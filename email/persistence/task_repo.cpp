#include "task_repo.h"
#include "db_connection.h"
#include "logger.h"
#include "x_mailer.h"
#include <sqlite3.h>

int64_t TaskRepo::insert(const std::string& account, const std::string& recipient,
                          const std::string& subject, const std::string& body,
                          const std::string& inReplyTo, const std::string& messageId,
                          const std::string& xMessageId, const std::string& sessionId,
                          const std::string& xSessionChart) {
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db) {
        LOG_INFO("[TaskRepo] insert failed: db is null\n");
        return 0;
    }

    const char* sql = "INSERT INTO task (account, recipient, subject, body, in_reply_to, message_id, x_message_id, session_id, x_session_chart, status) "
                      "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, 0);";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        LOG_INFO("[TaskRepo] insert failed: prepare error %s\n", sqlite3_errmsg(db));
        return 0;
    }

    sqlite3_bind_text(stmt, 1, account.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, recipient.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, subject.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, body.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, inReplyTo.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, messageId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, xMessageId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 8, sessionId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 9, xSessionChart.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        LOG_INFO("[TaskRepo] insert failed: step error %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        return 0;
    }
    sqlite3_finalize(stmt);

    int64_t rowid = sqlite3_last_insert_rowid(db);
    LOG_INFO("[TaskRepo] insert success: rowid=%lld\n", (long long)rowid);
    return rowid;
}

std::vector<TaskRecord> TaskRepo::queryPending(const std::string& account, int limit) {
    std::vector<TaskRecord> result;
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db) return result;

    const char* sql =
        "SELECT id, account, recipient, subject, body, in_reply_to, message_id, x_message_id, session_id, x_session_chart, status, created_at FROM task "
        "WHERE account = ? AND status = 0 ORDER BY id ASC LIMIT ?;";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return result;
    sqlite3_bind_text(stmt, 1, account.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, limit);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        TaskRecord rec;
        rec.id = sqlite3_column_int64(stmt, 0);
        rec.account = (const char*)sqlite3_column_text(stmt, 1);
        rec.recipient = (const char*)sqlite3_column_text(stmt, 2);
        rec.subject = (const char*)sqlite3_column_text(stmt, 3);
        rec.body = (const char*)sqlite3_column_text(stmt, 4);
        rec.inReplyTo = (const char*)sqlite3_column_text(stmt, 5) ? (const char*)sqlite3_column_text(stmt, 5) : "";
        rec.messageId = (const char*)sqlite3_column_text(stmt, 6) ? (const char*)sqlite3_column_text(stmt, 6) : "";
        rec.xMessageId = (const char*)sqlite3_column_text(stmt, 7) ? (const char*)sqlite3_column_text(stmt, 7) : "";
        rec.sessionId = (const char*)sqlite3_column_text(stmt, 8) ? (const char*)sqlite3_column_text(stmt, 8) : "";
        rec.xSessionChart = (const char*)sqlite3_column_text(stmt, 9) ? (const char*)sqlite3_column_text(stmt, 9) : XMailer::TEXT;
        rec.status = sqlite3_column_int(stmt, 10);
        const char* createdPtr = (const char*)sqlite3_column_text(stmt, 11);
        rec.createdAt = createdPtr ? createdPtr : "";
        result.push_back(rec);
    }
    sqlite3_finalize(stmt);
    return result;
}

bool TaskRepo::markSent(int64_t id) {
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db) return false;

    const char* sql = "UPDATE task SET status = 1 WHERE id = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;
    sqlite3_bind_int64(stmt, 1, id);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool TaskRepo::markFailed(int64_t id) {
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db) return false;

    const char* sql = "UPDATE task SET status = 2 WHERE id = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;
    sqlite3_bind_int64(stmt, 1, id);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool TaskRepo::deleteTask(int64_t id) {
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db) return false;

    const char* sql = "DELETE FROM task WHERE id = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;
    sqlite3_bind_int64(stmt, 1, id);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}
