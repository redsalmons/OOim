#include "group_session_repo.h"
#include "db_connection.h"
#include "logger.h"
#include <sqlite3.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

static std::string membersToJson(const std::vector<std::string>& members) {
    return json(members).dump();
}

static std::vector<std::string> jsonToMembers(const std::string& s) {
    std::vector<std::string> result;
    if (s.empty()) return result;
    try {
        auto arr = json::parse(s);
        if (arr.is_array()) {
            for (auto& m : arr) {
                result.push_back(m.get<std::string>());
            }
        }
    } catch (...) {}
    return result;
}

// Column order: group_id, group_email, x_reply_id, x_session_id, subject, members, owner, account, epoch, status, created_at, updated_at
static void fillRecord(sqlite3_stmt* stmt, GroupSessionRecord& out) {
    out.groupId = std::to_string(sqlite3_column_int64(stmt, 0));
    out.groupEmail = sqlite3_column_text(stmt, 1) ? (const char*)sqlite3_column_text(stmt, 1) : "";
    out.xReplyId = sqlite3_column_text(stmt, 2) ? (const char*)sqlite3_column_text(stmt, 2) : "";
    out.xSessionId = sqlite3_column_text(stmt, 3) ? (const char*)sqlite3_column_text(stmt, 3) : "";
    out.subject = sqlite3_column_text(stmt, 4) ? (const char*)sqlite3_column_text(stmt, 4) : "";
    out.members = jsonToMembers(sqlite3_column_text(stmt, 5) ? (const char*)sqlite3_column_text(stmt, 5) : "");
    out.owner = sqlite3_column_text(stmt, 6) ? (const char*)sqlite3_column_text(stmt, 6) : "";
    out.account = sqlite3_column_text(stmt, 7) ? (const char*)sqlite3_column_text(stmt, 7) : "";
    out.epoch = sqlite3_column_int(stmt, 8);
    out.status = sqlite3_column_int(stmt, 9);
    out.createdAt = sqlite3_column_text(stmt, 10) ? (const char*)sqlite3_column_text(stmt, 10) : "";
    out.updatedAt = sqlite3_column_text(stmt, 11) ? (const char*)sqlite3_column_text(stmt, 11) : "";
}

bool GroupSessionRepo::createGroup(GroupSessionRecord& rec) {
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db) return false;

    bool explicitId = !rec.groupId.empty();
    int64_t gid = 0;

    if (explicitId) {
        try { gid = std::stoll(rec.groupId); } catch (...) { return false; }
        if (rec.groupEmail.empty()) rec.groupEmail = "group_" + rec.groupId + "@oim";

        const char* sql = "INSERT OR IGNORE INTO group_session "
                          "(group_id, group_email, x_reply_id, x_session_id, subject, members, owner, account, epoch, status, created_at, updated_at) "
                          "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, datetime('now','localtime'), datetime('now','localtime'));";
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;
        sqlite3_bind_int64(stmt, 1, gid);
        sqlite3_bind_text(stmt, 2, rec.groupEmail.c_str(), -1, SQLITE_TRANSIENT);
        if (rec.xReplyId.empty()) {
            sqlite3_bind_null(stmt, 3);
        } else {
            sqlite3_bind_text(stmt, 3, rec.xReplyId.c_str(), -1, SQLITE_TRANSIENT);
        }
        if (rec.xSessionId.empty()) {
            sqlite3_bind_null(stmt, 4);
        } else {
            sqlite3_bind_text(stmt, 4, rec.xSessionId.c_str(), -1, SQLITE_TRANSIENT);
        }
        sqlite3_bind_text(stmt, 5, rec.subject.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 6, membersToJson(rec.members).c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 7, rec.owner.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 8, rec.account.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 9, rec.epoch);
        sqlite3_bind_int(stmt, 10, rec.status);
        sqlite3_step(stmt); // ignore conflict; if exists, we just keep it
        sqlite3_finalize(stmt);
    } else {
        const char* sql = "INSERT INTO group_session "
                          "(group_email, x_reply_id, x_session_id, subject, members, owner, account, epoch, status, created_at, updated_at) "
                          "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, datetime('now','localtime'), datetime('now','localtime'));";
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;
        sqlite3_bind_text(stmt, 1, "", -1, SQLITE_TRANSIENT);
        if (rec.xReplyId.empty()) {
            sqlite3_bind_null(stmt, 2);
        } else {
            sqlite3_bind_text(stmt, 2, rec.xReplyId.c_str(), -1, SQLITE_TRANSIENT);
        }
        if (rec.xSessionId.empty()) {
            sqlite3_bind_null(stmt, 3);
        } else {
            sqlite3_bind_text(stmt, 3, rec.xSessionId.c_str(), -1, SQLITE_TRANSIENT);
        }
        sqlite3_bind_text(stmt, 4, rec.subject.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 5, membersToJson(rec.members).c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 6, rec.owner.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 7, rec.account.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 8, rec.epoch);
        sqlite3_bind_int(stmt, 9, rec.status);
        bool ok = sqlite3_step(stmt) == SQLITE_DONE;
        sqlite3_finalize(stmt);
        if (!ok) return false;
        gid = sqlite3_last_insert_rowid(db);
        rec.groupId = std::to_string(gid);
    }

    // Load/refresh group_email from DB
    const char* loadSql = "SELECT group_email FROM group_session WHERE group_id=? LIMIT 1;";
    sqlite3_stmt* lstmt;
    if (sqlite3_prepare_v2(db, loadSql, -1, &lstmt, NULL) != SQLITE_OK) return false;
    sqlite3_bind_int64(lstmt, 1, gid);
    bool ok = false;
    if (sqlite3_step(lstmt) == SQLITE_ROW) {
        rec.groupEmail = sqlite3_column_text(lstmt, 0) ? (const char*)sqlite3_column_text(lstmt, 0) : "";
        ok = true;
    }
    sqlite3_finalize(lstmt);
    return ok;
}

