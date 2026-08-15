#include "email_core_common.h"
#include "email_core.h"
#include "logger.h"
#include <stdio.h>
#include <string.h>
#include <sqlite3.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

int email_db_init(const char* path) {
    if (g_db != NULL) {
        char* err = NULL;

        const char* sql_contact = "CREATE TABLE IF NOT EXISTS contact ("
                                  "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                                  "email TEXT NOT NULL,"
                                  "name TEXT NOT NULL,"
                                  "categories TEXT,"
                                  "notes TEXT,"
                                  "key TEXT"
                                  ");";
        sqlite3_exec(g_db, sql_contact, NULL, NULL, &err);
        if (err) sqlite3_free(err);
        sqlite3_exec(g_db, "CREATE INDEX IF NOT EXISTS idx_contact_email ON contact(email);", NULL, NULL, &err);
        if (err) sqlite3_free(err);

        sqlite3_exec(g_db, "ALTER TABLE session ADD COLUMN encrypt_method INTEGER DEFAULT 0;", NULL, NULL, &err);
        if (err) sqlite3_free(err);

        const char* sql_keyinfo = "CREATE TABLE IF NOT EXISTS keyinfo ("
                                  "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                                  "pub TEXT,"
                                  "key TEXT,"
                                  "password TEXT,"
                                  "session_id INTEGER,"
                                  "FOREIGN KEY(session_id) REFERENCES session(id)"
                                  ");";
        sqlite3_exec(g_db, sql_keyinfo, NULL, NULL, &err);
        if (err) sqlite3_free(err);
        sqlite3_exec(g_db, "CREATE INDEX IF NOT EXISTS idx_keyinfo_session_id ON keyinfo(session_id);", NULL, NULL, &err);
        if (err) sqlite3_free(err);

        const char* sql_code = "CREATE TABLE IF NOT EXISTS code ("
                               "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                               "account TEXT NOT NULL,"
                               "pubkey TEXT,"
                               "secretkey TEXT,"
                               "identify TEXT"
                               ");";
        sqlite3_exec(g_db, sql_code, NULL, NULL, &err);
        if (err) sqlite3_free(err);
        sqlite3_exec(g_db, "CREATE INDEX IF NOT EXISTS idx_code_account ON code(account);", NULL, NULL, &err);
        if (err) sqlite3_free(err);
        sqlite3_exec(g_db, "CREATE INDEX IF NOT EXISTS idx_code_identify ON code(identify);", NULL, NULL, &err);
        if (err) sqlite3_free(err);

        return 0;
    }

    int rc = sqlite3_open_v2(path, &g_db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, NULL);
    if (rc != SQLITE_OK) {
        LOG_INFO("Cannot open database: %s\n", sqlite3_errmsg(g_db));
        return -1;
    }

    sqlite3_exec(g_db, "PRAGMA journal_mode=WAL;", NULL, NULL, NULL);
    sqlite3_exec(g_db, "PRAGMA busy_timeout=5000;", NULL, NULL, NULL);

    char* err_msg = NULL;

    const char* sql_localemail = "CREATE TABLE IF NOT EXISTS localemail ("
                                 "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                                 "uuid TEXT,"
                                 "account TEXT NOT NULL,"
                                 "sender TEXT,"
                                 "from_addr TEXT,"
                                 "to_addr TEXT,"
                                 "subject TEXT,"
                                 "date TEXT,"
                                 "bodystructure TEXT,"
                                 "reply_to TEXT,"
                                 "in_reply_to TEXT,"
                                 "message_id TEXT,"
                                 "flags TEXT,"
                                 "folder TEXT,"
                                 "islocal INTEGER DEFAULT 0,"
                                 "servicerecvtime TEXT"
                                 ");";
    rc = sqlite3_exec(g_db, sql_localemail, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        LOG_INFO("SQL error (localemail): %s\n", err_msg);
        sqlite3_free(err_msg);
        sqlite3_close(g_db);
        g_db = NULL;
        return -3;
    }

    sqlite3_exec(g_db, "ALTER TABLE localemail ADD COLUMN reply_to TEXT;", NULL, NULL, &err_msg);
    sqlite3_exec(g_db, "ALTER TABLE localemail ADD COLUMN to_addr TEXT;", NULL, NULL, &err_msg);
    sqlite3_exec(g_db, "ALTER TABLE localemail ADD COLUMN in_reply_to TEXT;", NULL, NULL, &err_msg);
    sqlite3_exec(g_db, "ALTER TABLE localemail ADD COLUMN message_id TEXT;", NULL, NULL, &err_msg);
    sqlite3_exec(g_db, "ALTER TABLE localemail ADD COLUMN folder TEXT;", NULL, NULL, &err_msg);
    sqlite3_exec(g_db, "ALTER TABLE localemail ADD COLUMN flags TEXT;", NULL, NULL, &err_msg);
    sqlite3_exec(g_db, "ALTER TABLE localemail ADD COLUMN file TEXT;", NULL, NULL, &err_msg);

    const char* sql_session = "CREATE TABLE IF NOT EXISTS session ("
                              "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                              "session_id TEXT NOT NULL,"
                              "email_id INTEGER,"
                              "visible INTEGER DEFAULT 1,"
                              "auto INTEGER DEFAULT 1,"
                              "isread INTEGER DEFAULT 0,"
                              "encrypt_method INTEGER DEFAULT 0"
                              ");";
    rc = sqlite3_exec(g_db, sql_session, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        LOG_INFO("SQL error (session): %s\n", err_msg);
        sqlite3_free(err_msg);
        sqlite3_close(g_db);
        g_db = NULL;
        return -3;
    }

    sqlite3_exec(g_db, "CREATE INDEX IF NOT EXISTS idx_session_id ON session(session_id);", NULL, NULL, &err_msg);
    sqlite3_exec(g_db, "ALTER TABLE session ADD COLUMN email_id INTEGER;", NULL, NULL, &err_msg);
    if (err_msg) { sqlite3_free(err_msg); err_msg = NULL; }
    sqlite3_exec(g_db, "ALTER TABLE session ADD COLUMN isread INTEGER DEFAULT 0;", NULL, NULL, &err_msg);
    if (err_msg) { sqlite3_free(err_msg); err_msg = NULL; }
    sqlite3_exec(g_db, "DELETE FROM session WHERE id NOT IN (SELECT MIN(id) FROM session GROUP BY email_id);", NULL, NULL, &err_msg);
    if (err_msg) sqlite3_free(err_msg);
    sqlite3_exec(g_db, "CREATE UNIQUE INDEX IF NOT EXISTS idx_session_email_id ON session(email_id);", NULL, NULL, &err_msg);
    if (err_msg) { LOG_INFO("SQL warning (session unique index): %s\n", err_msg); sqlite3_free(err_msg); }

    sqlite3_exec(g_db, "ALTER TABLE localemail ADD COLUMN islocal INTEGER DEFAULT 0;", NULL, NULL, &err_msg);
    sqlite3_exec(g_db, "ALTER TABLE localemail ADD COLUMN retry_count INTEGER DEFAULT 0;", NULL, NULL, &err_msg);
    sqlite3_exec(g_db, "ALTER TABLE localemail ADD COLUMN servicerecvtime TEXT;", NULL, NULL, &err_msg);

    const char* sql_contact = "CREATE TABLE IF NOT EXISTS contact ("
                              "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                              "email TEXT NOT NULL,"
                              "name TEXT NOT NULL,"
                              "categories TEXT,"
                              "notes TEXT,"
                              "key TEXT"
                              ");";
    rc = sqlite3_exec(g_db, sql_contact, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        LOG_INFO("SQL error (contact): %s\n", err_msg);
        sqlite3_free(err_msg);
        sqlite3_close(g_db);
        g_db = NULL;
        return -4;
    }

    sqlite3_exec(g_db, "CREATE INDEX IF NOT EXISTS idx_contact_email ON contact(email);", NULL, NULL, &err_msg);
    if (err_msg) sqlite3_free(err_msg);

    sqlite3_exec(g_db, "ALTER TABLE session ADD COLUMN encrypt_method INTEGER DEFAULT 0;", NULL, NULL, &err_msg);
    if (err_msg) sqlite3_free(err_msg);

    sqlite3_exec(g_db, "ALTER TABLE keyinfo ADD COLUMN account TEXT DEFAULT '';", NULL, NULL, &err_msg);
    if (err_msg) sqlite3_free(err_msg);

    const char* sql_keyinfo = "CREATE TABLE IF NOT EXISTS keyinfo ("
                              "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                              "pub TEXT,"
                              "key TEXT,"
                              "password TEXT,"
                              "session_id INTEGER,"
                              "account TEXT DEFAULT '',"
                              "FOREIGN KEY(session_id) REFERENCES session(id)"
                              ");";
    rc = sqlite3_exec(g_db, sql_keyinfo, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        LOG_INFO("SQL error (keyinfo): %s\n", err_msg);
        sqlite3_free(err_msg);
    } else {
        sqlite3_exec(g_db, "CREATE INDEX IF NOT EXISTS idx_keyinfo_session_id ON keyinfo(session_id);", NULL, NULL, &err_msg);
        if (err_msg) sqlite3_free(err_msg);
    }

    const char* sql_code = "CREATE TABLE IF NOT EXISTS code ("
                           "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                           "account TEXT NOT NULL,"
                           "pubkey TEXT,"
                           "secretkey TEXT,"
                           "identify TEXT"
                           ");";
    rc = sqlite3_exec(g_db, sql_code, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        LOG_INFO("SQL error (code): %s\n", err_msg);
        sqlite3_free(err_msg);
    } else {
        sqlite3_exec(g_db, "CREATE INDEX IF NOT EXISTS idx_code_account ON code(account);", NULL, NULL, &err_msg);
        if (err_msg) sqlite3_free(err_msg);
        sqlite3_exec(g_db, "CREATE INDEX IF NOT EXISTS idx_code_identify ON code(identify);", NULL, NULL, &err_msg);
        if (err_msg) sqlite3_free(err_msg);
        sqlite3_exec(g_db, "ALTER TABLE code ADD COLUMN keypassword TEXT;", NULL, NULL, &err_msg);
        if (err_msg) { sqlite3_free(err_msg); err_msg = NULL; }
    }

    return 0;
}

