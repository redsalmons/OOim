// C API for the Unified Session Manager.
// This is the single entry point for the Flutter/Dart layer.
// All protocol details (Signal vs MLS) are hidden behind this facade.

#include "email_core.h"
#include "email_core_common.h"
#include "unified_session.h"
#include "unified_session_manager.h"
#include "logger.h"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

using json = nlohmann::json;

static UnifiedSessionManager s_manager;

// Helper: parse JSON array string to vector<string>.
static std::vector<std::string> parseMembersJson(const char* membersJson) {
    std::vector<std::string> result;
    if (!membersJson || !*membersJson) return result;
    try {
        auto arr = json::parse(membersJson);
        for (auto& m : arr) {
            if (m.is_string()) result.push_back(m.get<std::string>());
        }
    } catch (...) {}
    return result;
}

// Helper: serialize a UnifiedSession to JSON.
static json sessionToJson(const UnifiedSession& s) {
    json j;
    j["session_id"] = s.sessionId;
    j["account"] = s.account;
    j["subject"] = s.subject;
    j["mode"] = sessionModeToString(s.mode);
    j["members"] = s.members;
    j["signal_session_id"] = s.signalSessionId;
    j["mls_group_id"] = s.mlsGroupId;
    j["root_message_id"] = s.rootMessageId;
    j["mailmen"] = s.mailmen;
    j["mailman_cursor"] = s.mailmanCursor;
    j["pinned"] = s.pinned;
    j["hidden"] = s.hidden;
    j["status"] = s.status;
    j["created_at"] = s.createdAt;
    j["updated_at"] = s.updatedAt;
    return j;
}

