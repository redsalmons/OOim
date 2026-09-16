#include "mls_repo.h"
#include "db_connection.h"
#include <sqlite3.h>

bool MlsRepo::saveState(const std::string& account, const std::vector<uint8_t>& blob) {
    sqlite3* db = DbConnection::instance().get();
    if (!db) return false;
    const char* sql = "INSERT INTO mls_state (account, state, updated_at) VALUES (?, ?, datetime('now','localtime')) "
                      "ON CONFLICT(account) DO UPDATE SET state=excluded.state, updated_at=excluded.updated_at;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, account.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_blob(stmt, 2, blob.data(), (int)blob.size(), SQLITE_TRANSIENT);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

bool MlsRepo::loadState(const std::string& account, std::vector<uint8_t>& out) {
    sqlite3* db = DbConnection::instance().get();
    if (!db) return false;
    const char* sql = "SELECT state FROM mls_state WHERE account=? LIMIT 1;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, account.c_str(), -1, SQLITE_TRANSIENT);
    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const void* p = sqlite3_column_blob(stmt, 0);
        int n = sqlite3_column_bytes(stmt, 0);
        out.assign((const uint8_t*)p, (const uint8_t*)p + n);
        found = n > 0;
    }
    sqlite3_finalize(stmt);
    return found;
}

bool MlsRepo::setMlsGroupId(const std::string& groupId, const std::string& mlsGroupIdHex) {
    sqlite3* db = DbConnection::instance().get();
    if (!db) return false;
    int64_t gid = 0;
    try { gid = std::stoll(groupId); } catch (...) { return false; }
    const char* sql = "UPDATE group_session SET mls_group_id=?, updated_at=datetime('now','localtime') WHERE group_id=?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, mlsGroupIdHex.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, gid);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

std::string MlsRepo::getMlsGroupId(const std::string& groupId) {
    sqlite3* db = DbConnection::instance().get();
    if (!db) return "";
    int64_t gid = 0;
    try { gid = std::stoll(groupId); } catch (...) { return ""; }
    const char* sql = "SELECT mls_group_id FROM group_session WHERE group_id=? LIMIT 1;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return "";
    sqlite3_bind_int64(stmt, 1, gid);
    std::string r;
    if (sqlite3_step(stmt) == SQLITE_ROW && sqlite3_column_text(stmt, 0)) {
        r = (const char*)sqlite3_column_text(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return r;
}

std::string MlsRepo::findGroupIdByMlsGroupId(const std::string& account, const std::string& mlsGroupIdHex) {
    sqlite3* db = DbConnection::instance().get();
    if (!db || mlsGroupIdHex.empty()) return "";
    const char* sql = "SELECT group_id FROM group_session WHERE account=? AND mls_group_id=? LIMIT 1;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return "";
    sqlite3_bind_text(stmt, 1, account.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, mlsGroupIdHex.c_str(), -1, SQLITE_TRANSIENT);
    std::string r;
    if (sqlite3_step(stmt) == SQLITE_ROW) r = std::to_string(sqlite3_column_int64(stmt, 0));
    sqlite3_finalize(stmt);
    return r;
}
