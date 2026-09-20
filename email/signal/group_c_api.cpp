// C API + protocol orchestration for 1:n group sessions over MLS (RFC 9420).
//
// Wire format (X-Mailer 2.0.x, body = JSON):
//   2.0.0  type=mls_invite       owner -> all members   {x_session_id, x_message_id, x_reply_to:"", subject, members, owner}
//   2.0.0  type=mls_key_package  member -> owner        {x_session_id, x_message_id, x_reply_to:<last>, key_package}
//   2.0.1  type=mls_welcome      owner -> new member    {x_session_id, x_message_id, x_reply_to:<last>, welcome, ratchet_tree}
//   2.0.2  type=mls_commit       owner -> old members   {x_session_id, x_message_id, x_reply_to:<last>, commit}
//   2.0.3  (application)         member -> members      {x_session_id, x_message_id, x_reply_to:<last>, sender, ciphertext}
//
// Session rules:
//   - x_session_id is created with the group (value = root invite's x_message_id) and
//     carried by every message in the session; receivers resolve the local group by it.
//   - x_reply_to always points to the x_message_id of the last message the sender
//     saw in the conversation; x_message_id is generated locally per message.
// This file never touches the 1:1 Double Ratchet code.

#include "email_core.h"
#include "email_core_common.h"
#include "mls_group.h"
#include "group_session_repo.h"
#include "email_repo.h"
#include "task_repo.h"
#include "unified/unified_session_repo.h"
#include "persistence/file_transfer_repo.h"
#include "file_chunk_util.h"
#include "x_mailer.h"
#include "logger.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <cstring>
#include <ctime>
#include <chrono>
#include <random>

using json = nlohmann::json;

