#include "contact_repo.h"
#include "db_connection.h"
#include <sqlite3.h>

bool ContactRepo::add(const std::string& email, const std::string& name,
    const std::string& categories, const std::string& notes,
    const std::string& key) {

    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db) return false;

    const char* sql = "INSERT INTO contact (email, name, categories, notes, key) VALUES (?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, email.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, categories.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, notes.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, key.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

std::vector<ContactRecord> ContactRepo::queryAll() {
    std::vector<ContactRecord> result;
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db) return result;

    const char* sql = "SELECT id, email, name, categories, notes, key FROM contact ORDER BY name;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return result;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        ContactRecord r;
        r.id = sqlite3_column_int64(stmt, 0);
        r.email = (const char*)sqlite3_column_text(stmt, 1);
        r.name = sqlite3_column_text(stmt, 2) ? (const char*)sqlite3_column_text(stmt, 2) : "";
        r.categories = sqlite3_column_text(stmt, 3) ? (const char*)sqlite3_column_text(stmt, 3) : "";
        r.notes = sqlite3_column_text(stmt, 4) ? (const char*)sqlite3_column_text(stmt, 4) : "";
        r.key = sqlite3_column_text(stmt, 5) ? (const char*)sqlite3_column_text(stmt, 5) : "";
        result.push_back(r);
    }
    sqlite3_finalize(stmt);
    return result;
}

bool ContactRepo::remove(int64_t id) {
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db) return false;

    const char* sql = "DELETE FROM contact WHERE id = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;
    sqlite3_bind_int64(stmt, 1, id);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}
