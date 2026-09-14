#include "sender_key_repo.h"
#include "db_connection.h"
#include "logger.h"
#include <sqlite3.h>

bool SenderKeyRepo::saveSenderKey(const SenderKeyRecord& rec) {
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db) return false;

    const char* sql = "INSERT INTO sender_key "
                      "(group_id, account, sender_email, chain_key, signing_key_pub, "
                      "signing_key_priv, iteration, epoch, status, updated_at) "
                      "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, datetime('now','localtime')) "
                      "ON CONFLICT(group_id, account, sender_email, epoch) DO UPDATE SET "
                      "chain_key=excluded.chain_key, signing_key_pub=excluded.signing_key_pub, "
                      "signing_key_priv=excluded.signing_key_priv, iteration=excluded.iteration, "
                      "status=excluded.status, updated_at=datetime('now','localtime');";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, rec.groupId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, rec.account.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, rec.senderEmail.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, rec.chainKey.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, rec.signingKeyPub.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, rec.signingKeyPriv.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 7, rec.iteration);
    sqlite3_bind_int(stmt, 8, rec.epoch);
    sqlite3_bind_int(stmt, 9, rec.status);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool SenderKeyRepo::loadSenderKey(const std::string& groupId, const std::string& account,
                                   const std::string& senderEmail, int epoch,
                                   SenderKeyRecord& out) {
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db) return false;

    const char* sql = "SELECT id, group_id, account, sender_email, chain_key, "
                      "signing_key_pub, signing_key_priv, iteration, epoch, status "
                      "FROM sender_key WHERE group_id=? AND account=? AND sender_email=? AND epoch=? LIMIT 1;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, groupId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, account.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, senderEmail.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 4, epoch);

    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        out.id = sqlite3_column_int64(stmt, 0);
        out.groupId = (const char*)sqlite3_column_text(stmt, 1);
        out.account = (const char*)sqlite3_column_text(stmt, 2);
        out.senderEmail = (const char*)sqlite3_column_text(stmt, 3);
        out.chainKey = sqlite3_column_text(stmt, 4) ? (const char*)sqlite3_column_text(stmt, 4) : "";
        out.signingKeyPub = sqlite3_column_text(stmt, 5) ? (const char*)sqlite3_column_text(stmt, 5) : "";
        out.signingKeyPriv = sqlite3_column_text(stmt, 6) ? (const char*)sqlite3_column_text(stmt, 6) : "";
        out.iteration = sqlite3_column_int(stmt, 7);
        out.epoch = sqlite3_column_int(stmt, 8);
        out.status = sqlite3_column_int(stmt, 9);
        found = true;
    }
    sqlite3_finalize(stmt);
    return found;
}

bool SenderKeyRepo::loadActiveSenderKey(const std::string& groupId, const std::string& account,
                                          const std::string& senderEmail,
                                          SenderKeyRecord& out) {
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db) return false;

    const char* sql = "SELECT id, group_id, account, sender_email, chain_key, "
                      "signing_key_pub, signing_key_priv, iteration, epoch, status "
                      "FROM sender_key WHERE group_id=? AND account=? AND sender_email=? AND status=0 "
                      "ORDER BY epoch DESC LIMIT 1;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, groupId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, account.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, senderEmail.c_str(), -1, SQLITE_TRANSIENT);

    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        out.id = sqlite3_column_int64(stmt, 0);
        out.groupId = (const char*)sqlite3_column_text(stmt, 1);
        out.account = (const char*)sqlite3_column_text(stmt, 2);
        out.senderEmail = (const char*)sqlite3_column_text(stmt, 3);
        out.chainKey = sqlite3_column_text(stmt, 4) ? (const char*)sqlite3_column_text(stmt, 4) : "";
        out.signingKeyPub = sqlite3_column_text(stmt, 5) ? (const char*)sqlite3_column_text(stmt, 5) : "";
        out.signingKeyPriv = sqlite3_column_text(stmt, 6) ? (const char*)sqlite3_column_text(stmt, 6) : "";
        out.iteration = sqlite3_column_int(stmt, 7);
        out.epoch = sqlite3_column_int(stmt, 8);
        out.status = sqlite3_column_int(stmt, 9);
        found = true;
    }
    sqlite3_finalize(stmt);
    return found;
}