void email_db_close(void) {
    if (g_db != NULL) {
        sqlite3_close(g_db);
        g_db = NULL;
    }
}

int email_db_insert(const char* email, const char* sender, const char* recipient,
                    const char* subject, const char* body, const char* timestamp) {
    if (!g_db) return -1;
    return 0;
}

int email_db_query(const char* email, EmailHandler* handler) {
    if (!g_db || !email || !handler) return -1;
    return 0;
}

int email_query_localemail(const char* account, char* outJson, int outSize) {
    if (!g_db) {
        if (outJson && outSize > 0) {
            snprintf(outJson, outSize, R"({"status":"failed","error":"database_not_initialized"})");
        }
        return -1;
    }

    LOG_INFO("[DB] email_query_localemail called with account: '%s'\n", account ? account : "null");

    const char* sql = "SELECT l.uuid, l.account, l.sender, l.from_addr, l.subject, l.date, l.bodystructure, l.reply_to, l.in_reply_to, l.message_id, l.flags, l.folder, l.islocal, s.session_id, l.servicerecvtime, l.id, l.to_addr, l.file "
                      "FROM localemail l LEFT JOIN session s ON l.id = s.email_id WHERE l.account = ? ORDER BY l.id DESC;";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        LOG_INFO("[DB] prepare failed: %s\n", sqlite3_errmsg(g_db));
        if (outJson && outSize > 0) {
            snprintf(outJson, outSize, R"({"status":"failed","error":"prepare_failed"})");
        }
        return -2;
    }

    sqlite3_bind_text(stmt, 1, account ? account : "", -1, SQLITE_STATIC);

    json emails_array = json::array();
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        json email_obj;
        email_obj["uuid"] = (const char*)sqlite3_column_text(stmt, 0);
        email_obj["account"] = (const char*)sqlite3_column_text(stmt, 1);
        email_obj["sender"] = sqlite3_column_text(stmt, 2) ? (const char*)sqlite3_column_text(stmt, 2) : "";
        email_obj["from"] = sqlite3_column_text(stmt, 3) ? (const char*)sqlite3_column_text(stmt, 3) : "";
        email_obj["subject"] = sqlite3_column_text(stmt, 4) ? (const char*)sqlite3_column_text(stmt, 4) : "";
        email_obj["date"] = sqlite3_column_text(stmt, 5) ? (const char*)sqlite3_column_text(stmt, 5) : "";
        email_obj["bodystructure"] = sqlite3_column_text(stmt, 6) ? (const char*)sqlite3_column_text(stmt, 6) : "";
        email_obj["reply_to"] = sqlite3_column_text(stmt, 7) ? (const char*)sqlite3_column_text(stmt, 7) : "";
        email_obj["in_reply_to"] = sqlite3_column_text(stmt, 8) ? (const char*)sqlite3_column_text(stmt, 8) : "";
        email_obj["message_id"] = sqlite3_column_text(stmt, 9) ? (const char*)sqlite3_column_text(stmt, 9) : "";
        email_obj["flags"] = sqlite3_column_text(stmt, 10) ? (const char*)sqlite3_column_text(stmt, 10) : "";
        email_obj["folder"] = sqlite3_column_text(stmt, 11) ? (const char*)sqlite3_column_text(stmt, 11) : "INBOX";
        email_obj["islocal"] = sqlite3_column_int(stmt, 12);
        email_obj["session_id"] = sqlite3_column_text(stmt, 13) ? (const char*)sqlite3_column_text(stmt, 13) : "";
        email_obj["servicerecvtime"] = sqlite3_column_text(stmt, 14) ? (const char*)sqlite3_column_text(stmt, 14) : "";
        email_obj["rowid"] = sqlite3_column_int64(stmt, 15);
        email_obj["to_addr"] = sqlite3_column_text(stmt, 16) ? (const char*)sqlite3_column_text(stmt, 16) : "";
        email_obj["file"] = sqlite3_column_text(stmt, 17) ? (const char*)sqlite3_column_text(stmt, 17) : "";
        emails_array.push_back(email_obj);
    }

    LOG_INFO("[DB] email_query_localemail step result: %d, found %zu emails\n", rc, emails_array.size());
    if (rc != SQLITE_DONE) {
        LOG_INFO("[DB] step error: %s\n", sqlite3_errmsg(g_db));
    }
    sqlite3_finalize(stmt);

    json response;
    response["status"] = "success";
    response["count"] = emails_array.size();
    response["emails"] = emails_array;
    response["debug_account"] = account ? account : "null";
    response["debug_account_len"] = account ? strlen(account) : 0;

    std::string jsonStr = response.dump();
    if (outJson && outSize > 0) {
        snprintf(outJson, outSize, "%s", jsonStr.c_str());
    }
    return 0;
}

