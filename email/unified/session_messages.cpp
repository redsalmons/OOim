#include "session_messages.h"
#include "email_core_common.h"
#include "persistence/email_repo.h"
#include "persistence/file_transfer_repo.h"
#include "persistence/task_repo.h"
#include "x_mailer.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <queue>
#include <unordered_map>
#include <unordered_set>

using json = nlohmann::json;

namespace {

EmailRepo s_emailRepo;
FileTransferRepo s_ftRepo;
TaskRepo s_taskRepo;

bool isChunk(const std::string& xm) {
    return xm == XMailer::ATTACH_CHUNK || xm == XMailer::MLS_FILE_CHUNK;
}

// Rows reachable from the root through x-reply-to, in reply-chain order.
std::vector<ChainRow> chainRows(const UnifiedSession& us) {
    std::vector<ChainRow> rows;
    if (us.rootMessageId.empty()) return rows;

    // One row per x-message-id (a message can be stored twice, e.g. sent copy +
    // Sent-folder sync); keep the earliest.
    std::unordered_set<std::string> seen;
    for (auto& r : s_emailRepo.queryChainRows(us.account)) {
        if (seen.insert(r.messageId).second) rows.push_back(std::move(r));
    }

    std::unordered_map<std::string, std::vector<size_t>> children;
    size_t rootIdx = rows.size();
    for (size_t i = 0; i < rows.size(); i++) {
        if (rows[i].messageId == us.rootMessageId) rootIdx = i;
        else if (!rows[i].inReplyTo.empty()) children[rows[i].inReplyTo].push_back(i);
    }

    // Walk the tree from the root: always emit the eligible message that arrived
    // first, so replies sit below their parent and siblings interleave by rowid.
    auto later = [&](size_t a, size_t b) { return rows[a].rowid > rows[b].rowid; };
    std::priority_queue<size_t, std::vector<size_t>, decltype(later)> eligible(later);
    std::vector<ChainRow> ordered;
    if (rootIdx < rows.size()) {
        eligible.push(rootIdx);
    } else {
        // Root row not stored locally (yet): its direct replies still belong here.
        auto it = children.find(us.rootMessageId);
        if (it != children.end()) for (size_t c : it->second) eligible.push(c);
    }
    while (!eligible.empty()) {
        size_t i = eligible.top();
        eligible.pop();
        ordered.push_back(rows[i]);
        auto it = children.find(rows[i].messageId);
        if (it != children.end()) for (size_t c : it->second) eligible.push(c);
    }
    return ordered;
}

bool contains(const std::string& s, const char* needle) {
    return s.find(needle) != std::string::npos;
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

std::string stripHtml(const std::string& html) {
    std::string out;
    bool inTag = false;
    for (char c : html) {
        if (c == '<') inTag = true;
        else if (c == '>') inTag = false;
        else if (!inTag) out += c;
    }
    const std::pair<const char*, const char*> ents[] = {
        {"&nbsp;", " "}, {"&lt;", "<"}, {"&gt;", ">"}, {"&quot;", "\""}, {"&#39;", "'"}, {"&amp;", "&"}};
    for (auto& [from, to] : ents) {
        size_t pos;
        while ((pos = out.find(from)) != std::string::npos) out.replace(pos, strlen(from), to);
    }
    std::string collapsed;
    bool space = false;
    for (char c : out) {
        if (std::isspace((unsigned char)c)) { space = !collapsed.empty(); continue; }
        if (space) collapsed += ' ';
        space = false;
        collapsed += c;
    }
    return collapsed;
}

json fileEntry(const json& f) {
    std::string fileId = f.value("file_id", "");
    int received = 0, status = 0;
    FileTransferRecord rec;
    if (!fileId.empty() && s_ftRepo.queryByFileId(fileId, rec)) {
        // Sender rows progress by delivered chunk tasks; receiver rows by stored chunks.
        bool isSender = !rec.sender.empty() && rec.sender == rec.account;
        received = isSender ? rec.sentChunks : s_ftRepo.countReceivedChunks(fileId);
        status = rec.status;
    }
    return {{"file_id", fileId},
            {"file_name", f.value("file_name", "")},
            {"file_size", f.value("file_size", (int64_t)0)},
            {"total_chunks", f.value("total_chunks", 0)},
            {"received_chunks", received},
            {"transfer_status", status}};
}

bool isHandshakeMailer(const std::string& xm) {
    return xm == XMailer::PREKEY_BUNDLE || xm == XMailer::SESSION_INIT ||
           xm == XMailer::MLS_KEY_PACKAGE || xm == XMailer::MLS_WELCOME || xm == XMailer::MLS_COMMIT;
}

json displayOf(const ChainRow& r) {
    json m = {{"rowid", r.rowid}, {"x_message_id", r.messageId}, {"x_reply_to", r.inReplyTo},
              {"sender", r.fromAddr}, {"timestamp", r.date}, {"is_sent", r.isSent},
              {"x_mailer", r.xMailer}, {"kind", "text"}, {"text", ""}, {"batch_id", ""},
              {"files", json::array()}};

    // Handshake / control messages carry no user content (SESSION_INIT only holds a
    // fixed X3DH payload): render as a marker.
    if (isHandshakeMailer(r.xMailer)) {
        m["kind"] = "handshake";
        return m;
    }

    std::string text, html;
    parse_stored_body_text(r.rowid, text, html);

    if ((r.xMailer == XMailer::ATTACH_META || r.xMailer == XMailer::MLS_FILE_META) && !text.empty()) {
        try {
            auto meta = json::parse(text);
            if (meta.value("msg_type", "") == "file") {
                json files = json::array();
                if (meta.contains("files") && meta["files"].is_array() && !meta["files"].empty()) {
                    for (auto& f : meta["files"]) files.push_back(fileEntry(f));
                } else {
                    files.push_back(fileEntry(meta));
                }
                m["kind"] = "file";
                m["text"] = meta.value("text", "");
                m["batch_id"] = meta.value("batch_id", "");
                m["files"] = files;
                return m;
            }
        } catch (...) {}
    }

    // MLS application message: received rows are rewritten to plaintext; a sent row
    // may still hold the {plaintext, ...} task envelope.
    if (r.xMailer == XMailer::MLS_APP_MSG && !text.empty() && text.front() == '{') {
        try {
            auto j = json::parse(text);
            if (j.contains("plaintext")) text = j.value("plaintext", "");
            else if (j.contains("ciphertext")) text = "[MLS message pending decryption]";
        } catch (...) {}
    }

    const std::string lt = lower(text);
    if (contains(lt, "mls_invite") || contains(lt, "mls_key_package") || contains(lt, "mls_welcome") ||
        contains(lt, "mls_commit") || contains(text, "[MLS handshake:")) {
        m["kind"] = "handshake";
        return m;
    }

    // A Signal envelope that is still stored as wire JSON was not decrypted.
    if (contains(text, "\"signal_header\"") && contains(text, "\"version\"")) {
        try {
            auto j = json::parse(text);
            std::string t = j.value("signal_header", json::object()).value("msg_type", "");
            if (t == "prekey" || t == "init") { m["kind"] = "handshake"; return m; }
            text = t == "msg" ? "[Signal encrypted message]"
                 : t == "repair" ? "[Signal session repair requested]"
                 : "[Signal protocol message]";
        } catch (...) {}
    }

    m["text"] = text.empty() ? stripHtml(html) : text;
    return m;
}

} // namespace

json session_list_messages(const UnifiedSession& us) {
    json out = json::array();
    for (auto& r : chainRows(us)) {
        if (!isChunk(r.xMailer)) out.push_back(displayOf(r));
    }
    return out;
}

std::string session_last_message_id(const UnifiedSession& us) {
    std::string last;
    for (auto& r : chainRows(us)) {
        if (!isChunk(r.xMailer)) last = r.messageId;
    }
    if (last.empty()) last = us.rootMessageId;

    // Messages still in the outbox are not stored yet; follow their x-reply-to links
    // so consecutive sends chain instead of all replying to the same parent.
    auto links = s_taskRepo.queryOutboxLinks();
    std::unordered_set<std::string> visited{last};
    for (bool advanced = true; advanced;) {
        advanced = false;
        for (auto& [msgId, parent] : links) {
            if (parent == last && visited.insert(msgId).second) { last = msgId; advanced = true; break; }
        }
    }
    return last;
}