namespace {

GroupSessionRepo g_groupRepo;
EmailRepo g_emailRepo;
TaskRepo g_taskRepo;

std::string newMessageId(const std::string& account) {
    static std::mt19937_64 rng{std::random_device{}()};
    std::string domain = account.substr(account.find('@') + 1);
    return "<" + std::to_string(std::time(nullptr)) + "." + std::to_string(rng() % 100000000) + "@" + domain + ">";
}

std::string joinRecipients(const std::vector<std::string>& v, const std::string& exclude) {
    std::string out;
    for (auto& m : v) {
        if (m == exclude) continue;
        if (!out.empty()) out += ", ";
        out += m;
    }
    return out;
}

// Resolve the local group of an incoming/outgoing message. x_session_id is the
// authoritative key (shared by all members); the x_reply_to chain is only a
// fallback for messages that predate the field.
std::string resolveGroup(const std::string& account, const std::string& parentId, const std::string& xSessionId) {
    if (!xSessionId.empty()) {
        GroupSessionRecord rec;
        if (g_groupRepo.loadBySessionId(xSessionId, account, rec)) return rec.groupId;
    }
    if (parentId.empty()) return "";
    int64_t gid = g_emailRepo.findGroupIdByMessageId(account, parentId);
    if (gid > 0) return std::to_string(gid);
    GroupSessionRecord rec;
    if (g_groupRepo.loadByXReplyId(parentId, account, rec)) return rec.groupId;
    return "";
}

void associateSent(const std::string& account, const std::string& groupId, const std::string& messageId) {
    int64_t emailId = g_emailRepo.findIdByMessageId(messageId, account);
    if (emailId <= 0) return;
    char sr[4096];
    email_add_email_to_session(("group_" + groupId).c_str(), std::to_string(emailId).c_str(), account.c_str(), 1, sr, sizeof(sr));
}

int reply(char* out, int size, const json& j) {
    if (out && size > 0) snprintf(out, size, "%s", j.dump().c_str());
    return 0;
}
int fail(char* out, int size, int code, const std::string& err) {
    if (out && size > 0) snprintf(out, size, "%s", json{{"status", "error"}, {"error", err}}.dump().c_str());
    LOG_INFO("[MLS] %s\n", err.c_str());
    return code;
}

void copyOut(char* out, int size, const std::string& s) {
    if (!out || size <= 0) return;
    snprintf(out, size, "%s", s.c_str());
}

// ─── receive handlers ────────────────────────────────────────────────────────

// 2.0.0 mls_invite: create the local group and answer with our KeyPackage.
int onInvite(const std::string& account, const std::string& from, const json& body, const std::string& msgId, std::string& groupId) {
    // x_session_id = creator-generated session key; pre-field invites use the root msg id.
    std::string xSessionId = body.value("x_session_id", msgId);
    GroupSessionRecord rec;
    if (g_groupRepo.loadBySessionId(xSessionId, account, rec) || g_groupRepo.loadByXReplyId(msgId, account, rec)) {
        groupId = rec.groupId;
        LOG_INFO("[MLS] invite root=%s already known as group=%s\n", msgId.c_str(), groupId.c_str());
        return 0;
    }
    rec.xReplyId = msgId;
    rec.xSessionId = xSessionId;
    rec.subject = body.value("subject", "");
    rec.owner = body.value("owner", from);
    rec.account = account;
    for (auto& m : body.value("members", json::array())) if (m.is_string()) rec.members.push_back(m.get<std::string>());
    if (!g_groupRepo.createGroup(rec)) return -10;
    groupId = rec.groupId;

    // Upsert the unified session so this group appears in the conversation list.
    // If x_session_id matches an existing 1:1 session's root_message_id, that
    // session is upgraded to MLS in place (1:1 -> group continuation).
    {
        static UnifiedSessionRepo s_usRepo;
        UnifiedSession us;
        if (s_usRepo.loadByRootMessageId(account, xSessionId, us)) {
            if (s_usRepo.upgradeToMls(us.sessionId, groupId, rec.members)) {
                LOG_INFO("[MLS] invite upgraded existing 1:1 session=%s to mls group=%s\n",
                         us.sessionId.c_str(), groupId.c_str());
            }
        } else {
            UnifiedSession usRec;
            static std::mt19937_64 rng{std::random_device{}()};
            auto secs = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            usRec.sessionId = "us_" + std::to_string(secs) + "." + std::to_string(rng() % 100000000);
            usRec.account = account;
            usRec.subject = rec.subject;
            usRec.mode = SessionMode::Mls;
            usRec.members = rec.members;
            usRec.mlsGroupId = groupId;
            usRec.rootMessageId = xSessionId;
            usRec.status = 0;
            if (s_usRepo.create(usRec)) {
                LOG_INFO("[MLS] invite created unified session=%s for group=%s\n",
                         usRec.sessionId.c_str(), groupId.c_str());
            }
        }
    }

    std::string kp = mls::generateKeyPackage(account);
    if (kp.empty()) return -11;
    std::string kpMsgId = newMessageId(account);
    json kpBody = {{"type", "mls_key_package"}, {"x_session_id", rec.xSessionId},
                   {"x_message_id", kpMsgId}, {"x_reply_to", msgId}, {"key_package", kp}};
    int64_t tid = g_taskRepo.insert(account, rec.owner, rec.subject, kpBody.dump(), msgId, kpMsgId, "", "", XMailer::MLS_KEY_PACKAGE);
    LOG_INFO("[MLS] invite from %s -> group=%s, queued KeyPackage to owner task=%lld\n", from.c_str(), groupId.c_str(), (long long)tid);
    return tid > 0 ? 0 : -12;
}

// 2.0.0 mls_key_package (owner side): add the member, send Welcome to them and Commit to the rest.
int onKeyPackage(const std::string& account, const std::string& from, const json& body, const std::string& msgId,
                 const std::string& parentId, const std::string& xSessionId, std::string& groupId) {
    groupId = resolveGroup(account, parentId, xSessionId);
    if (groupId.empty()) { LOG_INFO("[MLS] key_package: session %s parent %s unknown\n", xSessionId.c_str(), parentId.c_str()); return -20; }
    GroupSessionRecord rec;
    if (!g_groupRepo.loadGroup(groupId, rec)) return -21;
    if (rec.owner != account) { LOG_INFO("[MLS] key_package received by non-owner, ignored\n"); return 0; }

    std::vector<std::string> before = mls::members(account, groupId);
    auto r = mls::addMembers(account, groupId, {body.value("key_package", "")});
    if (!r.ok) return -22;

    std::string wId = newMessageId(account);
    json welcome = {{"type", "mls_welcome"}, {"x_session_id", rec.xSessionId},
                    {"x_message_id", wId}, {"x_reply_to", msgId},
                    {"welcome", r.welcomeB64}, {"ratchet_tree", r.treeB64}};
    int64_t t1 = g_taskRepo.insert(account, from, rec.subject, welcome.dump(), msgId, wId, "", "", XMailer::MLS_WELCOME);

    std::string others = joinRecipients(before, account);
    if (!others.empty()) {
        std::string cId = newMessageId(account);
        json commit = {{"type", "mls_commit"}, {"x_session_id", rec.xSessionId},
                       {"x_message_id", cId}, {"x_reply_to", msgId}, {"commit", r.commitB64}};
        int64_t t2 = g_taskRepo.insert(account, others, rec.subject, commit.dump(), msgId, cId, "", "", XMailer::MLS_COMMIT);
        LOG_INFO("[MLS] queued Commit to [%s] task=%lld\n", others.c_str(), (long long)t2);
    }
    g_groupRepo.incrementEpoch(groupId);
    LOG_INFO("[MLS] added %s to group=%s, queued Welcome task=%lld\n", from.c_str(), groupId.c_str(), (long long)t1);
    return t1 > 0 ? 0 : -23;
}

// 2.0.1 mls_welcome (member side): join.
int onWelcome(const std::string& account, const json& body, const std::string& parentId, const std::string& xSessionId, std::string& groupId) {
    groupId = resolveGroup(account, parentId, xSessionId);
    if (groupId.empty()) { LOG_INFO("[MLS] welcome: session %s parent %s unknown\n", xSessionId.c_str(), parentId.c_str()); return 1; }
    if (mls::isReady(account, groupId)) { LOG_INFO("[MLS] welcome: group=%s already joined\n", groupId.c_str()); return 0; }
    std::string gid = mls::joinGroup(account, groupId, body.value("welcome", ""), body.value("ratchet_tree", ""));
    return gid.empty() ? -30 : 0;
}

// 2.0.2 mls_commit (member side): advance epoch. Retry if we have not joined yet.
int onCommit(const std::string& account, const json& body, const std::string& parentId, const std::string& xSessionId, std::string& groupId) {
    groupId = resolveGroup(account, parentId, xSessionId);
    if (groupId.empty()) { LOG_INFO("[MLS] commit: session %s parent %s unknown\n", xSessionId.c_str(), parentId.c_str()); return 1; }
    if (!mls::isReady(account, groupId)) { LOG_INFO("[MLS] commit before welcome for group=%s, retry later\n", groupId.c_str()); return 1; }
    if (!mls::processCommit(account, groupId, body.value("commit", ""))) return 1;
    g_groupRepo.incrementEpoch(groupId);
    return 0;
}

// 2.0.3 application message.
int onAppMessage(const std::string& account, const json& body, const std::string& parentId, const std::string& xSessionId,
                 std::string& groupId, std::string& plaintext) {
    groupId = resolveGroup(account, parentId, xSessionId);
    if (groupId.empty()) { LOG_INFO("[MLS] app msg: session %s parent %s unknown\n", xSessionId.c_str(), parentId.c_str()); return 1; }
    if (!mls::isReady(account, groupId)) return 1;
    return mls::decrypt(account, groupId, body.value("ciphertext", ""), plaintext) ? 0 : -40;
}

} // namespace

