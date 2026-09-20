#ifndef UNIFIED_SESSION_H
#define UNIFIED_SESSION_H

#include <string>
#include <vector>

// Session mode: automatically selected based on member count.
// Signal = 1:1 Double Ratchet (exactly 2 members)
// Mls    = 1:n MLS ratchet tree  (3 or more members)
enum class SessionMode { Signal, Mls };

inline const char* sessionModeToString(SessionMode m) {
    return m == SessionMode::Signal ? "signal" : "mls";
}

inline SessionMode sessionModeFromString(const std::string& s) {
    return s == "mls" ? SessionMode::Mls : SessionMode::Signal;
}

// A unified session record persisted in the unified_session table.
struct UnifiedSession {
    std::string sessionId;        // "us_<timestamp>.<random>"
    std::string account;          // local account email
    std::string subject;
    SessionMode mode = SessionMode::Signal;
    std::vector<std::string> members;
    std::string signalSessionId;  // sig_xxx when mode=Signal
    std::string mlsGroupId;       // group_session.group_id when mode=Mls
    std::string rootMessageId;    // x_message_id of the first message
    int status = 0;               // 0=active, 1=closed
    std::string createdAt;
    std::string updatedAt;
};

// Result returned by createSession / sendMessage / addMembers.
struct SendResult {
    bool success = false;
    std::string sessionId;        // unified session id
    std::string messageId;        // x_message_id of the sent message (if any)
    std::string xMailer;          // X-Mailer value used (e.g. "1.0.1", "2.0.0")
    std::string encryptedBody;    // encrypted body JSON (for direct SMTP send)
    int64_t taskId = 0;           // task id if queued via task_repo
    bool upgraded = false;        // true if session was upgraded from Signal to MLS
    std::string error;
};

// Result returned by handleIncoming.
struct RecvResult {
    bool success = false;
    std::string sessionId;        // unified session id
    std::string plaintext;        // decrypted message body
    std::string sender;           // sender email address
    std::string xMailer;          // original X-Mailer value
    std::string error;
};

#endif // UNIFIED_SESSION_H
