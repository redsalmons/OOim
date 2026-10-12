#include "signal_key_repo.h"
#include "db_connection.h"
#include "logger.h"
#include <sqlite3.h>

static sqlite3* db_handle() { return DbConnection::instance().get(); }

static std::string col_text(sqlite3_stmt* stmt, int i) {
    const unsigned char* t = sqlite3_column_text(stmt, i);
    return t ? (const char*)t : "";
}

// --- Account identity key ---

bool SignalKeyRepo::upsertIdentity(const std::string& account, const std::string& ikPub,
                                   const std::string& ikPriv, const std::string& ikPassword) {
    sqlite3* db = db_handle();
    if (!db) return false;
    const char* sql = "INSERT INTO signal_identity (account, ik_pub, ik_priv, ik_password) "
                      "VALUES (?, ?, ?, ?) "
                      "ON CONFLICT(account) DO UPDATE SET ik_pub=excluded.ik_pub, "
                      "ik_priv=excluded.ik_priv, ik_password=excluded.ik_password;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, account.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, ikPub.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, ikPriv.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, ikPassword.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool SignalKeyRepo::loadIdentity(const std::string& account, SignalIdentityRecord& out) {
    sqlite3* db = db_handle();
    if (!db) return false;
    const char* sql = "SELECT id, account, ik_pub, ik_priv, ik_password FROM signal_identity "
                      "WHERE account = ? LIMIT 1;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, account.c_str(), -1, SQLITE_TRANSIENT);
    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        out.id = sqlite3_column_int64(stmt, 0);
        out.account = col_text(stmt, 1);
        out.ikPub = col_text(stmt, 2);
        out.ikPriv = col_text(stmt, 3);
        out.ikPassword = col_text(stmt, 4);
        found = true;
    }
    sqlite3_finalize(stmt);
    return found;
}

// --- Per-session signed prekey ---

static void read_prekey(sqlite3_stmt* stmt, SignalPrekeyRecord& out) {
    out.id = sqlite3_column_int64(stmt, 0);
    out.account = col_text(stmt, 1);
    out.sessionUuid = col_text(stmt, 2);
    out.pub = col_text(stmt, 3);
    out.priv = col_text(stmt, 4);
    out.password = col_text(stmt, 5);
    out.signature = col_text(stmt, 6);
}

bool SignalKeyRepo::insertSignedPrekey(const std::string& account, const std::string& sessionUuid,
                                       const std::string& pub, const std::string& priv,
                                       const std::string& password, const std::string& signature) {
    sqlite3* db = db_handle();
    if (!db) return false;
    const char* sql = "INSERT INTO signal_prekey (account, session_uuid, pub, priv, password, signature) "
                      "VALUES (?, ?, ?, ?, ?, ?) "
                      "ON CONFLICT(account, session_uuid) DO UPDATE SET pub=excluded.pub, "
                      "priv=excluded.priv, password=excluded.password, signature=excluded.signature;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, account.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, sessionUuid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, pub.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, priv.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, password.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, signature.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool SignalKeyRepo::loadSignedPrekey(const std::string& account, const std::string& sessionUuid,
                                     SignalPrekeyRecord& out) {
    sqlite3* db = db_handle();
    if (!db) return false;
    const char* sql = "SELECT id, account, session_uuid, pub, priv, password, signature "
                      "FROM signal_prekey WHERE account = ? AND session_uuid = ? LIMIT 1;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, account.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, sessionUuid.c_str(), -1, SQLITE_TRANSIENT);
    bool found = sqlite3_step(stmt) == SQLITE_ROW;
    if (found) read_prekey(stmt, out);
    sqlite3_finalize(stmt);
    return found;
}

