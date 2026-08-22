#ifndef PERSISTENCE_EMAIL_REPO_H
#define PERSISTENCE_EMAIL_REPO_H

#include <string>
#include <cstdint>
#include <vector>

struct EmailRecord {
    std::string uuid;
    std::string account;
    std::string sender;
    std::string fromAddr;
    std::string toAddr;
    std::string subject;
    std::string date;
    std::string bodystructure;
    std::string replyTo;
    std::string inReplyTo;
    std::string messageId;
    std::string flags;
    std::string folder;
    int isLocal = 0;
    int visible = 1;
    std::string sessionId;
    std::string servicerecvtime;
    int64_t rowid = 0;
    std::string file;
};

struct PendingEmail {
    std::string uuid;
    std::string folder;
    int islocal = 0;
};

class EmailRepo {
public:
    // Query all emails for an account (with session join)
    std::vector<EmailRecord> queryByAccount(const std::string& account);

    // Query thread roots (first email of each session for an account)
    std::vector<EmailRecord> queryThreadRoots(const std::string& account);

    // Query all emails in a session thread
    std::vector<EmailRecord> queryThread(const std::string& sessionId);

    // Check if email exists by message_id + account, return uuid if found
    std::string findUuidByMessageId(const std::string& messageId, const std::string& account);

    // Find rowid by uuid
    int64_t findRowidByUuid(const std::string& uuid);

    // Find rowid by uuid + account
    int64_t findRowidByUuidAndAccount(const std::string& uuid, const std::string& account);

    // Check if email exists by message_id + account, return rowid if found
    int64_t findIdByMessageId(const std::string& messageId, const std::string& account);

    // Find sent email (uuid=0) by in_reply_to + account, for dedup when SMTP rewrote Message-ID
    int64_t findSentByInReplyTo(const std::string& inReplyTo, const std::string& account);

    // Insert new email record, return rowid (0 on failure)
    int64_t insert(const EmailRecord& rec);

    // Migration: Update islocal for existing emails
    bool migrateIslocalForNoSessionChart();

    // Update existing email by id (IMAP metadata backfill)
    bool updateById(int64_t id, const EmailRecord& rec);

    // Insert sent email with pending uuid=0, return rowid
    int64_t insertSentEmail(const std::string& account, const std::string& sender,
        const std::string& fromAddr, const std::string& toAddr,
        const std::string& subject, const std::string& date,
        const std::string& messageId, const std::string& inReplyTo,
        const std::string& bodystructure, const std::string& file);

    // Update email after download (set islocal=2, message_id, in_reply_to, file)
    bool updateAfterDownload(const std::string& uuid, const std::string& account,
        const std::string& messageId, const std::string& inReplyTo,
        const std::string& file);

    // Set islocal flag for an email (used when body is downloaded in fetch phase)
    bool setIslocal(const std::string& uuid, const std::string& account, int islocal);

    // Increment retry count
    bool incrementRetryCount(const std::string& uuid, const std::string& account);

    // Count pending bodies for an account
    int countPendingBodies(const std::string& account);

    // Query pending emails (uuid, folder) for download
    std::vector<PendingEmail> queryPendingEmails(const std::string& account, int limit = 10);

    // Get max uuid for account+folder
    std::string getMaxUid(const std::string& account, const std::string& folder);
};

#endif // PERSISTENCE_EMAIL_REPO_H
