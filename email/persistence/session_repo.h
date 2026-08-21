#ifndef PERSISTENCE_SESSION_REPO_H
#define PERSISTENCE_SESSION_REPO_H

#include <string>
#include <cstdint>

struct SessionInfo {
    std::string sessionId;
    int64_t emailId = 0;
    int visible = 1;
    int autoFlag = 0;
    int isRead = 0;
    int encryptMethod = 0;
};

class SessionRepo {
public:
    // Query session_id by message_id + account
    std::string querySessionByMessageId(const std::string& messageId, const std::string& account);

    // Query session_id by in_reply_to + account (for joining existing sessions)
    std::string querySessionByInReplyTo(const std::string& inReplyTo, const std::string& account);

    // Add email to session (upsert by email_id). Multi-step: uses transaction.
    bool addEmailToSession(const std::string& sessionId, int64_t emailId, int encryptMethod);

    // Insert session association directly (INSERT OR IGNORE)
    bool insertSessionAssoc(const std::string& sessionId, int64_t emailId);

    // Query first email_id for a session
    int64_t queryFirstEmailId(const std::string& sessionId);

    // Query first email's message_id for a session
    std::string queryFirstMessageId(const std::string& sessionId);

    // Mark session as read
    bool updateRead(const std::string& sessionId);

    // Count unread in session
    int countUnread(const std::string& sessionId);

    // Hide session (set visible=0)
    bool hideSession(const std::string& sessionId);
};

#endif // PERSISTENCE_SESSION_REPO_H
