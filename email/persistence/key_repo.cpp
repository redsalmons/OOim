#include "key_repo.h"
#include "db_connection.h"
#include "../email_core_common.h"
#include <sqlite3.h>
#include <cstring>
#include <vector>

// --- code table ---

bool KeyRepo::upsertCode(const std::string& account, const std::string& pubkey, const std::string& secretkey, const std::string& sessionUuid) {
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db || account.empty()) return false;

    std::string identify = compute_md5(pubkey);

    // Check if record exists
    const char* check_sql = "SELECT id FROM code WHERE account = ? AND session_uuid = ?;";
    sqlite3_stmt* check_stmt;
    bool exists = false;
    if (sqlite3_prepare_v2(db, check_sql, -1, &check_stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(check_stmt, 1, account.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(check_stmt, 2, sessionUuid.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(check_stmt) == SQLITE_ROW) {
            exists = true;
        }
        sqlite3_finalize(check_stmt);
    }

    if (exists) {
        const char* sql = "UPDATE code SET pubkey = ?, secretkey = ?, identify = ? WHERE account = ? AND session_uuid = ?;";
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;
        sqlite3_bind_text(stmt, 1, pubkey.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, secretkey.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, identify.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, account.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 5, sessionUuid.c_str(), -1, SQLITE_TRANSIENT);
        int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        return rc == SQLITE_DONE;
    } else {
        const char* sql = "INSERT INTO code (account, pubkey, secretkey, identify, session_uuid) VALUES (?, ?, ?, ?, ?);";
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;
        sqlite3_bind_text(stmt, 1, account.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, pubkey.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, secretkey.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, identify.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 5, sessionUuid.c_str(), -1, SQLITE_TRANSIENT);
        int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        return rc == SQLITE_DONE;
    }
}

std::string KeyRepo::queryPubkeyByAccountAndSession(const std::string& account, const std::string& sessionUuid) {
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db) return "";

    if (!sessionUuid.empty()) {
        const char* sql = "SELECT pubkey FROM code WHERE account = ? AND session_uuid = ? ORDER BY id DESC LIMIT 1;";
        sqlite3_stmt* stmt;
        std::string result;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, account.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, sessionUuid.c_str(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                const char* pub = (const char*)sqlite3_column_text(stmt, 0);
                if (pub) result = pub;
            }
            sqlite3_finalize(stmt);
        }
        if (!result.empty()) {
            return result;
        }
    }

    // Fallback to latest pubkey if not found by session
    return queryPubkeyByAccount(account);
}

std::string KeyRepo::queryPubkeyByAccount(const std::string& account) {
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db) return "";

    const char* sql = "SELECT pubkey FROM code WHERE account = ? ORDER BY id DESC LIMIT 1;";
    sqlite3_stmt* stmt;
    std::string result;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, account.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* pub = (const char*)sqlite3_column_text(stmt, 0);
            if (pub) result = pub;
        }
        sqlite3_finalize(stmt);
    }
    return result;
}

std::vector<CodeRecord> KeyRepo::queryCodeByAccount(const std::string& account) {
    std::vector<CodeRecord> result;
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db) return result;

    const char* sql = "SELECT id, account, pubkey, secretkey, identify FROM code WHERE account = ? ORDER BY id DESC;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return result;
    sqlite3_bind_text(stmt, 1, account.c_str(), -1, SQLITE_TRANSIENT);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        CodeRecord r;
        r.id = sqlite3_column_int64(stmt, 0);
        r.account = (const char*)sqlite3_column_text(stmt, 1);
        r.pubkey = sqlite3_column_text(stmt, 2) ? (const char*)sqlite3_column_text(stmt, 2) : "";
        r.secretkey = sqlite3_column_text(stmt, 3) ? (const char*)sqlite3_column_text(stmt, 3) : "";
        r.identify = sqlite3_column_text(stmt, 4) ? (const char*)sqlite3_column_text(stmt, 4) : "";
        result.push_back(r);
    }
    sqlite3_finalize(stmt);
    return result;
}

CodeRecord KeyRepo::queryCodeByIdentify(const std::string& identify) {
    CodeRecord result;
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db) return result;

    const char* sql = "SELECT id, account, pubkey, secretkey, identify FROM code WHERE identify = ? LIMIT 1;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, identify.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            result.id = sqlite3_column_int64(stmt, 0);
            result.account = (const char*)sqlite3_column_text(stmt, 1);
            result.pubkey = sqlite3_column_text(stmt, 2) ? (const char*)sqlite3_column_text(stmt, 2) : "";
            result.secretkey = sqlite3_column_text(stmt, 3) ? (const char*)sqlite3_column_text(stmt, 3) : "";
            result.identify = sqlite3_column_text(stmt, 4) ? (const char*)sqlite3_column_text(stmt, 4) : "";
        }
        sqlite3_finalize(stmt);
    }
    return result;
}

// --- keyinfo table ---

bool KeyRepo::insertKeyInfo(const std::string& pub, const std::string& key,
    const std::string& password, const std::string& sessionUuid, const std::string& account) {

    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db) return false;

    const char* sql = "INSERT INTO keyinfo (pub, key, password, session_uuid, account) VALUES (?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, pub.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, key.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, password.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, sessionUuid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, account.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

std::string KeyRepo::queryLatestPubkeyFromKeyInfo(const std::string& account) {
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db) return "";

    const char* sql = "SELECT pub FROM keyinfo WHERE account = ? AND key != '' ORDER BY id DESC LIMIT 1;";
    sqlite3_stmt* stmt;
    std::string result;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, account.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* pub = (const char*)sqlite3_column_text(stmt, 0);
            if (pub) result = pub;
        }
        sqlite3_finalize(stmt);
    }
    return result;
}

bool KeyRepo::queryPrivateKeyByAccountAndMd5(const std::string& account,
    const std::string& expectedMd5,
    std::string& outPrivPem, std::string& outKeyPassword) {

    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db) return false;

    const char* sql = "SELECT key, password, pub FROM keyinfo WHERE account = ? AND key != '' ORDER BY id DESC;";
    sqlite3_stmt* stmt;
    bool found = false;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, account.c_str(), -1, SQLITE_TRANSIENT);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* sk = (const char*)sqlite3_column_text(stmt, 0);
            const char* kp = (const char*)sqlite3_column_text(stmt, 1);
            const char* pub = (const char*)sqlite3_column_text(stmt, 2);
            if (pub && compute_md5(std::string(pub)) == expectedMd5) {
                if (sk) outPrivPem = sk;
                if (kp) outKeyPassword = kp;
                found = true;
                break;
            }
        }
        sqlite3_finalize(stmt);
    }
    return found;
}

bool KeyRepo::queryKeyInfoBySession(const std::string& sessionUuid,
    std::string& outPubkey, std::string& outPrivPem, std::string& outKeyPassword) {

    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db) return false;

    const char* sql = "SELECT pub, key, password FROM keyinfo WHERE session_uuid = ? AND key != '' ORDER BY id DESC LIMIT 1;";
    sqlite3_stmt* stmt;
    bool found = false;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, sessionUuid.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* pub = (const char*)sqlite3_column_text(stmt, 0);
            const char* key = (const char*)sqlite3_column_text(stmt, 1);
            const char* pwd = (const char*)sqlite3_column_text(stmt, 2);
            if (pub) outPubkey = pub;
            if (key) outPrivPem = key;
            if (pwd) outKeyPassword = pwd;
            found = true;
        }
        sqlite3_finalize(stmt);
    }
    return found;
}