int email_query_thread_roots(const char* account, char* outJson, int outSize) {
    if (!g_db) {
        if (outJson && outSize > 0) {
            snprintf(outJson, outSize, R"({"status":"failed","error":"database_not_initialized"})");
        }
        return -1;
    }

    LOG_INFO("[DB] email_query_thread_roots called with account: '%s'\n", account ? account : "null");

    const char* sql =
        "SELECT l.uuid, l.account, l.sender, l.from_addr, l.subject, l.date, l.bodystructure, l.reply_to, l.in_reply_to, l.message_id, l.flags, l.folder, l.islocal, s.session_id, l.servicerecvtime, l.to_addr, l.id, l.file "
        "FROM localemail l "
        "INNER JOIN session s ON l.id = s.email_id "
        "WHERE l.account = ? AND s.visible = 1 "
        "AND l.id = (SELECT MIN(email_id) FROM session WHERE session_id = s.session_id AND email_id > 0) "
        "ORDER BY l.id DESC;";

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        LOG_INFO("[DB] prepare failed: %s\n", sqlite3_errmsg(g_db));
        if (outJson && outSize > 0) {
            snprintf(outJson, outSize, R"({"status":"failed","error":"prepare_failed"})");
        }
        return -2;
    }

    sqlite3_bind_text(stmt, 1, account ? account : "", -1, SQLITE_STATIC);

    json emails_array = json::array();
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        json email_obj;
        email_obj["uuid"] = (const char*)sqlite3_column_text(stmt, 0);
        email_obj["account"] = (const char*)sqlite3_column_text(stmt, 1);
        email_obj["sender"] = sqlite3_column_text(stmt, 2) ? (const char*)sqlite3_column_text(stmt, 2) : "";
        email_obj["from"] = sqlite3_column_text(stmt, 3) ? (const char*)sqlite3_column_text(stmt, 3) : "";
        email_obj["subject"] = sqlite3_column_text(stmt, 4) ? (const char*)sqlite3_column_text(stmt, 4) : "";
        email_obj["date"] = sqlite3_column_text(stmt, 5) ? (const char*)sqlite3_column_text(stmt, 5) : "";
        email_obj["bodystructure"] = sqlite3_column_text(stmt, 6) ? (const char*)sqlite3_column_text(stmt, 6) : "";
        email_obj["reply_to"] = sqlite3_column_text(stmt, 7) ? (const char*)sqlite3_column_text(stmt, 7) : "";
        email_obj["in_reply_to"] = sqlite3_column_text(stmt, 8) ? (const char*)sqlite3_column_text(stmt, 8) : "";
        email_obj["message_id"] = sqlite3_column_text(stmt, 9) ? (const char*)sqlite3_column_text(stmt, 9) : "";
        email_obj["flags"] = sqlite3_column_text(stmt, 10) ? (const char*)sqlite3_column_text(stmt, 10) : "";
        email_obj["folder"] = sqlite3_column_text(stmt, 11) ? (const char*)sqlite3_column_text(stmt, 11) : "INBOX";
        email_obj["islocal"] = sqlite3_column_int(stmt, 12);
        email_obj["session_id"] = sqlite3_column_text(stmt, 13) ? (const char*)sqlite3_column_text(stmt, 13) : "";
        email_obj["servicerecvtime"] = sqlite3_column_text(stmt, 14) ? (const char*)sqlite3_column_text(stmt, 14) : "";
        email_obj["to_addr"] = sqlite3_column_text(stmt, 15) ? (const char*)sqlite3_column_text(stmt, 15) : "";
        email_obj["rowid"] = sqlite3_column_int64(stmt, 16);
        email_obj["file"] = sqlite3_column_text(stmt, 17) ? (const char*)sqlite3_column_text(stmt, 17) : "";
        emails_array.push_back(email_obj);
    }

    LOG_INFO("[DB] email_query_thread_roots step result: %d, found %zu thread roots\n", rc, emails_array.size());
    if (rc != SQLITE_DONE) {
        LOG_INFO("[DB] step error: %s\n", sqlite3_errmsg(g_db));
    }
    sqlite3_finalize(stmt);

    json response;
    response["status"] = "success";
    response["count"] = emails_array.size();
    response["emails"] = emails_array;

    std::string jsonStr = response.dump();
    if (outJson && outSize > 0) {
        snprintf(outJson, outSize, "%s", jsonStr.c_str());
    }
    return 0;
}

