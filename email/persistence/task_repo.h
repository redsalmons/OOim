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
    std::string createdAt;
};

class TaskRepo {
public:
    // Insert a new task with basic email info, returns task id (0 on failure)
    int64_t insert(const std::string& account, const std::string& recipient,
                   const std::string& subject, const std::string& body,
                   const std::string& inReplyTo, const std::string& messageId,
                   const std::string& xMessageId, const std::string& sessionId,
                   const std::string& xSessionChart);

    // Query pending tasks for a specific account (status=0), ordered by id ASC
    std::vector<TaskRecord> queryPending(const std::string& account, int limit = 10);

    // Mark task as sent (status=1)
    bool markSent(int64_t id);

    // Mark task as failed (status=2)
    bool markFailed(int64_t id);

    // Delete task by id
    bool deleteTask(int64_t id);
};

#endif // PERSISTENCE_TASK_REPO_H
