#include "email_repo.h"
#include "db_connection.h"
#include "logger.h"
#include <sqlite3.h>
#include <cstring>

static EmailRecord readEmailRecord(sqlite3_stmt* stmt, bool hasSessionIdAt13, bool hasRowidAt16, bool hasVisibleLast) {
    EmailRecord r;
    r.uuid = (const char*)sqlite3_column_text(stmt, 0);
    r.account = (const char*)sqlite3_column_text(stmt, 1);
    r.sender = sqlite3_column_text(stmt, 2) ? (const char*)sqlite3_column_text(stmt, 2) : "";
    r.fromAddr = sqlite3_column_text(stmt, 3) ? (const char*)sqlite3_column_text(stmt, 3) : "";
    r.subject = sqlite3_column_text(stmt, 4) ? (const char*)sqlite3_column_text(stmt, 4) : "";
    r.date = sqlite3_column_text(stmt, 5) ? (const char*)sqlite3_column_text(stmt, 5) : "";
    r.bodystructure = sqlite3_column_text(stmt, 6) ? (const char*)sqlite3_column_text(stmt, 6) : "";
    r.replyTo = sqlite3_column_text(stmt, 7) ? (const char*)sqlite3_column_text(stmt, 7) : "";
    r.inReplyTo = sqlite3_column_text(stmt, 8) ? (const char*)sqlite3_column_text(stmt, 8) : "";
    r.messageId = sqlite3_column_text(stmt, 9) ? (const char*)sqlite3_column_text(stmt, 9) : "";
    r.flags = sqlite3_column_text(stmt, 10) ? (const char*)sqlite3_column_text(stmt, 10) : "";
    r.folder = sqlite3_column_text(stmt, 11) ? (const char*)sqlite3_column_text(stmt, 11) : "INBOX";
    r.isLocal = sqlite3_column_int(stmt, 12);
    if (hasSessionIdAt13) {
        r.sessionId = sqlite3_column_text(stmt, 13) ? (const char*)sqlite3_column_text(stmt, 13) : "";
        r.servicerecvtime = sqlite3_column_text(stmt, 14) ? (const char*)sqlite3_column_text(stmt, 14) : "";
        if (hasRowidAt16) {
            r.toAddr = sqlite3_column_text(stmt, 15) ? (const char*)sqlite3_column_text(stmt, 15) : "";
            r.rowid = sqlite3_column_int64(stmt, 16);
            r.file = sqlite3_column_text(stmt, 17) ? (const char*)sqlite3_column_text(stmt, 17) : "";
        } else {
            r.rowid = sqlite3_column_int64(stmt, 15);
            r.toAddr = sqlite3_column_text(stmt, 16) ? (const char*)sqlite3_column_text(stmt, 16) : "";
            r.file = sqlite3_column_text(stmt, 17) ? (const char*)sqlite3_column_text(stmt, 17) : "";
        }
        if (hasVisibleLast) {
            r.visible = sqlite3_column_int(stmt, 18);
        }
    }
    return r;
}

std::vector<EmailRecord> EmailRepo::queryByAccount(const std::string& account) {
    std::vector<EmailRecord> result;
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db) return result;

    const char* sql =
        "SELECT l.uuid, l.account, l.sender, l.from_addr, l.subject, l.date, l.bodystructure, "
        "l.reply_to, l.in_reply_to, l.message_id, l.flags, l.folder, l.islocal, s.session_id, "
        "l.servicerecvtime, l.id, l.to_addr, l.file, l.visible "
        "FROM localemail l LEFT JOIN session s ON l.id = s.email_id "
        "WHERE l.account = ? ORDER BY l.id DESC;";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return result;
    sqlite3_bind_text(stmt, 1, account.c_str(), -1, SQLITE_TRANSIENT);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        result.push_back(readEmailRecord(stmt, true, false, true));
    }
    sqlite3_finalize(stmt);
    return result;
}

std::vector<EmailRecord> EmailRepo::queryThreadRoots(const std::string& account) {
    std::vector<EmailRecord> result;
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db) return result;

    const char* sql =
        "SELECT l.uuid, l.account, l.sender, l.from_addr, l.subject, l.date, l.bodystructure, "
        "l.reply_to, l.in_reply_to, l.message_id, l.flags, l.folder, l.islocal, s.session_id, "
        "l.servicerecvtime, l.to_addr, l.id, l.file "
        "FROM localemail l "
        "INNER JOIN session s ON l.id = s.email_id "
        "WHERE l.account = ? AND s.visible = 1 AND l.visible = 1 "
        "AND l.id = (SELECT MIN(email_id) FROM session WHERE session_id = s.session_id AND email_id > 0) "
        "ORDER BY l.id DESC;";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return result;
    sqlite3_bind_text(stmt, 1, account.c_str(), -1, SQLITE_TRANSIENT);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        result.push_back(readEmailRecord(stmt, true, true, false));
    }
    sqlite3_finalize(stmt);
    return result;
}

