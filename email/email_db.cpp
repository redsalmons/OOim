#include "email_core_common.h"
#include "email_core.h"
#include "logger.h"
#include "db_connection.h"
#include "email_repo.h"
#include "x_mailer.h"
#include "key_repo.h"
#include <stdio.h>
#include <string.h>
#include <sqlite3.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

static EmailRepo s_emailRepo;
static KeyRepo s_keyRepo;

int email_db_init(const char* path) {
    if (g_db != NULL) {
        // Already initialized via legacy path
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

        // Addressbook table (auto-populated from received emails)
        const char* sql_addressbook = "CREATE TABLE IF NOT EXISTS addressbook ("
                                      "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                                      "email TEXT NOT NULL UNIQUE,"
                                      "name TEXT,"
                                      "group_name TEXT,"
                                      "notes TEXT,"
                                      "created_at TEXT DEFAULT (datetime('now','localtime')),"
                                      "updated_at TEXT DEFAULT (datetime('now','localtime'))"
                                      ");";
        sqlite3_exec(g_db, sql_addressbook, NULL, NULL, &err);
        if (err) sqlite3_free(err);
        sqlite3_exec(g_db, "CREATE INDEX IF NOT EXISTS idx_addressbook_email ON addressbook(email);", NULL, NULL, &err);
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

        // Add session_uuid column safely
        sqlite3_exec(g_db, "ALTER TABLE keyinfo ADD COLUMN session_uuid TEXT;", NULL, NULL, NULL);
        sqlite3_exec(g_db, "ALTER TABLE code ADD COLUMN session_uuid TEXT;", NULL, NULL, NULL);
        sqlite3_exec(g_db, "ALTER TABLE file_transfer ADD COLUMN message_id TEXT;", NULL, NULL, NULL);
        sqlite3_exec(g_db, "ALTER TABLE file_transfer ADD COLUMN original_path TEXT;", NULL, NULL, NULL);

        // File transfer tables
        const char* sql_file_transfer = "CREATE TABLE IF NOT EXISTS file_transfer ("
                                        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                                        "file_id TEXT NOT NULL UNIQUE,"
                                        "session_id TEXT,"
                                        "account TEXT NOT NULL,"
                                        "sender TEXT,"
                                        "file_name TEXT NOT NULL,"
                                        "file_size INTEGER NOT NULL,"
                                        "file_md5 TEXT,"
                                        "total_chunks INTEGER NOT NULL,"
                                        "chunk_size INTEGER NOT NULL,"
                                        "status INTEGER DEFAULT 0,"
                                        "message_id TEXT,"
                                        "created_at TEXT DEFAULT (datetime('now','localtime')),"
                                        "updated_at TEXT DEFAULT (datetime('now','localtime'))"
                                        ");";
        sqlite3_exec(g_db, "ALTER TABLE file_transfer ADD COLUMN original_path TEXT;", NULL, NULL, NULL);
        sqlite3_exec(g_db, sql_file_transfer, NULL, NULL, &err);
        if (err) sqlite3_free(err);
        sqlite3_exec(g_db, "CREATE INDEX IF NOT EXISTS idx_file_transfer_account ON file_transfer(account);", NULL, NULL, &err);
        if (err) sqlite3_free(err);
        sqlite3_exec(g_db, "CREATE INDEX IF NOT EXISTS idx_file_transfer_session ON file_transfer(session_id);", NULL, NULL, &err);
        if (err) sqlite3_free(err);

        const char* sql_file_chunk = "CREATE TABLE IF NOT EXISTS file_chunk ("
                                     "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                                     "file_id TEXT NOT NULL,"
                                     "chunk_index INTEGER NOT NULL,"
                                     "chunk_data TEXT,"
                                     "chunk_md5 TEXT,"
                                     "status INTEGER DEFAULT 0,"
                                     "received_at TEXT DEFAULT (datetime('now','localtime')),"
                                     "UNIQUE(file_id, chunk_index)"
                                     ");";
        sqlite3_exec(g_db, sql_file_chunk, NULL, NULL, &err);
        if (err) sqlite3_free(err);
        sqlite3_exec(g_db, "CREATE INDEX IF NOT EXISTS idx_file_chunk_file_id ON file_chunk(file_id);", NULL, NULL, &err);
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
    sqlite3_exec(g_db, "ALTER TABLE localemail ADD COLUMN visible INTEGER DEFAULT 1;", NULL, NULL, &err_msg);

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

        // Add session_uuid column safely
        sqlite3_exec(g_db, "ALTER TABLE keyinfo ADD COLUMN session_uuid TEXT;", NULL, NULL, NULL);
        sqlite3_exec(g_db, "ALTER TABLE code ADD COLUMN session_uuid TEXT;", NULL, NULL, NULL);
        sqlite3_exec(g_db, "ALTER TABLE file_transfer ADD COLUMN message_id TEXT;", NULL, NULL, NULL);
        sqlite3_exec(g_db, "ALTER TABLE file_transfer ADD COLUMN original_path TEXT;", NULL, NULL, NULL);
    }

    // Task table for queued email sending
    const char* sql_task = "CREATE TABLE IF NOT EXISTS task ("
                           "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                           "account TEXT NOT NULL,"
                           "recipient TEXT NOT NULL,"
                           "subject TEXT NOT NULL,"
                           "body TEXT NOT NULL,"
                           "in_reply_to TEXT,"
                           "message_id TEXT,"
                           "x_message_id TEXT,"
                           "session_id TEXT,"
                           "x_session_chart TEXT DEFAULT '0.1.2',"
                           "status INTEGER DEFAULT 0,"
                           "created_at TEXT DEFAULT (datetime('now','localtime'))"
                           ");";
    rc = sqlite3_exec(g_db, sql_task, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        LOG_INFO("SQL error (task): %s\n", err_msg);
        sqlite3_free(err_msg);
    } else {
        sqlite3_exec(g_db, "CREATE INDEX IF NOT EXISTS idx_task_account_status ON task(account, status);", NULL, NULL, &err_msg);
        if (err_msg) sqlite3_free(err_msg);
    }

    // File transfer tables
    const char* sql_file_transfer = "CREATE TABLE IF NOT EXISTS file_transfer ("
                                    "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                                    "file_id TEXT NOT NULL UNIQUE,"
                                    "session_id TEXT,"
                                    "account TEXT NOT NULL,"
                                    "sender TEXT,"
                                    "file_name TEXT NOT NULL,"
                                    "file_size INTEGER NOT NULL,"
                                    "file_md5 TEXT,"
                                    "total_chunks INTEGER NOT NULL,"
                                    "chunk_size INTEGER NOT NULL,"
                                    "status INTEGER DEFAULT 0,"
                                    "message_id TEXT,"
                                    "created_at TEXT DEFAULT (datetime('now','localtime')),"
                                    "updated_at TEXT DEFAULT (datetime('now','localtime'))"
                                    ");";
    rc = sqlite3_exec(g_db, sql_file_transfer, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        LOG_INFO("SQL error (file_transfer): %s\n", err_msg);
        sqlite3_free(err_msg);
    } else {
        sqlite3_exec(g_db, "CREATE INDEX IF NOT EXISTS idx_file_transfer_account ON file_transfer(account);", NULL, NULL, &err_msg);
        if (err_msg) sqlite3_free(err_msg);
        sqlite3_exec(g_db, "CREATE INDEX IF NOT EXISTS idx_file_transfer_session ON file_transfer(session_id);", NULL, NULL, &err_msg);
        if (err_msg) sqlite3_free(err_msg);
    }

    const char* sql_file_chunk = "CREATE TABLE IF NOT EXISTS file_chunk ("
                                 "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                                 "file_id TEXT NOT NULL,"
                                 "chunk_index INTEGER NOT NULL,"
                                 "chunk_data TEXT,"
                                 "chunk_md5 TEXT,"
                                 "status INTEGER DEFAULT 0,"
                                 "received_at TEXT DEFAULT (datetime('now','localtime')),"
                                 "UNIQUE(file_id, chunk_index)"
                                 ");";
    rc = sqlite3_exec(g_db, sql_file_chunk, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        LOG_INFO("SQL error (file_chunk): %s\n", err_msg);
        sqlite3_free(err_msg);
    } else {
        sqlite3_exec(g_db, "CREATE INDEX IF NOT EXISTS idx_file_chunk_file_id ON file_chunk(file_id);", NULL, NULL, &err_msg);
        if (err_msg) sqlite3_free(err_msg);
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
    auto& conn = DbConnection::instance();
    if (!conn.get()) {
        if (outJson && outSize > 0) {
            snprintf(outJson, outSize, R"({"status":"failed","error":"database_not_initialized"})");
        }
        return -1;
    }

    LOG_INFO("[DB] email_query_localemail called with account: '%s'\n", account ? account : "null");

    auto emails = s_emailRepo.queryByAccount(account ? account : "");

    json emails_array = json::array();
    for (const auto& e : emails) {
        json email_obj;
        email_obj["uuid"] = e.uuid;
        email_obj["account"] = e.account;
        email_obj["sender"] = e.sender;
        email_obj["from"] = e.fromAddr;
        email_obj["subject"] = e.subject;
        email_obj["date"] = e.date;
        email_obj["bodystructure"] = e.bodystructure;
        email_obj["reply_to"] = e.replyTo;
        email_obj["in_reply_to"] = e.inReplyTo;
        email_obj["message_id"] = e.messageId;
        email_obj["flags"] = e.flags;
        email_obj["folder"] = e.folder;
        email_obj["islocal"] = e.isLocal;
        email_obj["session_id"] = e.sessionId;
        email_obj["servicerecvtime"] = e.servicerecvtime;
        email_obj["rowid"] = e.rowid;
        email_obj["to_addr"] = e.toAddr;
        email_obj["file"] = e.file;
        email_obj["visible"] = e.visible;
        emails_array.push_back(email_obj);
    }

    LOG_INFO("[DB] email_query_localemail found %zu emails\n", emails_array.size());

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
    auto& conn = DbConnection::instance();
    if (!conn.get()) {
        if (outJson && outSize > 0) {
            snprintf(outJson, outSize, R"({"status":"failed","error":"database_not_initialized"})");
        }
        return -1;
    }

    LOG_INFO("[DB] email_query_thread_roots called with account: '%s'\n", account ? account : "null");

    auto emails = s_emailRepo.queryThreadRoots(account ? account : "");

    json emails_array = json::array();
    for (const auto& e : emails) {
        json email_obj;
        email_obj["uuid"] = e.uuid;
        email_obj["account"] = e.account;
        email_obj["sender"] = e.sender;
        email_obj["from"] = e.fromAddr;
        email_obj["subject"] = e.subject;
        email_obj["date"] = e.date;
        email_obj["bodystructure"] = e.bodystructure;
        email_obj["reply_to"] = e.replyTo;
        email_obj["in_reply_to"] = e.inReplyTo;
        email_obj["message_id"] = e.messageId;
        email_obj["flags"] = e.flags;
        email_obj["folder"] = e.folder;
        email_obj["islocal"] = e.isLocal;
        email_obj["session_id"] = e.sessionId;
        email_obj["servicerecvtime"] = e.servicerecvtime;
        email_obj["to_addr"] = e.toAddr;
        email_obj["rowid"] = e.rowid;
        email_obj["file"] = e.file;
        emails_array.push_back(email_obj);
    }

    LOG_INFO("[DB] email_query_thread_roots found %zu thread roots\n", emails_array.size());

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
extern "C" int email_code_insert(const char* account, const char* pubkey, const char* secretkey, const char* sessionUuid) {
    if (!account) return -1;

    std::string pubkeyStr = pubkey ? pubkey : "";
    std::string secretkeyStr = secretkey ? secretkey : "";
    std::string sessionUuidStr = sessionUuid ? sessionUuid : "";
    std::string identify = compute_md5(pubkeyStr);

    if (!s_keyRepo.upsertCode(account, pubkeyStr, secretkeyStr, sessionUuidStr)) {
        LOG_INFO("email_code_insert: failed for account=%s\n", account);
        return -3;
    }

    LOG_INFO("email_code_insert: upserted code for account=%s, identify=%s\n", account, identify.c_str());
    return 0;
}

// Query code table by account, return JSON array
extern "C" int email_code_query_by_account(const char* account, char* outJson, int outSize) {
    if (!account) {
        if (outJson && outSize > 0) snprintf(outJson, outSize, R"([])");
        return -1;
    }

    auto codes = s_keyRepo.queryCodeByAccount(account);

    json codes_array = json::array();
    for (const auto& c : codes) {
        json item;
        item["id"] = c.id;
        item["account"] = c.account;
        item["pubkey"] = c.pubkey;
        item["secretkey"] = c.secretkey;
        item["identify"] = c.identify;
        codes_array.push_back(item);
    }

    std::string jsonStr = codes_array.dump();
    if (outJson && outSize > 0) {
        snprintf(outJson, outSize, "%s", jsonStr.c_str());
    }
    return 0;
}

// Query code table by identify (MD5 of pubkey), return JSON object
extern "C" int email_code_query_by_identify(const char* identify, char* outJson, int outSize) {
    if (!identify) {
        if (outJson && outSize > 0) snprintf(outJson, outSize, R"({})");
        return -1;
    }

    auto rec = s_keyRepo.queryCodeByIdentify(identify);

    json result;
    result["id"] = rec.id;
    result["account"] = rec.account;
    result["pubkey"] = rec.pubkey;
    result["secretkey"] = rec.secretkey;
    result["identify"] = rec.identify;

    std::string jsonStr = result.dump();
    if (outJson && outSize > 0) {
        snprintf(outJson, outSize, "%s", jsonStr.c_str());
    }
    return 0;
}