// Insert a record into the code table (upsert by account)
extern "C" int email_code_insert(const char* account, const char* pubkey, const char* secretkey, const char* keypassword) {
    if (!g_db || !account) return -1;

    std::string pubkeyStr = pubkey ? pubkey : "";
    std::string identify = compute_md5(pubkeyStr);
    std::string secretkeyStr = secretkey ? secretkey : "";
    std::string keypasswordStr = keypassword ? keypassword : "";

    // Check if record already exists for this account
    const char* check_sql = "SELECT id FROM code WHERE account = ?;";
    sqlite3_stmt* check_stmt;
    bool exists = false;
    if (sqlite3_prepare_v2(g_db, check_sql, -1, &check_stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(check_stmt, 1, account, -1, SQLITE_TRANSIENT);
        if (sqlite3_step(check_stmt) == SQLITE_ROW) {
            exists = true;
        }
        sqlite3_finalize(check_stmt);
    }

    if (exists) {
        // Update existing record
        const char* sql = "UPDATE code SET pubkey = ?, secretkey = ?, identify = ?, keypassword = ? WHERE account = ?;";
        sqlite3_stmt* stmt;
        int rc = sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL);
        if (rc != SQLITE_OK) {
            LOG_INFO("email_code_insert: update prepare failed: %s\n", sqlite3_errmsg(g_db));
            return -2;
        }
        sqlite3_bind_text(stmt, 1, pubkeyStr.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, secretkeyStr.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, identify.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, keypasswordStr.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 5, account, -1, SQLITE_TRANSIENT);

        rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        if (rc != SQLITE_DONE) {
            LOG_INFO("email_code_insert: update step failed: %s\n", sqlite3_errmsg(g_db));
            return -3;
        }

        LOG_INFO("email_code_insert: updated code for account=%s, identify=%s\n", account, identify.c_str());
    } else {
        const char* sql = "INSERT INTO code (account, pubkey, secretkey, identify, keypassword) VALUES (?, ?, ?, ?, ?);";
        sqlite3_stmt* stmt;
        int rc = sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL);
        if (rc != SQLITE_OK) {
            LOG_INFO("email_code_insert: insert prepare failed: %s\n", sqlite3_errmsg(g_db));
            return -2;
        }

        sqlite3_bind_text(stmt, 1, account, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, pubkeyStr.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, secretkeyStr.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, identify.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 5, keypasswordStr.c_str(), -1, SQLITE_TRANSIENT);

        rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        if (rc != SQLITE_DONE) {
            LOG_INFO("email_code_insert: insert step failed: %s\n", sqlite3_errmsg(g_db));
            return -3;
        }

        LOG_INFO("email_code_insert: inserted code for account=%s, identify=%s\n", account, identify.c_str());
    }
    return 0;
}