// ─── C API ───────────────────────────────────────────────────────────────────

// Shared group creation. xSessionIdOverride: when non-empty the group inherits an
// existing conversation's x_session_id (e.g. a 1:1 session's root x-message-id during
// a Signal->MLS upgrade) instead of generating a fresh one. inReplyTo: the invite's
// x_reply_to (last message id of the conversation, "" for a brand-new group).
int groupCreateInternal(const std::string& account, const std::string& subject,
                        const std::vector<std::string>& members,
                        const std::string& xSessionIdOverride, const std::string& inReplyTo,
                        char* outJson, int outSize) {
    GroupSessionRecord rec;
    rec.subject = subject;
    rec.owner = rec.account = account;
    rec.members = members;
    if (std::find(rec.members.begin(), rec.members.end(), rec.account) == rec.members.end()) rec.members.insert(rec.members.begin(), rec.account);
    rec.xReplyId = newMessageId(account);
    rec.xSessionId = xSessionIdOverride.empty() ? rec.xReplyId : xSessionIdOverride;
    if (!g_groupRepo.createGroup(rec)) return fail(outJson, outSize, -3, "createGroup failed");
    if (mls::createGroup(account, rec.groupId).empty()) return fail(outJson, outSize, -4, "mls createGroup failed");

    json invite = {{"type", "mls_invite"}, {"x_session_id", rec.xSessionId},
                   {"x_message_id", rec.xReplyId}, {"x_reply_to", inReplyTo},
                   {"subject", rec.subject}, {"members", rec.members}, {"owner", rec.owner}};
    std::string recipients = joinRecipients(rec.members, rec.account);
    int64_t tid = g_taskRepo.insert(account, recipients, rec.subject, invite.dump(), inReplyTo, rec.xReplyId, "", "", XMailer::MLS_KEY_PACKAGE);
    if (tid <= 0) return fail(outJson, outSize, -5, "queue invite failed");
    LOG_INFO("[MLS] group_create: group=%s root=%s session=%s recipients=[%s] task=%lld\n",
             rec.groupId.c_str(), rec.xReplyId.c_str(), rec.xSessionId.c_str(), recipients.c_str(), (long long)tid);
    return reply(outJson, outSize, {{"status", "success"}, {"group_id", rec.groupId}, {"root_message_id", rec.xReplyId}, {"task_id", tid}});
}