bool SenderKeyRepo::updateChainKey(const std::string& groupId, const std::string& account,
                                    const std::string& senderEmail, int epoch,
                                    const std::string& chainKey, int iteration) {
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db) return false;

    const char* sql = "UPDATE sender_key SET chain_key=?, iteration=?, updated_at=datetime('now','localtime') "
                      "WHERE group_id=? AND account=? AND sender_email=? AND epoch=?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, chainKey.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, iteration);
    sqlite3_bind_text(stmt, 3, groupId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, account.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, senderEmail.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 6, epoch);

    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

bool SenderKeyRepo::closeSenderKeys(const std::string& groupId, const std::string& account) {
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db) return false;

    const char* sql = "UPDATE sender_key SET status=1 WHERE group_id=? AND account=?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, groupId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, account.c_str(), -1, SQLITE_TRANSIENT);

    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

bool SenderKeyRepo::insertSkippedKey(const SkippedSenderKeyRecord& rec) {
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db) return false;

    const char* sql = "INSERT OR REPLACE INTO skipped_sender_keys "
                      "(group_id, account, sender_email, iteration, epoch, cipher_key, iv, signing_key) "
                      "VALUES (?, ?, ?, ?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, rec.groupId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, rec.account.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, rec.senderEmail.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 4, rec.iteration);
    sqlite3_bind_int(stmt, 5, rec.epoch);
    sqlite3_bind_text(stmt, 6, rec.cipherKey.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, rec.iv.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 8, rec.signingKey.c_str(), -1, SQLITE_TRANSIENT);

    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

bool SenderKeyRepo::findSkippedKey(const std::string& groupId, const std::string& account,
                                    const std::string& senderEmail, int iteration, int epoch,
                                    SkippedSenderKeyRecord& out) {
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db) return false;

    const char* sql = "SELECT id, group_id, account, sender_email, iteration, epoch, "
                      "cipher_key, iv, signing_key FROM skipped_sender_keys "
                      "WHERE group_id=? AND account=? AND sender_email=? AND iteration=? AND epoch=? LIMIT 1;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, groupId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, account.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, senderEmail.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 4, iteration);
    sqlite3_bind_int(stmt, 5, epoch);

    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        out.id = sqlite3_column_int64(stmt, 0);
        out.groupId = (const char*)sqlite3_column_text(stmt, 1);
        out.account = (const char*)sqlite3_column_text(stmt, 2);
        out.senderEmail = (const char*)sqlite3_column_text(stmt, 3);
        out.iteration = sqlite3_column_int(stmt, 4);
        out.epoch = sqlite3_column_int(stmt, 5);
        out.cipherKey = sqlite3_column_text(stmt, 6) ? (const char*)sqlite3_column_text(stmt, 6) : "";
        out.iv = sqlite3_column_text(stmt, 7) ? (const char*)sqlite3_column_text(stmt, 7) : "";
        out.signingKey = sqlite3_column_text(stmt, 8) ? (const char*)sqlite3_column_text(stmt, 8) : "";
        found = true;
    }
    sqlite3_finalize(stmt);
    return found;
}

bool SenderKeyRepo::deleteSkippedKey(const std::string& groupId, const std::string& account,
                                      const std::string& senderEmail, int iteration, int epoch) {
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db) return false;

    const char* sql = "DELETE FROM skipped_sender_keys "
                      "WHERE group_id=? AND account=? AND sender_email=? AND iteration=? AND epoch=?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, groupId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, account.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, senderEmail.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 4, iteration);
    sqlite3_bind_int(stmt, 5, epoch);

    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

int SenderKeyRepo::countSkippedKeys(const std::string& groupId, const std::string& account,
                                      const std::string& senderEmail) {
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db) return 0;

    const char* sql = "SELECT COUNT(*) FROM skipped_sender_keys "
                      "WHERE group_id=? AND account=? AND sender_email=?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return 0;

    sqlite3_bind_text(stmt, 1, groupId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, account.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, senderEmail.c_str(), -1, SQLITE_TRANSIENT);

    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return count;
}