// Query code table by account, return JSON array
extern "C" int email_code_query_by_account(const char* account, char* outJson, int outSize) {
    if (!g_db || !account) {
        if (outJson && outSize > 0) snprintf(outJson, outSize, R"([])");
        return -1;
    }

    const char* sql = "SELECT id, account, pubkey, secretkey, identify FROM code WHERE account = ? ORDER BY id DESC;";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        if (outJson && outSize > 0) snprintf(outJson, outSize, R"([])");
        return -2;
    }

    sqlite3_bind_text(stmt, 1, account, -1, SQLITE_TRANSIENT);

    json codes = json::array();
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        json item;
        item["id"] = sqlite3_column_int64(stmt, 0);
        const char* acc = (const char*)sqlite3_column_text(stmt, 1);
        const char* pub = (const char*)sqlite3_column_text(stmt, 2);
        const char* sec = (const char*)sqlite3_column_text(stmt, 3);
        const char* iden = (const char*)sqlite3_column_text(stmt, 4);
        item["account"] = acc ? acc : "";
        item["pubkey"] = pub ? pub : "";
        item["secretkey"] = sec ? sec : "";
        item["identify"] = iden ? iden : "";
        codes.push_back(item);
    }
    sqlite3_finalize(stmt);

    std::string jsonStr = codes.dump();
    if (outJson && outSize > 0) {
        snprintf(outJson, outSize, "%s", jsonStr.c_str());
    }
    return 0;
}

