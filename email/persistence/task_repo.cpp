#include "task_repo.h"
#include "db_connection.h"
#include "logger.h"
#include "x_mailer.h"
#include <sqlite3.h>

int64_t TaskRepo::insert(const std::string& account, const std::string& recipient,
                          const std::string& subject, const std::string& body,
                          const std::string& inReplyTo, const std::string& messageId,
                          const std::string& xMessageId, const std::string& sessionId,
                          const std::string& xSessionChart,
                          int preEncrypted, const std::string& localBody) {
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db) {
        LOG_INFO("[TaskRepo] insert failed: db is null\n");
        return 0;
    }

    const char* sql = "INSERT INTO task (account, recipient, subject, body, in_reply_to, message_id, x_message_id, session_id, x_session_chart, status, pre_encrypted, local_body) "
                      "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, 0, ?, ?);";
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
    sqlite3_bind_int(stmt, 10, preEncrypted);
    sqlite3_bind_text(stmt, 11, localBody.c_str(), -1, SQLITE_TRANSIENT);

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
        "SELECT id, account, recipient, subject, body, in_reply_to, message_id, x_message_id, session_id, x_session_chart, status, pre_encrypted, local_body, retry_count, next_retry_at, last_error, created_at FROM task "
        "WHERE account = ? AND status = 0 "
        "AND (next_retry_at IS NULL OR next_retry_at = '' OR next_retry_at <= datetime('now','localtime')) "
        "ORDER BY id ASC LIMIT ?;";

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
        rec.preEncrypted = sqlite3_column_int(stmt, 11);
        const char* lbPtr = (const char*)sqlite3_column_text(stmt, 12);
        rec.localBody = lbPtr ? lbPtr : "";
        rec.retryCount = sqlite3_column_int(stmt, 13);
        const char* nrPtr = (const char*)sqlite3_column_text(stmt, 14);
        rec.nextRetryAt = nrPtr ? nrPtr : "";
        const char* lePtr = (const char*)sqlite3_column_text(stmt, 15);
        rec.lastError = lePtr ? lePtr : "";
        const char* createdPtr = (const char*)sqlite3_column_text(stmt, 16);
        rec.createdAt = createdPtr ? createdPtr : "";
        result.push_back(rec);
    }
    sqlite3_finalize(stmt);
    return result;
}

bool TaskRepo::queryByMessageId(const std::string& account, const std::string& messageId, TaskRecord& out) {
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db || messageId.empty()) return false;

    const char* sql =
        "SELECT id, account, recipient, subject, body, in_reply_to, message_id, x_message_id, session_id, x_session_chart, status, pre_encrypted, local_body, retry_count, next_retry_at, last_error, created_at FROM task "
        "WHERE account = ? AND message_id = ? ORDER BY id DESC LIMIT 1;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, account.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, messageId.c_str(), -1, SQLITE_TRANSIENT);

    bool found = sqlite3_step(stmt) == SQLITE_ROW;
    if (found) {
        out.id = sqlite3_column_int64(stmt, 0);
        out.account = (const char*)sqlite3_column_text(stmt, 1);
        out.recipient = (const char*)sqlite3_column_text(stmt, 2);
        out.subject = (const char*)sqlite3_column_text(stmt, 3);
        out.body = (const char*)sqlite3_column_text(stmt, 4);
        const char* irPtr = (const char*)sqlite3_column_text(stmt, 5);
        out.inReplyTo = irPtr ? irPtr : "";
        const char* miPtr = (const char*)sqlite3_column_text(stmt, 6);
        out.messageId = miPtr ? miPtr : "";
        const char* xmiPtr = (const char*)sqlite3_column_text(stmt, 7);
        out.xMessageId = xmiPtr ? xmiPtr : "";
        const char* sidPtr = (const char*)sqlite3_column_text(stmt, 8);
        out.sessionId = sidPtr ? sidPtr : "";
        const char* xscPtr = (const char*)sqlite3_column_text(stmt, 9);
        out.xSessionChart = xscPtr ? xscPtr : XMailer::TEXT;
        out.status = sqlite3_column_int(stmt, 10);
        out.preEncrypted = sqlite3_column_int(stmt, 11);
        const char* lbPtr = (const char*)sqlite3_column_text(stmt, 12);
        out.localBody = lbPtr ? lbPtr : "";
        out.retryCount = sqlite3_column_int(stmt, 13);
        const char* nrPtr = (const char*)sqlite3_column_text(stmt, 14);
        out.nextRetryAt = nrPtr ? nrPtr : "";
        const char* lePtr = (const char*)sqlite3_column_text(stmt, 15);
        out.lastError = lePtr ? lePtr : "";
        const char* createdPtr = (const char*)sqlite3_column_text(stmt, 16);
        out.createdAt = createdPtr ? createdPtr : "";
    }
    sqlite3_finalize(stmt);
    return found;
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

bool TaskRepo::markFailed(int64_t id, const std::string& error) {
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db) return false;

    const char* sql = "UPDATE task SET status = 2, last_error = ? WHERE id = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, error.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, id);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool TaskRepo::markRetryable(int64_t id, int backoffSeconds, const std::string& error, int maxRetries) {
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db) return false;

    // If the retry budget is already spent, fail the task permanently.
    const char* peekSql = "SELECT retry_count FROM task WHERE id = ?;";
    sqlite3_stmt* peekStmt;
    if (sqlite3_prepare_v2(db, peekSql, -1, &peekStmt, NULL) != SQLITE_OK) return false;
    sqlite3_bind_int64(peekStmt, 1, id);
    bool hasRow = sqlite3_step(peekStmt) == SQLITE_ROW;
    int retried = hasRow ? sqlite3_column_int(peekStmt, 0) : 0;
    sqlite3_finalize(peekStmt);
    if (!hasRow) return false;
    if (retried >= maxRetries) {
        markFailed(id, error);
        LOG_INFO("[TaskRepo] task %lld retry budget exhausted (%d), marked failed: %s\n",
                 (long long)id, retried, error.c_str());
        return false;
    }

    // Keep status=0, push next_retry_at forward, count the retry.
    const char* sql = "UPDATE task SET retry_count = retry_count + 1, last_error = ?, "
                      "next_retry_at = datetime('now','localtime', '+' || ? || ' seconds') WHERE id = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, error.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, backoffSeconds);
    sqlite3_bind_int64(stmt, 3, id);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc == SQLITE_DONE) {
        LOG_INFO("[TaskRepo] task %lld retry #%d scheduled in %ds: %s\n",
                 (long long)id, retried + 1, backoffSeconds, error.c_str());
    }
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
