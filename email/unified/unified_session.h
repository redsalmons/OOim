#ifndef UNIFIED_SESSION_H
#define UNIFIED_SESSION_H

#include <string>
#include <vector>

// Session mode: automatically selected based on member count.
// Signal = 1:1 Double Ratchet (exactly 2 members)
// Mls    = 1:n MLS ratchet tree  (3 or more members)
enum class SessionMode { Signal, Mls };

// unified_session.status
enum SessionStatus {
    kSessionActive = 0,
    kSessionClosed = 1,
    // The peer presented an identity key that differs from the pinned one; the
    // session is shown with a warning and refuses to process protocol mail until
    // the user resets trust (signal_peer_identity_reset).
    kSessionIdentityChanged = 2,
};

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
    std::vector<std::string> mailmen;  // 发送邮差池（round-robin 轮换）
    int mailmanCursor = 0;        // 轮换游标
    int pinned = 0;               // 1=置顶
    int hidden = 0;               // 1=隐藏
    int status = 0;               // SessionStatus
    std::string createdAt;
    std::string updatedAt;
};

// Result returned by createSession / sendMessage / addMembers.
struct SendResult {
    bool success = false;
    std::string sessionId;        // unified session id
    std::string messageId;        // x_message_id of the sent message (if any)
    std::string xMailer;          // X-Mailer value used (e.g. "1.0.1", "2.0.0")
    int64_t taskId = 0;           // task id if queued via task_repo
    bool upgraded = false;        // true if session was upgraded from Signal to MLS
    std::string error;
};

#endif // UNIFIED_SESSION_H