std::vector<EmailRecord> EmailRepo::queryThread(const std::string& sessionId) {
    std::vector<EmailRecord> result;
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db) return result;

    const char* sql =
        "SELECT l.uuid, l.account, l.sender, l.from_addr, l.subject, l.date, l.bodystructure, "
        "l.reply_to, l.in_reply_to, l.message_id, l.flags, l.folder, l.islocal, s.session_id, "
        "l.servicerecvtime, l.to_addr, l.id, l.file "
        "FROM localemail l "
        "INNER JOIN session s ON l.id = s.email_id "
        "WHERE s.session_id = ? AND s.visible = 1 AND l.visible = 1 "
        "ORDER BY l.id ASC;";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return result;
    sqlite3_bind_text(stmt, 1, sessionId.c_str(), -1, SQLITE_TRANSIENT);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        result.push_back(readEmailRecord(stmt, true, true, false));
    }
    sqlite3_finalize(stmt);
    return result;
}

std::string EmailRepo::findUuidByMessageId(const std::string& messageId, const std::string& account) {
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db || messageId.empty()) return "";

    const char* sql = "SELECT uuid FROM localemail WHERE message_id = ? AND account = ? LIMIT 1;";
    sqlite3_stmt* stmt;
    std::string result;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, messageId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, account.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* val = (const char*)sqlite3_column_text(stmt, 0);
            if (val) result = val;
        }
        sqlite3_finalize(stmt);
    }
    return result;
}

int64_t EmailRepo::findRowidByUuid(const std::string& uuid) {
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db) return 0;

    const char* sql = "SELECT id FROM localemail WHERE uuid = ? LIMIT 1;";
    sqlite3_stmt* stmt;
    int64_t result = 0;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, uuid.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            result = sqlite3_column_int64(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }
    return result;
}

int64_t EmailRepo::findRowidByUuidAndAccount(const std::string& uuid, const std::string& account) {
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db) return 0;

    const char* sql = "SELECT id FROM localemail WHERE uuid = ? AND account = ? ORDER BY id DESC LIMIT 1;";
    sqlite3_stmt* stmt;
    int64_t result = 0;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, uuid.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, account.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            result = sqlite3_column_int64(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }
    return result;
}

int64_t EmailRepo::findIdByMessageId(const std::string& messageId, const std::string& account) {
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db || messageId.empty()) return 0;

    const char* sql = "SELECT id FROM localemail WHERE message_id = ? AND account = ? LIMIT 1;";
    sqlite3_stmt* stmt;
    int64_t result = 0;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, messageId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, account.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            result = sqlite3_column_int64(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }
    return result;
}

int64_t EmailRepo::findSentByInReplyTo(const std::string& inReplyTo, const std::string& account) {
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db || inReplyTo.empty()) return 0;

    const char* sql = "SELECT id FROM localemail WHERE in_reply_to = ? AND account = ? AND uuid = '0' LIMIT 1;";
    sqlite3_stmt* stmt;
    int64_t result = 0;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, inReplyTo.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, account.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            result = sqlite3_column_int64(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }
    return result;
}

int64_t EmailRepo::insert(const EmailRecord& rec) {
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db) return 0;

    const char* sql =
        "INSERT INTO localemail "
        "(uuid, account, sender, from_addr, subject, date, bodystructure, reply_to, "
        "in_reply_to, message_id, flags, folder, islocal, servicerecvtime, to_addr, file, visible) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, '', ?);";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return 0;

    sqlite3_bind_text(stmt, 1, rec.uuid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, rec.account.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, rec.sender.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, rec.fromAddr.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, rec.subject.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, rec.date.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, rec.bodystructure.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 8, rec.replyTo.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 9, rec.inReplyTo.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 10, rec.messageId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 11, rec.flags.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 12, rec.folder.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 13, rec.isLocal);
    sqlite3_bind_text(stmt, 14, rec.servicerecvtime.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 15, rec.toAddr.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 16, rec.visible);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) return 0;
    return sqlite3_last_insert_rowid(db);
}

// Migration: Update islocal=1 for emails that already have file downloaded
// This should be called once after schema update
bool EmailRepo::migrateIslocalForNoSessionChart() {
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db) return false;

    // For emails that were already downloaded (file not empty), set islocal=1
    const char* sql1 = "UPDATE localemail SET islocal = 1 WHERE file IS NOT NULL AND file != '' AND islocal = 0;";
    char* err_msg = NULL;
    int rc = sqlite3_exec(db, sql1, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        LOG_INFO("Migration error (islocal=1): %s\n", err_msg);
        sqlite3_free(err_msg);
    }

    // For emails with no file and islocal=0, they will be re-fetched with the new logic
    return rc == SQLITE_OK;
}

