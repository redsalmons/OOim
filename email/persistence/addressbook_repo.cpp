#include "addressbook_repo.h"
#include "db_connection.h"
#include <sqlite3.h>

bool AddressBookRepo::insertIfNotExists(const std::string& email, const std::string& name) {
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db || email.empty()) return false;

    // Extract display name from email if name is empty
    std::string displayName = name;
    if (displayName.empty()) {
        size_t atPos = email.find('@');
        if (atPos != std::string::npos) {
            displayName = email.substr(0, atPos);
        } else {
            displayName = email;
        }
    }

    const char* sql = "INSERT OR IGNORE INTO addressbook (email, name) VALUES (?, ?);";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, email.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, displayName.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

std::vector<AddressBookRecord> AddressBookRepo::queryAll() {
    std::vector<AddressBookRecord> result;
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db) return result;

    const char* sql = "SELECT id, email, name, group_name, notes, created_at, updated_at "
                      "FROM addressbook ORDER BY name;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return result;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        AddressBookRecord r;
        r.id = sqlite3_column_int64(stmt, 0);
        r.email = (const char*)sqlite3_column_text(stmt, 1);
        r.name = sqlite3_column_text(stmt, 2) ? (const char*)sqlite3_column_text(stmt, 2) : "";
        r.groupName = sqlite3_column_text(stmt, 3) ? (const char*)sqlite3_column_text(stmt, 3) : "";
        r.notes = sqlite3_column_text(stmt, 4) ? (const char*)sqlite3_column_text(stmt, 4) : "";
        r.createdAt = sqlite3_column_text(stmt, 5) ? (const char*)sqlite3_column_text(stmt, 5) : "";
        r.updatedAt = sqlite3_column_text(stmt, 6) ? (const char*)sqlite3_column_text(stmt, 6) : "";
        result.push_back(r);
    }
    sqlite3_finalize(stmt);
    return result;
}

bool AddressBookRepo::queryById(int64_t id, AddressBookRecord& out) {
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db) return false;

    const char* sql = "SELECT id, email, name, group_name, notes, created_at, updated_at "
                      "FROM addressbook WHERE id = ?;";
    sqlite3_stmt* stmt;
    bool found = false;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, id);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            out.id = sqlite3_column_int64(stmt, 0);
            out.email = (const char*)sqlite3_column_text(stmt, 1);
            out.name = sqlite3_column_text(stmt, 2) ? (const char*)sqlite3_column_text(stmt, 2) : "";
            out.groupName = sqlite3_column_text(stmt, 3) ? (const char*)sqlite3_column_text(stmt, 3) : "";
            out.notes = sqlite3_column_text(stmt, 4) ? (const char*)sqlite3_column_text(stmt, 4) : "";
            out.createdAt = sqlite3_column_text(stmt, 5) ? (const char*)sqlite3_column_text(stmt, 5) : "";
            out.updatedAt = sqlite3_column_text(stmt, 6) ? (const char*)sqlite3_column_text(stmt, 6) : "";
            found = true;
        }
        sqlite3_finalize(stmt);
    }
    return found;
}

bool AddressBookRepo::update(int64_t id, const std::string& name, const std::string& groupName, const std::string& notes) {
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db) return false;

    const char* sql = "UPDATE addressbook SET name = ?, group_name = ?, notes = ?, "
                      "updated_at = datetime('now','localtime') WHERE id = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, groupName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, notes.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 4, id);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool AddressBookRepo::remove(int64_t id) {
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db) return false;

    const char* sql = "DELETE FROM addressbook WHERE id = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;
    sqlite3_bind_int64(stmt, 1, id);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

std::vector<std::string> AddressBookRepo::queryGroups() {
    std::vector<std::string> result;
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db) return result;

    const char* sql = "SELECT DISTINCT group_name FROM addressbook WHERE group_name IS NOT NULL AND group_name != '' ORDER BY group_name;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            result.push_back((const char*)sqlite3_column_text(stmt, 0));
        }
        sqlite3_finalize(stmt);
    }
    return result;
}