extern "C" int group_create(const char* account, const char* subject, const char* membersJson, char* outJson, int outSize) {
    if (!account || !membersJson) return -1;
    std::vector<std::string> members;
    try {
        for (auto& m : json::parse(membersJson)) if (m.is_string()) members.push_back(m.get<std::string>());
    } catch (...) { return fail(outJson, outSize, -2, "invalid membersJson"); }
    return groupCreateInternal(account, subject ? subject : "", members, "", "", outJson, outSize);
}

extern "C" int group_create_ex(const char* account, const char* subject, const char* membersJson,
                               const char* xSessionId, const char* inReplyTo,
                               char* outJson, int outSize) {
    if (!account || !membersJson) return -1;
    std::vector<std::string> members;
    try {
        for (auto& m : json::parse(membersJson)) if (m.is_string()) members.push_back(m.get<std::string>());
    } catch (...) { return fail(outJson, outSize, -2, "invalid membersJson"); }
    return groupCreateInternal(account, subject ? subject : "", members,
                               xSessionId ? xSessionId : "", inReplyTo ? inReplyTo : "",
                               outJson, outSize);
}

extern "C" int group_get_info(const char* account, const char* groupId, char* outJson, int outSize) {
    if (!account || !groupId) return -1;
    GroupSessionRecord rec;
    if (!g_groupRepo.loadGroup(groupId, rec)) return fail(outJson, outSize, -2, "group not found");
    return reply(outJson, outSize, {{"status", "success"}, {"group_id", rec.groupId}, {"group_email", rec.groupEmail},
                                    {"x_reply_id", rec.xReplyId}, {"x_session_id", rec.xSessionId},
                                    {"subject", rec.subject}, {"owner", rec.owner},
                                    {"members", rec.members}, {"joined", mls::members(account, groupId)},
                                    {"epoch", mls::epoch(account, groupId)}, {"status_code", rec.status},
                                    {"ready", mls::isReady(account, groupId)}});
}