extern "C" {

// Create a new encrypted session.
// membersJson: JSON array of email addresses (must include the local account).
// Auto-selects Signal (2 members) or MLS (3+ members).
// outJson: {status, session_id, message_id, x_mailer, task_id, encrypted_body}
int us_create_session(const char* account, const char* subject,
                      const char* membersJson, const char* mailmenJson,
                      int pinned, int hidden,
                      char* outJson, int outSize) {
    if (!account || !membersJson || !outJson || outSize <= 0) return -1;

    auto members = parseMembersJson(membersJson);
    if (members.empty()) {
        json err = {{"status", "error"}, {"error", "invalid_members"}};
        snprintf(outJson, outSize, "%s", err.dump().c_str());
        return -2;
    }

    auto mailmen = parseMembersJson(mailmenJson);

    auto result = s_manager.createSession(account, subject ? subject : "", members, mailmen,
                                          pinned, hidden);

    json resp;
    resp["status"] = result.success ? "success" : "error";
    resp["session_id"] = result.sessionId;
    resp["message_id"] = result.messageId;
    resp["x_mailer"] = result.xMailer;
    resp["task_id"] = result.taskId;
    resp["encrypted_body"] = result.encryptedBody;
    if (!result.error.empty()) resp["error"] = result.error;

    snprintf(outJson, outSize, "%s", resp.dump().c_str());
    LOG_INFO("[US] us_create_session: account=%s members=%zu -> session=%s mode=%s\n",
             account, members.size(), result.sessionId.c_str(), result.xMailer.c_str());
    return result.success ? 0 : -3;
}

// Send a message in an existing session.
// Auto-routes to Signal or MLS based on the session's mode.
// outJson: {status, session_id, message_id, x_mailer, task_id, encrypted_body}
int us_send_message(const char* account, const char* sessionId,
                    const char* plaintext, const char* inReplyTo,
                    char* outJson, int outSize) {
    if (!account || !sessionId || !plaintext || !outJson || outSize <= 0) return -1;

    auto result = s_manager.sendMessage(account, sessionId, plaintext,
                                        inReplyTo ? inReplyTo : "");

    json resp;
    resp["status"] = result.success ? "success" : "error";
    resp["session_id"] = result.sessionId;
    resp["message_id"] = result.messageId;
    resp["x_mailer"] = result.xMailer;
    resp["task_id"] = result.taskId;
    resp["encrypted_body"] = result.encryptedBody;
    if (!result.error.empty()) resp["error"] = result.error;

    snprintf(outJson, outSize, "%s", resp.dump().c_str());
    return result.success ? 0 : -3;
}

// Handle an incoming encrypted message.
// Auto-detects protocol from xMailer and routes to the correct decryptor.
// outJson: {status, session_id, plaintext, sender, x_mailer}
int us_handle_incoming(const char* account, const char* from,
                       const char* xMailer, const char* body,
                       const char* messageId, const char* inReplyTo,
                       char* outJson, int outSize) {
    if (!account || !from || !xMailer || !body || !outJson || outSize <= 0) return -1;

    auto result = s_manager.handleIncoming(account, from, xMailer, body,
                                           messageId ? messageId : "",
                                           inReplyTo ? inReplyTo : "");

    json resp;
    resp["status"] = result.success ? "success" : "error";
    resp["session_id"] = result.sessionId;
    resp["plaintext"] = result.plaintext;
    resp["sender"] = result.sender;
    resp["x_mailer"] = result.xMailer;
    if (!result.error.empty()) resp["error"] = result.error;

    snprintf(outJson, outSize, "%s", resp.dump().c_str());
    return result.success ? 0 : -3;
}

// Add members to an existing session.
// If the session is Signal and total members > 2, triggers an irreversible upgrade to MLS.
// outJson: {status, session_id, upgraded, mode, task_id}
int us_add_members(const char* account, const char* sessionId,
                   const char* membersJson, const char* inReplyTo,
                   char* outJson, int outSize) {
    if (!account || !sessionId || !membersJson || !outJson || outSize <= 0) return -1;

    auto newMembers = parseMembersJson(membersJson);
    if (newMembers.empty()) {
        json err = {{"status", "error"}, {"error", "invalid_members"}};
        snprintf(outJson, outSize, "%s", err.dump().c_str());
        return -2;
    }

    auto result = s_manager.addMembers(account, sessionId, newMembers,
                                       inReplyTo ? inReplyTo : "");

    json resp;
    resp["status"] = result.success ? "success" : "error";
    resp["session_id"] = result.sessionId;
    resp["upgraded"] = result.upgraded;
    resp["x_mailer"] = result.xMailer;
    resp["task_id"] = result.taskId;
    if (!result.error.empty()) resp["error"] = result.error;

    // If upgraded, include the new mode.
    if (result.upgraded) {
        resp["mode"] = "mls";
    }

    snprintf(outJson, outSize, "%s", resp.dump().c_str());
    LOG_INFO("[US] us_add_members: session=%s new_members=%zu upgraded=%d\n",
             sessionId, newMembers.size(), result.upgraded ? 1 : 0);
    return result.success ? 0 : -3;
}

// List all active sessions for an account.
// outJson: {status, sessions: [{session_id, mode, members, ...}]}
int us_list_sessions(const char* account, char* outJson, int outSize) {
    if (!account || !outJson || outSize <= 0) return -1;

    auto sessions = s_manager.listSessions(account);

    json arr = json::array();
    for (auto& s : sessions) {
        arr.push_back(sessionToJson(s));
    }

    json resp;
    resp["status"] = "success";
    resp["sessions"] = arr;

    snprintf(outJson, outSize, "%s", resp.dump().c_str());
    return 0;
}

// Get a specific session by its unified session_id.
// outJson: {status, session: {session_id, mode, members, ...}}
int us_get_session(const char* sessionId, char* outJson, int outSize) {
    if (!sessionId || !outJson || outSize <= 0) return -1;

    auto session = s_manager.getSession(sessionId);

    json resp;
    if (session.sessionId.empty()) {
        resp["status"] = "error";
        resp["error"] = "session_not_found";
    } else {
        resp["status"] = "success";
        resp["session"] = sessionToJson(session);
    }

    snprintf(outJson, outSize, "%s", resp.dump().c_str());
    return session.sessionId.empty() ? -2 : 0;
}

// Update the pinned/hidden display flags of a session.
// outJson: {status}
int us_set_session_flags(const char* sessionId, int pinned, int hidden,
                         char* outJson, int outSize) {
    if (!sessionId || !outJson || outSize <= 0) return -1;

    bool ok = s_manager.setSessionFlags(sessionId, pinned, hidden);

    json resp;
    resp["status"] = ok ? "success" : "error";
    if (!ok) resp["error"] = "update_failed";
    snprintf(outJson, outSize, "%s", resp.dump().c_str());

    LOG_INFO("[US] us_set_session_flags: session=%s pinned=%d hidden=%d ok=%d\n",
             sessionId, pinned, hidden, ok ? 1 : 0);
    return ok ? 0 : -2;
}

// Check if a session is ready for sending.
// Returns 1 if ready, 0 if not ready, negative on error.
int us_is_ready(const char* account, const char* sessionId) {
    if (!account || !sessionId) return -1;
    return s_manager.isReady(account, sessionId) ? 1 : 0;
}

} // extern "C"
