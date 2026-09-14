#include "email_core.h"
#include "email_core_common.h"
#include "sender_key.h"
#include "sender_key_repo.h"
#include "group_session_repo.h"
#include "session_repo.h"
#include "signal_session_repo.h"
#include "db_connection.h"
#include "logger.h"
#include <nlohmann/json.hpp>
#include <cstring>
#include <ctime>

using json = nlohmann::json;

static GroupSessionRepo s_groupRepo;
static SenderKeyRepo s_senderKeyRepo;
static SessionRepo s_sessionRepo;
static SignalSessionRepo s_signalSessionRepo;

// --- Group management ---

extern "C" int group_create(const char* account, const char* groupId,
                            const char* groupEmail, const char* subject,
                            const char* membersJson, char* outJson, int outSize) {
    if (!account || !groupId || !groupEmail || !membersJson) return -1;

    GroupSessionRecord rec;
    rec.groupId = groupId;
    rec.groupEmail = groupEmail;
    rec.subject = subject ? subject : "";
    rec.owner = account ? account : "";
    rec.account = account ? account : "";
    rec.epoch = 0;
    rec.status = 0;

    try {
        auto arr = json::parse(membersJson);
        if (arr.is_array()) {
            for (auto& m : arr) {
                rec.members.push_back(m.get<std::string>());
            }
        }
    } catch (...) {
        snprintf(outJson, outSize, "{\"status\":\"error\",\"error\":\"invalid membersJson\"}");
        return -2;
    }

    if (!s_groupRepo.createGroup(rec)) {
        snprintf(outJson, outSize, "{\"status\":\"error\",\"error\":\"createGroup failed\"}");
        return -3;
    }

    json resp;
    resp["status"] = "success";
    resp["group_id"] = rec.groupId;
    resp["group_email"] = rec.groupEmail;
    resp["epoch"] = 0;
    resp["members"] = rec.members;
    snprintf(outJson, outSize, "%s", resp.dump().c_str());

    LOG_INFO("[Group] group_create: group=%s, members=%d\n", groupId, (int)rec.members.size());
    return 0;
}

extern "C" int group_set_x_reply_id(const char* account, const char* groupId,
                                     const char* xReplyId, char* outJson, int outSize) {
    if (!account || !groupId || !xReplyId || !outJson || outSize <= 0) return -1;

    if (!s_groupRepo.setXReplyId(groupId, xReplyId)) {
        snprintf(outJson, outSize, "{\"status\":\"error\",\"error\":\"setXReplyId failed\"}");
        return -2;
    }

    json resp;
    resp["status"] = "success";
    resp["group_id"] = groupId;
    resp["x_reply_id"] = xReplyId;
    snprintf(outJson, outSize, "%s", resp.dump().c_str());
    LOG_INFO("[Group] group_set_x_reply_id: group=%s x=%s\n", groupId, xReplyId);
    return 0;
}

extern "C" int group_add_member(const char* account, const char* groupId,
                                const char* memberEmail, char* outJson, int outSize) {
    if (!account || !groupId || !memberEmail) return -1;

    if (!s_groupRepo.addMember(groupId, memberEmail)) {
        snprintf(outJson, outSize, "{\"status\":\"error\",\"error\":\"addMember failed\"}");
        return -2;
    }
    s_groupRepo.incrementEpoch(groupId);

    // Regenerate our own SenderKey for the new epoch
    sender_key_generate(account, groupId);

    json resp;
    resp["status"] = "success";
    resp["group_id"] = groupId;
    resp["new_member"] = memberEmail;
    snprintf(outJson, outSize, "%s", resp.dump().c_str());

    LOG_INFO("[Group] group_add_member: group=%s, member=%s\n", groupId, memberEmail);
    return 0;
}

extern "C" int group_remove_member(const char* account, const char* groupId,
                                     const char* memberEmail, char* outJson, int outSize) {
    if (!account || !groupId || !memberEmail) return -1;

    s_groupRepo.removeMember(groupId, memberEmail);
    s_groupRepo.incrementEpoch(groupId);

    // Regenerate our own SenderKey for the new epoch
    sender_key_generate(account, groupId);

    json resp;
    resp["status"] = "success";
    resp["group_id"] = groupId;
    resp["removed_member"] = memberEmail;
    snprintf(outJson, outSize, "%s", resp.dump().c_str());

    LOG_INFO("[Group] group_remove_member: group=%s, member=%s\n", groupId, memberEmail);
    return 0;
}

extern "C" int group_get_info(const char* account, const char* groupId,
                              char* outJson, int outSize) {
    if (!account || !groupId) return -1;

    GroupSessionRecord rec;
    if (!s_groupRepo.loadGroup(groupId, rec)) {
        snprintf(outJson, outSize, "{\"status\":\"error\",\"error\":\"group not found\"}");
        return -2;
    }

    json resp;
    resp["status"] = "success";
    resp["group_id"] = rec.groupId;
    resp["group_email"] = rec.groupEmail;
    resp["x_reply_id"] = rec.xReplyId;
    resp["subject"] = rec.subject;
    resp["members"] = rec.members;
    resp["epoch"] = rec.epoch;
    resp["status_code"] = rec.status;

    // Check if ready to send
    resp["ready"] = (sender_key_check_ready(account, groupId) == 1);

    snprintf(outJson, outSize, "%s", resp.dump().c_str());
    return 0;
}