extern "C" int group_list(const char* account, char* outJson, int outSize) {
    if (!account) return -1;
    json arr = json::array();
    for (auto& g : g_groupRepo.listGroups(account)) {
        arr.push_back({{"group_id", g.groupId}, {"group_email", g.groupEmail}, {"x_reply_id", g.xReplyId},
                       {"x_session_id", g.xSessionId},
                       {"subject", g.subject}, {"owner", g.owner}, {"members", g.members}, {"account", g.account},
                       {"epoch", mls::epoch(account, g.groupId)}, {"ready", mls::isReady(account, g.groupId)}});
    }
    return reply(outJson, outSize, {{"status", "success"}, {"groups", arr}});
}

extern "C" int group_send_message(const char* account, const char* groupId, const char* plaintext, const char* inReplyTo,
                                  char* outJson, int outSize) {
    if (!account || !groupId || !plaintext) return -1;
    GroupSessionRecord rec;
    if (!g_groupRepo.loadGroup(groupId, rec)) return fail(outJson, outSize, -2, "group not found");
    if (!mls::isReady(account, groupId)) return fail(outJson, outSize, -3, "group not ready");
    std::string parent = inReplyTo ? inReplyTo : "";
    if (parent.empty()) parent = rec.xReplyId;
    std::vector<std::string> tree = mls::members(account, groupId);
    std::string recipients = joinRecipients(tree, account);
    if (recipients.empty()) return fail(outJson, outSize, -4, "no other member has joined yet");

    std::string msgId = newMessageId(account);
    json body = {{"x_session_id", rec.xSessionId}, {"x_message_id", msgId}, {"x_reply_to", parent},
                 {"sender", account}, {"plaintext", plaintext}};
    int64_t tid = g_taskRepo.insert(account, recipients, rec.subject, body.dump(), parent, msgId, "", "", XMailer::MLS_APP_MSG);
    if (tid <= 0) return fail(outJson, outSize, -5, "queue failed");
    LOG_INFO("[MLS] queued app msg group=%s id=%s parent=%s to=[%s] task=%lld\n", groupId, msgId.c_str(), parent.c_str(), recipients.c_str(), (long long)tid);
    return reply(outJson, outSize, {{"status", "success"}, {"task_id", tid}, {"message_id", msgId}});
}

