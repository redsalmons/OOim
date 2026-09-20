#include "unified_session_repo.h"
#include "db_connection.h"
#include "logger.h"
#include <sqlite3.h>
#include <nlohmann/json.hpp>
#include <algorithm>

using json = nlohmann::json;

// ---------- helpers ----------

std::string UnifiedSessionRepo::membersToJson(const std::vector<std::string>& members) {
    json arr = json::array();
    for (auto& m : members) arr.push_back(m);
    return arr.dump();
}

std::vector<std::string> UnifiedSessionRepo::jsonToMembers(const std::string& jsonStr) {
    std::vector<std::string> result;
    if (jsonStr.empty()) return result;
    try {
        auto arr = json::parse(jsonStr);
        for (auto& m : arr) {
            if (m.is_string()) result.push_back(m.get<std::string>());
        }
    } catch (...) {}
    return result;
}

void UnifiedSessionRepo::fillRecord(sqlite3_stmt* stmt, UnifiedSession& out) {
    out.sessionId       = sqlite3_column_text(stmt, 0) ? (const char*)sqlite3_column_text(stmt, 0) : "";
    out.account         = sqlite3_column_text(stmt, 1) ? (const char*)sqlite3_column_text(stmt, 1) : "";
    out.subject         = sqlite3_column_text(stmt, 2) ? (const char*)sqlite3_column_text(stmt, 2) : "";
    out.mode            = sessionModeFromString(
                            sqlite3_column_text(stmt, 3) ? (const char*)sqlite3_column_text(stmt, 3) : "signal");
    out.members         = jsonToMembers(
                            sqlite3_column_text(stmt, 4) ? (const char*)sqlite3_column_text(stmt, 4) : "[]");
    out.signalSessionId = sqlite3_column_text(stmt, 5) ? (const char*)sqlite3_column_text(stmt, 5) : "";
    out.mlsGroupId      = sqlite3_column_text(stmt, 6) ? (const char*)sqlite3_column_text(stmt, 6) : "";
    out.rootMessageId   = sqlite3_column_text(stmt, 7) ? (const char*)sqlite3_column_text(stmt, 7) : "";
    out.status          = sqlite3_column_int(stmt, 8);
    out.createdAt       = sqlite3_column_text(stmt, 9) ? (const char*)sqlite3_column_text(stmt, 9) : "";
    out.updatedAt       = sqlite3_column_text(stmt, 10) ? (const char*)sqlite3_column_text(stmt, 10) : "";
}

// ---------- CRUD ----------

bool UnifiedSessionRepo::create(const UnifiedSession& rec) {
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db) return false;

    const char* sql = "INSERT OR IGNORE INTO unified_session "
                      "(session_id, account, subject, mode, members, "
                      " signal_session_id, mls_group_id, root_message_id, status) "
                      "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        LOG_INFO("[USRepo] create prepare error: %s\n", sqlite3_errmsg(db));
        return false;
    }

    sqlite3_bind_text(stmt, 1, rec.sessionId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, rec.account.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, rec.subject.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, sessionModeToString(rec.mode), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, membersToJson(rec.members).c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, rec.signalSessionId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, rec.mlsGroupId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 8, rec.rootMessageId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 9, rec.status);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        LOG_INFO("[USRepo] create step error: %s\n", sqlite3_errmsg(db));
        return false;
    }

    LOG_INFO("[USRepo] created session=%s mode=%s members=%zu\n",
             rec.sessionId.c_str(), sessionModeToString(rec.mode), rec.members.size());
    return true;
}

bool UnifiedSessionRepo::load(const std::string& sessionId, UnifiedSession& out) {
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db) return false;

    const char* sql = "SELECT session_id, account, subject, mode, members, "
                      "signal_session_id, mls_group_id, root_message_id, status, "
                      "created_at, updated_at "
                      "FROM unified_session WHERE session_id=? LIMIT 1;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, sessionId.c_str(), -1, SQLITE_TRANSIENT);

    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        fillRecord(stmt, out);
        found = true;
    }
    sqlite3_finalize(stmt);
    return found;
}

