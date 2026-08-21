#include "session_repo.h"
#include "db_connection.h"
#include <sqlite3.h>
#include <cstring>

std::string SessionRepo::querySessionByMessageId(const std::string& messageId, const std::string& account) {
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db || messageId.empty()) return "";

    const char* sql =
        "SELECT s.session_id FROM session s "
        "INNER JOIN localemail l ON l.id = s.email_id "
        "WHERE l.message_id = ? AND l.account = ? LIMIT 1;";

    sqlite3_stmt* stmt;
    std::string result;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, messageId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, account.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* sid = (const char*)sqlite3_column_text(stmt, 0);
            if (sid) result = sid;
        }
        sqlite3_finalize(stmt);
    }
    return result;
}

std::string SessionRepo::querySessionByInReplyTo(const std::string& inReplyTo, const std::string& account) {
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db || inReplyTo.empty()) return "";

    const char* sql =
        "SELECT s.session_id FROM session s "
        "JOIN localemail l ON s.email_id = l.id "
        "WHERE l.message_id = ? AND l.account = ? LIMIT 1;";

    sqlite3_stmt* stmt;
    std::string result;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, inReplyTo.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, account.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* sid = (const char*)sqlite3_column_text(stmt, 0);
            if (sid) result = sid;
        }
        sqlite3_finalize(stmt);
    }
    return result;
}

bool SessionRepo::addEmailToSession(const std::string& sessionId, int64_t emailId, int encryptMethod) {
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db) return false;

    // Multi-step: check exists -> update or insert. Use transaction.
    conn.beginTransaction();

    const char* check_sql = "SELECT id FROM session WHERE email_id = ?;";
    sqlite3_stmt* check_stmt;
    bool exists = false;
    if (sqlite3_prepare_v2(db, check_sql, -1, &check_stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int64(check_stmt, 1, emailId);
        if (sqlite3_step(check_stmt) == SQLITE_ROW) {
            exists = true;
        }
        sqlite3_finalize(check_stmt);
    }

    bool ok = false;
    if (exists) {
        const char* sql = "UPDATE session SET session_id = ?, auto = 1, encrypt_method = ? WHERE email_id = ?;";
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, sessionId.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt, 2, encryptMethod);
            sqlite3_bind_int64(stmt, 3, emailId);
            ok = (sqlite3_step(stmt) == SQLITE_DONE);
            sqlite3_finalize(stmt);
        }
    } else {
        const char* sql = "INSERT INTO session (session_id, email_id, visible, auto, isread, encrypt_method) VALUES (?, ?, 1, 0, 0, ?);";
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, sessionId.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(stmt, 2, emailId);
            sqlite3_bind_int(stmt, 3, encryptMethod);
            ok = (sqlite3_step(stmt) == SQLITE_DONE);
            sqlite3_finalize(stmt);
        }
    }

    if (ok) {
        conn.commitTransaction();
    } else {
        conn.rollbackTransaction();
    }
    return ok;
}

bool SessionRepo::insertSessionAssoc(const std::string& sessionId, int64_t emailId) {
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db) return false;

    const char* sql = "INSERT OR IGNORE INTO session (session_id, email_id, visible, auto, isread) VALUES (?, ?, 1, 0, 0);";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, sessionId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, emailId);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

int64_t SessionRepo::queryFirstEmailId(const std::string& sessionId) {
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db) return 0;

    const char* sql = "SELECT email_id FROM session WHERE session_id = ? LIMIT 1;";
    sqlite3_stmt* stmt;
    int64_t result = 0;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, sessionId.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            result = sqlite3_column_int64(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }
    return result;
}

std::string SessionRepo::queryFirstMessageId(const std::string& sessionId) {
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db || sessionId.empty()) return "";

    const char* sql =
        "SELECT l.message_id FROM session s "
        "INNER JOIN localemail l ON l.id = s.email_id "
        "WHERE s.session_id = ? AND l.message_id != '' "
        "ORDER BY s.id ASC LIMIT 1;";
    sqlite3_stmt* stmt;
    std::string result;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, sessionId.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* mid = (const char*)sqlite3_column_text(stmt, 0);
            if (mid) result = mid;
        }
        sqlite3_finalize(stmt);
    }
    return result;
}

bool SessionRepo::updateRead(const std::string& sessionId) {
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db) return false;

    const char* sql = "UPDATE session SET isread = 1 WHERE session_id = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, sessionId.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

int SessionRepo::countUnread(const std::string& sessionId) {
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db) return -1;

    const char* sql = "SELECT COUNT(*) FROM session WHERE session_id = ? AND isread = 0;";
    sqlite3_stmt* stmt;
    int count = 0;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, sessionId.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            count = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }
    return count;
}

bool SessionRepo::hideSession(const std::string& sessionId) {
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db) return false;

    const char* sql = "UPDATE session SET visible = 0 WHERE session_id = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, sessionId.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}