// 1:n file transfer: compress -> chunk -> queue one 2.0.4 meta task + N 2.0.5 chunk tasks.
// Task bodies carry {x_session_id, x_message_id, x_reply_to, sender, plaintext:<file/truck JSON>};
// group_prepare_outgoing encrypts each plaintext right before sending (tree ratchet order ==
// send order). unifiedSessionId is the us_xxx session whose mls_group_id selects the group.
extern "C" int group_send_file(const char* account, const char* unifiedSessionId,
                               const char* filePath, const char* fileName,
                               const char* inReplyTo, const char* subject,
                               const char* text, const char* batchId,
                               char* outJson, int outSize) {
    if (!account || !unifiedSessionId || !filePath || !fileName) {
        return fail(outJson, outSize, -1, "null_parameter");
    }
    static UnifiedSessionRepo s_usRepo;
    UnifiedSession us;
    if (!s_usRepo.load(unifiedSessionId, us) || us.mlsGroupId.empty()) {
        return fail(outJson, outSize, -2, "not an MLS session");
    }
    std::string groupId = us.mlsGroupId;
    GroupSessionRecord rec;
    if (!g_groupRepo.loadGroup(groupId, rec)) return fail(outJson, outSize, -3, "group not found");
    if (!mls::isReady(account, groupId)) return fail(outJson, outSize, -4, "group not ready");
    std::vector<std::string> tree = mls::members(account, groupId);
    std::string recipients = joinRecipients(tree, account);
    if (recipients.empty()) return fail(outJson, outSize, -5, "no other member has joined yet");

    filechunk::PreparedFile pf;
    if (!filechunk::prepareFileForSend(filePath, fileName, pf)) {
        return fail(outJson, outSize, -6, "file not found");
    }

    std::string acc(account), domain = acc.substr(acc.find('@') + 1);
    std::string fileId = "file_" + std::to_string(std::time(nullptr) * 1000) + "_" + std::to_string(rand() % 100000);
    std::string parent = inReplyTo ? inReplyTo : "";
    if (parent.empty()) parent = rec.xReplyId;
    std::string subjectStr = subject ? subject : "";

    static FileTransferRepo s_ftRepo;
    FileTransferRecord ft;
    ft.fileId = fileId; ft.sessionId = unifiedSessionId; ft.account = acc; ft.sender = acc;
    ft.fileName = pf.fileName; ft.fileSize = pf.fileSize; ft.fileMd5 = pf.fileMd5;
    ft.totalChunks = pf.totalChunks; ft.chunkSize = pf.chunkSize; ft.status = 0;
    ft.originalPath = filePath;
    ft.compression = pf.compression; ft.compressedMd5 = pf.compressedMd5;
    if (!s_ftRepo.insertFileTransfer(ft)) {
        filechunk::releasePrepared(pf);
        return fail(outJson, outSize, -7, "db_insert_failed");
    }

    std::string fileMsgId = "<file_" + fileId + "@" + domain + ">";
    json meta = {{"msg_type", "file"}, {"file_id", fileId}, {"file_name", pf.fileName},
                 {"file_size", pf.fileSize}, {"file_md5", pf.fileMd5},
                 {"compression", pf.compression}, {"compressed_size", pf.compressedSize},
                 {"compressed_md5", pf.compressedMd5},
                 {"total_chunks", pf.totalChunks}, {"chunk_size", pf.chunkSize},
                 {"text", text ? text : ""}, {"batch_id", batchId ? batchId : ""},
                 {"x_message_id", fileMsgId}, {"x_reply_to", parent}};
    json metaEnv = {{"x_session_id", rec.xSessionId}, {"x_message_id", fileMsgId},
                    {"x_reply_to", parent}, {"sender", acc}, {"plaintext", meta.dump()}};
    int64_t tid = g_taskRepo.insert(acc, recipients, subjectStr, metaEnv.dump(),
                                  parent, fileMsgId, "", "", XMailer::MLS_FILE_META);
    if (tid <= 0) {
        filechunk::releasePrepared(pf);
        return fail(outJson, outSize, -8, "queue failed");
    }

    std::string prevMsgId = fileMsgId;
    int queued = 0;
    for (int i = 0; i < pf.totalChunks; i++) {
        auto chunk = filechunk::readChunk(pf, i);
        if (chunk.empty() && i < pf.totalChunks - 1) {
            LOG_INFO("[MLS] group_send_file: failed to read chunk %d\n", i);
            continue;
        }
        std::string truckMsgId = "<truck_" + fileId + "_" + std::to_string(i) + "@" + domain + ">";
        json truck = {{"msg_type", "truck"}, {"file_id", fileId}, {"chunk_index", i},
                      {"chunk_data", base64_encode(chunk.data(), chunk.size())},
                      {"chunk_md5", compute_md5(std::string(chunk.begin(), chunk.end()))},
                      {"x_message_id", truckMsgId}, {"x_reply_to", prevMsgId}};
        json truckEnv = {{"x_session_id", rec.xSessionId}, {"x_message_id", truckMsgId},
                         {"x_reply_to", prevMsgId}, {"sender", acc}, {"plaintext", truck.dump()}};
        g_taskRepo.insert(acc, recipients, subjectStr, truckEnv.dump(),
                          prevMsgId, truckMsgId, "", "", XMailer::MLS_FILE_CHUNK);
        prevMsgId = truckMsgId;
        queued++;
    }
    filechunk::releasePrepared(pf);

    LOG_INFO("[MLS] group_send_file: group=%s file=%s size=%lld compression=%s chunks=%d file_id=%s\n",
             groupId.c_str(), pf.fileName.c_str(), (long long)pf.fileSize,
             pf.compression.c_str(), pf.totalChunks, fileId.c_str());
    return reply(outJson, outSize, {{"status", "success"}, {"file_id", fileId},
                 {"file_name", pf.fileName}, {"total_chunks", pf.totalChunks},
                 {"message_id", fileMsgId}});
}