extern "C" int group_list(const char* account, char* outJson, int outSize) {
    if (!account) return -1;

    auto groups = s_groupRepo.listGroups(account);
    json arr = json::array();
    for (auto& g : groups) {
        json item;
        item["group_id"] = g.groupId;
        item["group_email"] = g.groupEmail;
        item["x_reply_id"] = g.xReplyId;
        item["subject"] = g.subject;
        item["owner"] = g.owner;
        item["members"] = g.members;
        item["account"] = g.account;
        item["epoch"] = g.epoch;
        item["ready"] = (sender_key_check_ready(account, g.groupId.c_str()) == 1);
        arr.push_back(item);
    }

    json resp;
    resp["status"] = "success";
    resp["groups"] = arr;
    snprintf(outJson, outSize, "%s", resp.dump().c_str());
    LOG_INFO("[Group] group_list: account=%s, count=%d\n", account, (int)groups.size());
    return 0;
}

// --- Sender Key ---

extern "C" int sender_key_generate(const char* account, const char* groupId,
                                   char* outJson, int outSize) {
    if (!account || !groupId) return -1;

    std::string payload = sender_key_generate(std::string(account), std::string(groupId));
    if (payload.empty()) {
        snprintf(outJson, outSize, "{\"status\":\"error\",\"error\":\"generate failed\"}");
        return -2;
    }
    snprintf(outJson, outSize, "%s", payload.c_str());
    return 0;
}

extern "C" int sender_key_get_distribution(const char* account, const char* groupId,
                                            char* outJson, int outSize) {
    if (!account || !groupId) return -1;

    std::string payload = sender_key_get_distribution(std::string(account), std::string(groupId));
    if (payload.empty()) {
        snprintf(outJson, outSize, "{\"status\":\"error\",\"error\":\"no SenderKey found\"}");
        return -2;
    }
    snprintf(outJson, outSize, "%s", payload.c_str());
    return 0;
}

extern "C" int sender_key_store(const char* account, const char* groupId,
                                const char* senderEmail, const char* chainKey,
                                const char* signingPub, int epoch) {
    if (!account || !groupId || !senderEmail || !chainKey || !signingPub) return -1;

    if (!sender_key_store(std::string(account), std::string(groupId),
                          std::string(senderEmail), std::string(chainKey),
                          std::string(signingPub), epoch)) {
        return -2;
    }
    return 0;
}

extern "C" int sender_key_check_ready(const char* account, const char* groupId) {
    if (!account || !groupId) return -1;
    return sender_key_check_ready(std::string(account), std::string(groupId));
}

// --- Group encrypt/decrypt ---

extern "C" int group_encrypt(const char* account, const char* groupId,
                              const char* plaintext, char* outJson, int outSize) {
    if (!account || !groupId || !plaintext) return -1;

    GroupEncryptResult result = group_encrypt(std::string(account),
                                               std::string(groupId),
                                               std::string(plaintext));

    json resp;
    if (result.success) {
        resp["status"] = "success";
        resp["group_id"] = result.groupId;
        resp["sender"] = result.senderEmail;
        resp["iteration"] = result.iteration;
        resp["epoch"] = result.epoch;
        resp["ciphertext"] = result.ciphertext;
        resp["signature"] = result.signature;
    } else {
        resp["status"] = "error";
        resp["error"] = result.error;
    }
    snprintf(outJson, outSize, "%s", resp.dump().c_str());
    return result.success ? 0 : -2;
}

extern "C" int group_decrypt(const char* account, const char* groupId,
                              const char* senderEmail, int iteration, int epoch,
                              const char* ciphertext, const char* signature,
                              char* outJson, int outSize) {
    if (!account || !groupId || !senderEmail || !ciphertext || !signature) return -1;

    GroupDecryptResult result = group_decrypt(std::string(account),
                                               std::string(groupId),
                                               std::string(senderEmail),
                                               iteration, epoch,
                                               std::string(ciphertext),
                                               std::string(signature));

    json resp;
    if (result.success) {
        resp["status"] = "success";
        resp["plaintext"] = result.plaintext;
    } else {
        resp["status"] = "error";
        resp["error"] = result.error;
    }
    snprintf(outJson, outSize, "%s", resp.dump().c_str());
    return result.success ? 0 : -2;
}

extern "C" int group_find_1to1_session(const char* account, const char* peerEmail,
                                        char* outSessionId, int outSize) {
    if (!account || !peerEmail || !outSessionId || outSize <= 0) return -1;
    outSessionId[0] = '\0';

    std::string bestSid;
    sqlite3* db = DbConnection::instance().get();
    if (db) {
        const char* sql = "SELECT session_id FROM signal_session "
                          "WHERE account=? AND peer_email=? AND status=0 "
                          "ORDER BY id DESC;";
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, account, -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, peerEmail, -1, SQLITE_TRANSIENT);
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                const char* sigSid = (const char*)sqlite3_column_text(stmt, 0);
                if (!sigSid) continue;
                std::string emailSid = s_sessionRepo.findSessionIdBySignalSessionId(account, sigSid);
                if (!emailSid.empty()) {
                    bestSid = emailSid;
                    break;
                }
            }
            sqlite3_finalize(stmt);
        }
    }

    if (bestSid.empty()) {
        SignalSessionRecord rec;
        if (s_signalSessionRepo.loadActiveSession(account, peerEmail, rec)) {
            bestSid = s_sessionRepo.findSessionIdBySignalSessionId(account, rec.sessionId);
        }
    }

    if (bestSid.empty()) return 0;
    if ((int)bestSid.size() >= outSize) return -2;
    std::strncpy(outSessionId, bestSid.c_str(), outSize - 1);
    outSessionId[outSize - 1] = '\0';
    return 0;
}