bool SignalKeyRepo::loadSignedPrekeyByPub(const std::string& account, const std::string& pubPem,
                                          SignalPrekeyRecord& out) {
    sqlite3* db = db_handle();
    if (!db) return false;
    const char* sql = "SELECT id, account, session_uuid, pub, priv, password, signature "
                      "FROM signal_prekey WHERE account = ? AND pub = ? LIMIT 1;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, account.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, pubPem.c_str(), -1, SQLITE_TRANSIENT);
    bool found = sqlite3_step(stmt) == SQLITE_ROW;
    if (found) read_prekey(stmt, out);
    sqlite3_finalize(stmt);
    return found;
}

// --- Peer prekey bundle (per session) ---

bool SignalKeyRepo::upsertPeerPrekey(const std::string& account, const std::string& peerEmail,
                                     const std::string& sessionUuid, const std::string& ikPub,
                                     const std::string& spkPub, const std::string& spkSig) {
    sqlite3* db = db_handle();
    if (!db) return false;
    const char* sql = "INSERT INTO signal_peer_prekey (account, peer_email, session_uuid, ik_pub, spk_pub, spk_sig) "
                      "VALUES (?, ?, ?, ?, ?, ?) "
                      "ON CONFLICT(account, peer_email, session_uuid) DO UPDATE SET "
                      "ik_pub=excluded.ik_pub, spk_pub=excluded.spk_pub, spk_sig=excluded.spk_sig, "
                      "received_at=datetime('now','localtime');";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, account.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, peerEmail.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, sessionUuid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, ikPub.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, spkPub.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, spkSig.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool SignalKeyRepo::loadPeerPrekey(const std::string& account, const std::string& peerEmail,
                                   const std::string& sessionUuid, SignalPeerPrekeyRecord& out) {
    sqlite3* db = db_handle();
    if (!db) return false;
    const char* sql = "SELECT id, account, peer_email, session_uuid, ik_pub, spk_pub, spk_sig "
                      "FROM signal_peer_prekey WHERE account = ? AND peer_email = ? AND session_uuid = ? LIMIT 1;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, account.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, peerEmail.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, sessionUuid.c_str(), -1, SQLITE_TRANSIENT);
    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        out.id = sqlite3_column_int64(stmt, 0);
        out.account = col_text(stmt, 1);
        out.peerEmail = col_text(stmt, 2);
        out.sessionUuid = col_text(stmt, 3);
        out.ikPub = col_text(stmt, 4);
        out.spkPub = col_text(stmt, 5);
        out.spkSig = col_text(stmt, 6);
        found = true;
    }
    sqlite3_finalize(stmt);
    return found;
}

// --- Peer identity (TOFU) ---

bool SignalKeyRepo::loadPeerIdentity(const std::string& account, const std::string& peerEmail,
                                     SignalPeerIdentityRecord& out) {
    sqlite3* db = db_handle();
    if (!db) return false;
    const char* sql = "SELECT id, account, peer_email, ik_pub FROM signal_peer_identity "
                      "WHERE account = ? AND peer_email = ? LIMIT 1;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, account.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, peerEmail.c_str(), -1, SQLITE_TRANSIENT);
    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        out.id = sqlite3_column_int64(stmt, 0);
        out.account = col_text(stmt, 1);
        out.peerEmail = col_text(stmt, 2);
        out.ikPub = col_text(stmt, 3);
        found = true;
    }
    sqlite3_finalize(stmt);
    return found;
}

bool SignalKeyRepo::upsertPeerIdentity(const std::string& account, const std::string& peerEmail,
                                       const std::string& ikPub) {
    sqlite3* db = db_handle();
    if (!db) return false;
    const char* sql = "INSERT INTO signal_peer_identity (account, peer_email, ik_pub) VALUES (?, ?, ?) "
                      "ON CONFLICT(account, peer_email) DO UPDATE SET ik_pub=excluded.ik_pub, "
                      "first_seen=datetime('now','localtime');";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, account.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, peerEmail.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, ikPub.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool SignalKeyRepo::deletePeerIdentity(const std::string& account, const std::string& peerEmail) {
    sqlite3* db = db_handle();
    if (!db) return false;
    const char* sql = "DELETE FROM signal_peer_identity WHERE account = ? AND peer_email = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, account.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, peerEmail.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}