// Query code table by identify (MD5 of pubkey), return JSON object
extern "C" int email_code_query_by_identify(const char* identify, char* outJson, int outSize) {
    if (!g_db || !identify) {
        if (outJson && outSize > 0) snprintf(outJson, outSize, R"({})");
        return -1;
    }

    const char* sql = "SELECT id, account, pubkey, secretkey, identify FROM code WHERE identify = ? LIMIT 1;";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        if (outJson && outSize > 0) snprintf(outJson, outSize, R"({})");
        return -2;
    }

    sqlite3_bind_text(stmt, 1, identify, -1, SQLITE_TRANSIENT);

    json result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        result["id"] = sqlite3_column_int64(stmt, 0);
        const char* acc = (const char*)sqlite3_column_text(stmt, 1);
        const char* pub = (const char*)sqlite3_column_text(stmt, 2);
        const char* sec = (const char*)sqlite3_column_text(stmt, 3);
        const char* iden = (const char*)sqlite3_column_text(stmt, 4);
        result["account"] = acc ? acc : "";
        result["pubkey"] = pub ? pub : "";
        result["secretkey"] = sec ? sec : "";
        result["identify"] = iden ? iden : "";
    }
    sqlite3_finalize(stmt);

    std::string jsonStr = result.dump();
    if (outJson && outSize > 0) {
        snprintf(outJson, outSize, "%s", jsonStr.c_str());
    }
    return 0;
}
