#ifndef PERSISTENCE_TASK_REPO_H
#define PERSISTENCE_TASK_REPO_H

#include <string>
#include <cstdint>
#include <vector>

struct TaskRecord {
    int64_t id;
    std::string account;
    std::string recipient;
    std::string subject;
    std::string body;
    std::string inReplyTo;
    std::string messageId;
    std::string xMessageId;
    std::string sessionId;
    std::string xSessionChart;
    int status;         // 0=pending, 1=sent, 2=failed
    int preEncrypted;   // body is ciphertext; transport sends it verbatim
    std::string localBody;   // plaintext kept for the local .eml
    int retryCount;
    std::string nextRetryAt; // NULL = sendable now
    std::string lastError;
    std::string createdAt;
};

class TaskRepo {
public:
    // Insert a new task with basic email info, returns task id (0 on failure).
    // preEncrypted=1 means body already holds the ciphertext and localBody holds
    // the plaintext for the local archive; the transport must send it verbatim.
    int64_t insert(const std::string& account, const std::string& recipient,
                   const std::string& subject, const std::string& body,
                   const std::string& inReplyTo, const std::string& messageId,
                   const std::string& xMessageId, const std::string& sessionId,
                   const std::string& xSessionChart,
                   int preEncrypted = 0, const std::string& localBody = "");

    // Query sendable tasks for a specific account (status=0 and due), ordered by id ASC
    std::vector<TaskRecord> queryPending(const std::string& account, int limit = 10);

    // Query one task by account + message_id (for GUI outbox status)
    bool queryByMessageId(const std::string& account, const std::string& messageId, TaskRecord& out);

    // Mark task as sent (status=1)
    bool markSent(int64_t id);

    // Mark task as permanently failed (status=2) with the error text
    bool markFailed(int64_t id, const std::string& error = "");

    // Retryable failure: keep status=0 but delay it by backoffSeconds.
    // Returns false once retryCount reaches maxRetries (the task is then marked failed).
    bool markRetryable(int64_t id, int backoffSeconds, const std::string& error, int maxRetries = 3);

    // Delete task by id
    bool deleteTask(int64_t id);
};

#endif // PERSISTENCE_TASK_REPO_H