bool EmailRepo::updateById(int64_t id, const EmailRecord& rec) {
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db) return false;

    const char* sql =
        "UPDATE localemail SET uuid = ?, sender = ?, from_addr = ?, subject = ?, "
        "date = ?, bodystructure = ?, reply_to = ?, in_reply_to = ?, flags = ?, "
        "folder = ?, servicerecvtime = ?, to_addr = ?, islocal = 1 WHERE id = ?;";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, rec.uuid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, rec.sender.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, rec.fromAddr.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, rec.subject.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, rec.date.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, rec.bodystructure.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, rec.replyTo.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 8, rec.inReplyTo.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 9, rec.flags.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 10, rec.folder.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 11, rec.servicerecvtime.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 12, rec.toAddr.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 13, id);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

int64_t EmailRepo::insertSentEmail(const std::string& account, const std::string& sender,
    const std::string& fromAddr, const std::string& toAddr,
    const std::string& subject, const std::string& date,
    const std::string& messageId, const std::string& inReplyTo,
    const std::string& bodystructure, const std::string& file) {

    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db) return 0;

    const char* sql =
        "INSERT INTO localemail "
        "(uuid, account, sender, from_addr, to_addr, subject, date, bodystructure, "
        "reply_to, in_reply_to, message_id, flags, folder, islocal, servicerecvtime, file) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, '[]', 'INBOX', 0, ?, ?);";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return 0;

    sqlite3_bind_text(stmt, 1, "0", -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, account.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, sender.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, fromAddr.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, toAddr.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, subject.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, date.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 8, bodystructure.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 9, inReplyTo.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 10, inReplyTo.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 11, messageId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 12, date.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 13, file.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) return 0;
    return sqlite3_last_insert_rowid(db);
}

bool EmailRepo::updateAfterDownload(const std::string& uuid, const std::string& account,
    const std::string& messageId, const std::string& inReplyTo,
    const std::string& file) {

    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db) return false;

    const char* sql =
        "UPDATE localemail SET islocal = 2, message_id = ?, in_reply_to = ?, file = ? "
        "WHERE uuid = ? AND account = ?;";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, messageId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, inReplyTo.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, file.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, uuid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, account.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool EmailRepo::setIslocal(const std::string& uuid, const std::string& account, int islocal) {
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db) return false;

    const char* sql = "UPDATE localemail SET islocal = ? WHERE uuid = ? AND account = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;
    sqlite3_bind_int(stmt, 1, islocal);
    sqlite3_bind_text(stmt, 2, uuid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, account.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool EmailRepo::incrementRetryCount(const std::string& uuid, const std::string& account) {
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db) return false;

    const char* sql =
        "UPDATE localemail SET retry_count = COALESCE(retry_count, 0) + 1 "
        "WHERE uuid = ? AND account = ?;";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, uuid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, account.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

int EmailRepo::countPendingBodies(const std::string& account) {
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db) return -1;

    const char* sql =
        "SELECT COUNT(*) FROM localemail WHERE account = ? AND islocal IN (0, 1) "
        "AND uuid != '0' AND (retry_count IS NULL OR retry_count < 3);";

    sqlite3_stmt* stmt;
    int count = 0;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, account.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            count = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }
    return count;
}

std::vector<PendingEmail> EmailRepo::queryPendingEmails(const std::string& account, int limit) {
    std::vector<PendingEmail> result;
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db) return result;

    const char* sql =
        "SELECT uuid, folder, islocal FROM localemail WHERE account = ? AND islocal IN (0, 1) "
        "AND uuid != '0' AND (retry_count IS NULL OR retry_count < 3) "
        "ORDER BY islocal DESC, uuid ASC LIMIT ?;";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return result;
    sqlite3_bind_text(stmt, 1, account.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, limit);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        PendingEmail pe;
        pe.uuid = (const char*)sqlite3_column_text(stmt, 0);
        pe.folder = sqlite3_column_text(stmt, 1) ? (const char*)sqlite3_column_text(stmt, 1) : "INBOX";
        pe.islocal = sqlite3_column_int(stmt, 2);
        result.push_back(pe);
    }
    sqlite3_finalize(stmt);
    return result;
}

std::string EmailRepo::getMaxUid(const std::string& account, const std::string& folder) {
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db) return "";

    const char* sql = "SELECT MAX(uuid) FROM localemail WHERE account = ? AND folder = ?;";
    sqlite3_stmt* stmt;
    std::string result;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, account.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, folder.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* val = (const char*)sqlite3_column_text(stmt, 0);
            if (val) result = val;
        }
        sqlite3_finalize(stmt);
    }
    return result;
}