extern "C" int group_handle_incoming(const char* account, const char* fromAddr, const char* xMailer, const char* bodyText,
                                     const char* message_id, const char* in_reply_to,
                                     char* outGroupId, int gidSize, char* outPlaintext, int ptSize) {
    if (!account || !xMailer || !bodyText) return -1;
    json body;
    try { body = json::parse(bodyText); } catch (...) { LOG_INFO("[MLS] incoming body is not JSON\n"); return -2; }
    std::string acc = account, from = fromAddr ? fromAddr : "", xm = xMailer;
    std::string msgId = body.value("x_message_id", message_id ? message_id : "");
    std::string parent = body.value("x_reply_to", in_reply_to ? in_reply_to : "");
    std::string xSessionId = body.value("x_session_id", "");
    std::string type = body.value("type", "");
    std::string groupId, plaintext;
    int rc;

    if (xm == XMailer::MLS_KEY_PACKAGE && type == "mls_invite")           rc = onInvite(acc, from, body, msgId, groupId);
    else if (xm == XMailer::MLS_KEY_PACKAGE && type == "mls_key_package") rc = onKeyPackage(acc, from, body, msgId, parent, xSessionId, groupId);
    else if (xm == XMailer::MLS_WELCOME)                                  rc = onWelcome(acc, body, parent, xSessionId, groupId);
    else if (xm == XMailer::MLS_COMMIT)                                   rc = onCommit(acc, body, parent, xSessionId, groupId);
    else if (xm == XMailer::MLS_APP_MSG || xm == XMailer::MLS_FILE_META || xm == XMailer::MLS_FILE_CHUNK)
                                                                     rc = onAppMessage(acc, body, parent, xSessionId, groupId, plaintext);
    else { LOG_INFO("[MLS] unknown message xmailer=%s type=%s\n", xMailer, type.c_str()); return -3; }

    copyOut(outGroupId, gidSize, groupId);
    copyOut(outPlaintext, ptSize, plaintext);
    LOG_INFO("[MLS] incoming %s/%s from=%s id=%s parent=%s -> group=%s rc=%d\n", xMailer, type.c_str(), from.c_str(), msgId.c_str(), parent.c_str(), groupId.c_str(), rc);
    return rc;
}