bool UnifiedSessionRepo::loadByMembers(const std::string& account,
                                        const std::vector<std::string>& members,
                                        UnifiedSession& out) {
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db) return false;

    // Load all active sessions for this account and compare member sets.
    const char* sql = "SELECT session_id, account, subject, mode, members, "
                      "signal_session_id, mls_group_id, root_message_id, status, "
                      "created_at, updated_at "
                      "FROM unified_session WHERE account=? AND status=0;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, account.c_str(), -1, SQLITE_TRANSIENT);

    bool found = false;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        UnifiedSession rec;
        fillRecord(stmt, rec);

        // Compare member sets (order-independent).
        if (rec.members.size() != members.size()) continue;
        std::vector<std::string> sortedA = rec.members;
        std::vector<std::string> sortedB = members;
        std::sort(sortedA.begin(), sortedA.end());
        std::sort(sortedB.begin(), sortedB.end());
        if (sortedA == sortedB) {
            out = rec;
            found = true;
            break;
        }
    }
    sqlite3_finalize(stmt);
    return found;
}

bool UnifiedSessionRepo::loadByRootMessageId(const std::string& account,
                                             const std::string& rootMessageId,
                                             UnifiedSession& out) {
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db) return false;

    const char* sql = "SELECT session_id, account, subject, mode, members, "
                      "signal_session_id, mls_group_id, root_message_id, status, "
                      "created_at, updated_at "
                      "FROM unified_session WHERE account=? AND root_message_id=? AND status=0 "
                      "LIMIT 1;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, account.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, rootMessageId.c_str(), -1, SQLITE_TRANSIENT);

    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        fillRecord(stmt, out);
        found = true;
    }
    sqlite3_finalize(stmt);
    return found;
}

bool UnifiedSessionRepo::upgradeToMls(const std::string& sessionId,
                                       const std::string& mlsGroupId,
                                       const std::vector<std::string>& newMembers) {
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db) return false;

    const char* sql = "UPDATE unified_session SET mode='mls', mls_group_id=?, "
                      "members=?, updated_at=datetime('now','localtime') "
                      "WHERE session_id=?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, mlsGroupId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, membersToJson(newMembers).c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, sessionId.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        LOG_INFO("[USRepo] upgradeToMls error: %s\n", sqlite3_errmsg(db));
        return false;
    }

    LOG_INFO("[USRepo] upgraded session=%s to mls, group=%s\n",
             sessionId.c_str(), mlsGroupId.c_str());
    return true;
}

bool UnifiedSessionRepo::updateMembers(const std::string& sessionId,
                                        const std::vector<std::string>& members) {
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db) return false;

    const char* sql = "UPDATE unified_session SET members=?, "
                      "updated_at=datetime('now','localtime') "
                      "WHERE session_id=?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, membersToJson(members).c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, sessionId.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool UnifiedSessionRepo::updateSignalSessionId(const std::string& sessionId,
                                                const std::string& signalSessionId) {
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db) return false;

    const char* sql = "UPDATE unified_session SET signal_session_id=?, "
                      "updated_at=datetime('now','localtime') "
                      "WHERE session_id=?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, signalSessionId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, sessionId.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

std::vector<UnifiedSession> UnifiedSessionRepo::listByAccount(const std::string& account) {
    std::vector<UnifiedSession> result;
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db) return result;

    const char* sql = "SELECT session_id, account, subject, mode, members, "
                      "signal_session_id, mls_group_id, root_message_id, status, "
                      "created_at, updated_at "
                      "FROM unified_session WHERE account=? AND status=0 "
                      "ORDER BY updated_at DESC;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return result;

    sqlite3_bind_text(stmt, 1, account.c_str(), -1, SQLITE_TRANSIENT);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        UnifiedSession rec;
        fillRecord(stmt, rec);
        result.push_back(rec);
    }
    sqlite3_finalize(stmt);
    return result;
}

bool UnifiedSessionRepo::close(const std::string& sessionId) {
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db) return false;

    const char* sql = "UPDATE unified_session SET status=1, "
                      "updated_at=datetime('now','localtime') "
                      "WHERE session_id=?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, sessionId.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}