bool GroupSessionRepo::loadGroup(const std::string& groupId, GroupSessionRecord& out) {
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db) return false;

    int64_t gid = 0;
    try { gid = std::stoll(groupId); } catch (...) { return false; }

    const char* sql = "SELECT group_id, group_email, x_reply_id, x_session_id, subject, members, owner, account, epoch, status, created_at, updated_at "
                      "FROM group_session WHERE group_id=? LIMIT 1;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;

    sqlite3_bind_int64(stmt, 1, gid);

    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        fillRecord(stmt, out);
        found = true;
    }
    sqlite3_finalize(stmt);
    return found;
}

bool GroupSessionRepo::loadByXReplyId(const std::string& xReplyId, const std::string& account, GroupSessionRecord& out) {
    if (xReplyId.empty() || account.empty()) return false;
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db) return false;

    const char* sql = "SELECT group_id, group_email, x_reply_id, x_session_id, subject, members, owner, account, epoch, status, created_at, updated_at "
                      "FROM group_session WHERE x_reply_id=? AND account=? LIMIT 1;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, xReplyId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, account.c_str(), -1, SQLITE_TRANSIENT);

    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        fillRecord(stmt, out);
        found = true;
    }
    sqlite3_finalize(stmt);
    return found;
}

bool GroupSessionRepo::loadBySessionId(const std::string& xSessionId, const std::string& account, GroupSessionRecord& out) {
    if (xSessionId.empty() || account.empty()) return false;
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db) return false;

    const char* sql = "SELECT group_id, group_email, x_reply_id, x_session_id, subject, members, owner, account, epoch, status, created_at, updated_at "
                      "FROM group_session WHERE x_session_id=? AND account=? LIMIT 1;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, xSessionId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, account.c_str(), -1, SQLITE_TRANSIENT);

    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        fillRecord(stmt, out);
        found = true;
    }
    sqlite3_finalize(stmt);
    return found;
}

bool GroupSessionRepo::setXReplyId(const std::string& groupId, const std::string& xReplyId) {
    if (xReplyId.empty() || groupId.empty()) return false;
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db) return false;

    int64_t gid = 0;
    try { gid = std::stoll(groupId); } catch (...) { return false; }

    const char* sql = "UPDATE group_session SET x_reply_id=? WHERE group_id=?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, xReplyId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, gid);

    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

bool GroupSessionRepo::updateMembers(const std::string& groupId, const std::vector<std::string>& members) {
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db) return false;

    int64_t gid = 0;
    try { gid = std::stoll(groupId); } catch (...) { return false; }

    const char* sql = "UPDATE group_session SET members=?, updated_at=datetime('now','localtime') WHERE group_id=?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, membersToJson(members).c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, gid);

    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

bool GroupSessionRepo::addMember(const std::string& groupId, const std::string& memberEmail) {
    GroupSessionRecord rec;
    if (!loadGroup(groupId, rec)) return false;
    for (auto& m : rec.members) {
        if (m == memberEmail) return true;  // already a member
    }
    rec.members.push_back(memberEmail);
    return updateMembers(groupId, rec.members);
}

bool GroupSessionRepo::removeMember(const std::string& groupId, const std::string& memberEmail) {
    GroupSessionRecord rec;
    if (!loadGroup(groupId, rec)) return false;
    std::vector<std::string> newMembers;
    for (auto& m : rec.members) {
        if (m != memberEmail) newMembers.push_back(m);
    }
    return updateMembers(groupId, newMembers);
}

bool GroupSessionRepo::incrementEpoch(const std::string& groupId) {
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db) return false;

    int64_t gid = 0;
    try { gid = std::stoll(groupId); } catch (...) { return false; }

    const char* sql = "UPDATE group_session SET epoch=epoch+1, updated_at=datetime('now','localtime') WHERE group_id=?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;

    sqlite3_bind_int64(stmt, 1, gid);

    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

bool GroupSessionRepo::closeGroup(const std::string& groupId) {
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db) return false;

    int64_t gid = 0;
    try { gid = std::stoll(groupId); } catch (...) { return false; }

    const char* sql = "UPDATE group_session SET status=1, updated_at=datetime('now','localtime') WHERE group_id=?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;

    sqlite3_bind_int64(stmt, 1, gid);

    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

std::vector<GroupSessionRecord> GroupSessionRepo::listGroups(const std::string& accountEmail) {
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    std::vector<GroupSessionRecord> result;
    if (!db) return result;

    const char* sql = "SELECT group_id, group_email, x_reply_id, x_session_id, subject, members, owner, account, epoch, status, created_at, updated_at "
                      "FROM group_session WHERE account=? AND status=0;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return result;
    sqlite3_bind_text(stmt, 1, accountEmail.c_str(), -1, SQLITE_TRANSIENT);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        GroupSessionRecord rec;
        fillRecord(stmt, rec);
        result.push_back(rec);
    }
    sqlite3_finalize(stmt);
    return result;
}

bool GroupSessionRepo::isMember(const std::string& groupId, const std::string& memberEmail) {
    GroupSessionRecord rec;
    if (!loadGroup(groupId, rec)) return false;
    for (auto& m : rec.members) {
        if (m == memberEmail) return true;
    }
    return false;
}
