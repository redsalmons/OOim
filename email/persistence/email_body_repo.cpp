#include "email_body_repo.h"
#include "db_connection.h"
#include <sqlite3.h>

bool EmailBodyRepo::upsert(int64_t emailId, const std::string& body) {
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db || emailId <= 0) return false;
    std::lock_guard<std::mutex> lock(conn.mutex());
    const char* sql = "INSERT INTO email_body (email_id, body) VALUES (?, ?) "
                      "ON CONFLICT(email_id) DO UPDATE SET body = excluded.body;";
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int64(st, 1, emailId);
    sqlite3_bind_text(st, 2, body.c_str(), (int)body.size(), SQLITE_TRANSIENT);
    int rc = sqlite3_step(st);
    sqlite3_finalize(st);
    return rc == SQLITE_DONE;
}

std::string EmailBodyRepo::get(int64_t emailId) {
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db || emailId <= 0) return "";
    std::lock_guard<std::mutex> lock(conn.mutex());
    const char* sql = "SELECT body FROM email_body WHERE email_id = ?;";
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) != SQLITE_OK) return "";
    sqlite3_bind_int64(st, 1, emailId);
    std::string body;
    if (sqlite3_step(st) == SQLITE_ROW && sqlite3_column_text(st, 0)) {
        body.assign((const char*)sqlite3_column_text(st, 0),
                    (size_t)sqlite3_column_bytes(st, 0));
    }
    sqlite3_finalize(st);
    return body;
}

bool EmailBodyRepo::remove(int64_t emailId) {
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db) return false;
    std::lock_guard<std::mutex> lock(conn.mutex());
    const char* sql = "DELETE FROM email_body WHERE email_id = ?;";
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int64(st, 1, emailId);
    int rc = sqlite3_step(st);
    sqlite3_finalize(st);
    return rc == SQLITE_DONE;
}

bool EmailBodyRepo::removeByAccount(const std::string& account) {
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db) return false;
    std::lock_guard<std::mutex> lock(conn.mutex());
    const char* sql = "DELETE FROM email_body WHERE email_id IN "
                      "(SELECT id FROM localemail WHERE account = ?);";
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(st, 1, account.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(st);
    sqlite3_finalize(st);
    return rc == SQLITE_DONE;
}