extern "C" int group_prepare_outgoing(const char* account, const char* xMailer, const char* body, const char* inReplyTo,
                                      char* outBody, int outSize) {
    if (!account || !xMailer || !body || !outBody) return -1;
    {
        const std::string xm(xMailer);
        if (xm != XMailer::MLS_APP_MSG && xm != XMailer::MLS_FILE_META && xm != XMailer::MLS_FILE_CHUNK) {
            copyOut(outBody, outSize, body);
            return 0;
        }
    }
    json j;
    try { j = json::parse(body); } catch (...) { return -2; }
    std::string groupId = resolveGroup(account, j.value("x_reply_to", inReplyTo ? inReplyTo : ""),
                                       j.value("x_session_id", ""));
    if (groupId.empty()) { LOG_INFO("[MLS] outgoing: cannot resolve group for session=%s parent=%s\n",
                                    j.value("x_session_id", "").c_str(), j.value("x_reply_to", "").c_str()); return -3; }
    std::string ct;
    if (!mls::encrypt(account, groupId, j.value("plaintext", ""), ct)) return -4;
    json wire = {{"x_session_id", j.value("x_session_id", "")},
                 {"x_message_id", j.value("x_message_id", "")}, {"x_reply_to", j.value("x_reply_to", "")},
                 {"sender", j.value("sender", account)}, {"ciphertext", ct}};
    std::string s = wire.dump();
    if ((int)s.size() >= outSize) return -5;
    copyOut(outBody, outSize, s);
    return 0;
}

extern "C" int group_after_sent(const char* account, const char* xMailer, const char* messageId, const char* inReplyTo,
                                const char* localBody, const char* dataDir) {
    if (!account || !xMailer || !messageId) return -1;
    std::string acc = account, mid = messageId, parent = inReplyTo ? inReplyTo : "";
    // localBody is the task body JSON (all MLS messages carry x_session_id in it)
    std::string xSessionId;
    if (localBody && *localBody) {
        try { xSessionId = json::parse(localBody).value("x_session_id", ""); } catch (...) {}
    }
    std::string anchor = parent.empty() ? mid : parent;
    std::string groupId = resolveGroup(acc, anchor, xSessionId);
    if (groupId.empty()) { LOG_INFO("[MLS] after_sent: no group for id=%s parent=%s session=%s\n",
                                    messageId, parent.c_str(), xSessionId.c_str()); return -2; }
    associateSent(acc, groupId, mid);

    const std::string xmStr(xMailer);
    if ((xmStr == XMailer::MLS_APP_MSG || xmStr == XMailer::MLS_FILE_META || xmStr == XMailer::MLS_FILE_CHUNK)
        && localBody && dataDir && *dataDir) {
        // Keep the plaintext locally: rewrite the body of the sent .eml (headers unchanged).
        // localBody is the task envelope {..., "plaintext": <payload>}; store the payload
        // itself — plain text for app messages, the file meta JSON for 2.0.4, and a small
        // placeholder for multi-MB chunk envelopes.
        std::string displayBody;
        if (xmStr == XMailer::MLS_FILE_CHUNK) {
            displayBody = "[file chunk]";
        } else {
            try {
                displayBody = json::parse(localBody).value("plaintext", std::string(localBody));
            } catch (...) {
                displayBody = localBody;
            }
        }
        std::string fname = mid;
        if (fname.size() > 2 && fname.front() == '<' && fname.back() == '>') fname = fname.substr(1, fname.size() - 2);
        std::filesystem::path p = std::filesystem::path(dataDir) / acc / (fname + ".eml");
        std::ifstream in(p, std::ios::binary);
        if (in) {
            std::string content((std::istreambuf_iterator<char>(in)), {});
            in.close();
            size_t sep = content.find("\n\n");
            size_t sepLen = 2;
            size_t crlf = content.find("\r\n\r\n");
            if (crlf != std::string::npos && (sep == std::string::npos || crlf < sep)) { sep = crlf; sepLen = 4; }
            if (sep != std::string::npos) {
                std::ofstream out(p, std::ios::binary | std::ios::trunc);
                out << content.substr(0, sep + sepLen) << displayBody;
                LOG_INFO("[MLS] after_sent: stored plaintext copy %s\n", p.string().c_str());
            }
        }
    }
    LOG_INFO("[MLS] after_sent %s id=%s -> group=%s\n", xMailer, messageId, groupId.c_str());
    return 0;
}
