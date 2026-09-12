#include "signal_key_repo.h"
#include "db_connection.h"
#include "logger.h"
#include <sqlite3.h>

bool SignalKeyRepo::upsertIdentity(const std::string& account, const std::string& ikPub,
                                   const std::string& ikPriv, const std::string& ikPassword) {
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db) return false;

    const char* sql = "INSERT INTO signal_identity (account, ik_pub, ik_priv, ik_password) "
                      "VALUES (?, ?, ?, ?) "
                      "ON CONFLICT(account) DO UPDATE SET ik_pub=excluded.ik_pub, ik_priv=excluded.ik_priv, ik_password=excluded.ik_password;";
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
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db) return false;

    const char* sql = "SELECT id, account, ik_pub, ik_priv, ik_password, session_uuid FROM signal_identity WHERE account = ? LIMIT 1;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, account.c_str(), -1, SQLITE_TRANSIENT);

    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        out.id = sqlite3_column_int64(stmt, 0);
        out.account = (const char*)sqlite3_column_text(stmt, 1);
        out.ikPub = (const char*)sqlite3_column_text(stmt, 2);
        out.ikPriv = (const char*)sqlite3_column_text(stmt, 3);
        out.ikPassword = sqlite3_column_text(stmt, 4) ? (const char*)sqlite3_column_text(stmt, 4) : "";
        out.sessionUuid = sqlite3_column_text(stmt, 5) ? (const char*)sqlite3_column_text(stmt, 5) : "";
        found = true;
    }
    sqlite3_finalize(stmt);
    return found;
}

bool SignalKeyRepo::insertSignedPrekey(const std::string& account, const std::string& pub,
                                       const std::string& priv, const std::string& password,
                                       const std::string& signature) {
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db) return false;

    // Delete old signed prekeys for this account, then insert new one
    sqlite3_exec(db, "BEGIN;", NULL, NULL, NULL);

    const char* delSql = "DELETE FROM signal_prekey WHERE account = ? AND key_type = 0;";
    sqlite3_stmt* delStmt;
    if (sqlite3_prepare_v2(db, delSql, -1, &delStmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(delStmt, 1, account.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(delStmt);
        sqlite3_finalize(delStmt);
    }

    const char* sql = "INSERT INTO signal_prekey (account, key_type, pub, priv, password, signature, used) "
                      "VALUES (?, 0, ?, ?, ?, ?, 0);";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
        return false;
    }

    sqlite3_bind_text(stmt, 1, account.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, pub.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, priv.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, password.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, signature.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc == SQLITE_DONE) {
        sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL);
        return true;
    } else {
        sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
        return false;
    }
}

bool SignalKeyRepo::loadSignedPrekey(const std::string& account, SignalPrekeyRecord& out) {
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db) return false;

    const char* sql = "SELECT id, account, key_type, pub, priv, password, signature, used, session_uuid "
                      "FROM signal_prekey WHERE account = ? AND key_type = 0 "
                      "ORDER BY id DESC LIMIT 1;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, account.c_str(), -1, SQLITE_TRANSIENT);

    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        out.id = sqlite3_column_int64(stmt, 0);
        out.account = (const char*)sqlite3_column_text(stmt, 1);
        out.keyType = sqlite3_column_int(stmt, 2);
        out.pub = (const char*)sqlite3_column_text(stmt, 3);
        out.priv = (const char*)sqlite3_column_text(stmt, 4);
        out.password = sqlite3_column_text(stmt, 5) ? (const char*)sqlite3_column_text(stmt, 5) : "";
        out.signature = sqlite3_column_text(stmt, 6) ? (const char*)sqlite3_column_text(stmt, 6) : "";
        out.used = sqlite3_column_int(stmt, 7);
        out.sessionUuid = sqlite3_column_text(stmt, 8) ? (const char*)sqlite3_column_text(stmt, 8) : "";
        found = true;
    }
    sqlite3_finalize(stmt);
    return found;
}

bool SignalKeyRepo::insertOneTimePrekey(const std::string& account, const std::string& pub,
                                        const std::string& priv, const std::string& password) {
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db) return false;

    const char* sql = "INSERT INTO signal_prekey (account, key_type, pub, priv, password, used) "
                      "VALUES (?, 1, ?, ?, ?, 0);";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, account.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, pub.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, priv.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, password.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool SignalKeyRepo::claimOneTimePrekey(const std::string& account, SignalPrekeyRecord& out) {
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db) return false;

    // Find an available OPK
    const char* findSql = "SELECT id, account, key_type, pub, priv, password, used, session_uuid "
                          "FROM signal_prekey WHERE account = ? AND key_type = 1 AND used = 0 "
                          "ORDER BY id ASC LIMIT 1;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, findSql, -1, &stmt, NULL) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, account.c_str(), -1, SQLITE_TRANSIENT);

    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        out.id = sqlite3_column_int64(stmt, 0);
        out.account = (const char*)sqlite3_column_text(stmt, 1);
        out.keyType = sqlite3_column_int(stmt, 2);
        out.pub = (const char*)sqlite3_column_text(stmt, 3);
        out.priv = (const char*)sqlite3_column_text(stmt, 4);
        out.password = sqlite3_column_text(stmt, 5) ? (const char*)sqlite3_column_text(stmt, 5) : "";
        out.signature = "";
        out.used = sqlite3_column_int(stmt, 6);
        out.sessionUuid = sqlite3_column_text(stmt, 7) ? (const char*)sqlite3_column_text(stmt, 7) : "";
        found = true;
    }
    sqlite3_finalize(stmt);

    if (!found) return false;

    // Mark as used
    const char* updateSql = "UPDATE signal_prekey SET used = 1 WHERE id = ?;";
    if (sqlite3_prepare_v2(db, updateSql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, out.id);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    return true;
}

int SignalKeyRepo::countAvailableOneTimePrekeys(const std::string& account) {
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db) return 0;

    const char* sql = "SELECT COUNT(*) FROM signal_prekey WHERE account = ? AND key_type = 1 AND used = 0;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return 0;

    sqlite3_bind_text(stmt, 1, account.c_str(), -1, SQLITE_TRANSIENT);

    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return count;
}

bool SignalKeyRepo::upsertPeerPrekey(const std::string& account, const std::string& peerEmail,
                                     const std::string& ikPub, const std::string& spkPub,
                                     const std::string& spkSig, const std::string& opkPub,
                                     const std::string& keyScope) {
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db) return false;

    const char* sql = "INSERT INTO signal_peer_prekey (account, peer_email, ik_pub, spk_pub, spk_sig, opk_pub, version, session_uuid) "
                      "VALUES (?, ?, ?, ?, ?, ?, 1, ?) "
                      "ON CONFLICT(account, peer_email) DO UPDATE SET "
                      "ik_pub=excluded.ik_pub, spk_pub=excluded.spk_pub, spk_sig=excluded.spk_sig, "
                      "opk_pub=excluded.opk_pub, version=excluded.version, session_uuid=excluded.session_uuid, "
                      "received_at=datetime('now','localtime');";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, account.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, peerEmail.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, ikPub.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, spkPub.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, spkSig.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, opkPub.c_str(), -1, SQLITE_TRANSIENT);
    if (keyScope.empty()) {
        sqlite3_bind_null(stmt, 7);
    } else {
        sqlite3_bind_text(stmt, 7, keyScope.c_str(), -1, SQLITE_TRANSIENT);
    }

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool SignalKeyRepo::loadPeerPrekey(const std::string& account, const std::string& peerEmail,
                                   SignalPeerPrekeyRecord& out) {
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db) return false;

    const char* sql = "SELECT id, account, peer_email, ik_pub, spk_pub, spk_sig, opk_pub, version, session_uuid "
                      "FROM signal_peer_prekey WHERE account = ? AND peer_email = ? LIMIT 1;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, account.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, peerEmail.c_str(), -1, SQLITE_TRANSIENT);

    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        out.id = sqlite3_column_int64(stmt, 0);
        out.account = (const char*)sqlite3_column_text(stmt, 1);
        out.peerEmail = (const char*)sqlite3_column_text(stmt, 2);
        out.ikPub = (const char*)sqlite3_column_text(stmt, 3);
        out.spkPub = (const char*)sqlite3_column_text(stmt, 4);
        out.spkSig = sqlite3_column_text(stmt, 5) ? (const char*)sqlite3_column_text(stmt, 5) : "";
        out.opkPub = sqlite3_column_text(stmt, 6) ? (const char*)sqlite3_column_text(stmt, 6) : "";
        out.version = sqlite3_column_int(stmt, 7);
        out.sessionUuid = sqlite3_column_text(stmt, 8) ? (const char*)sqlite3_column_text(stmt, 8) : "";
        found = true;
    }
    sqlite3_finalize(stmt);
    return found;
}

// ---------------------------------------------------------------------------
// Per-session helpers (sessionUuid used as logical account key)
// ---------------------------------------------------------------------------

bool SignalKeyRepo::upsertIdentityForSession(const std::string& sessionUuid,
                                             const std::string& ikPub,
                                             const std::string& ikPriv,
                                             const std::string& ikPassword) {
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db) return false;

    const char* sql =
        "INSERT INTO signal_identity (account, ik_pub, ik_priv, ik_password, session_uuid) "
        "VALUES (?, ?, ?, ?, ?) "
        "ON CONFLICT(account) DO UPDATE SET ik_pub=excluded.ik_pub, ik_priv=excluded.ik_priv, "
        "ik_password=excluded.ik_password, session_uuid=excluded.session_uuid;";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, sessionUuid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, ikPub.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, ikPriv.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, ikPassword.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, sessionUuid.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool SignalKeyRepo::loadIdentityForSession(const std::string& sessionUuid, SignalIdentityRecord& out) {
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db) return false;

    const char* sql =
        "SELECT id, account, ik_pub, ik_priv, ik_password, session_uuid "
        "FROM signal_identity WHERE account = ? LIMIT 1;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, sessionUuid.c_str(), -1, SQLITE_TRANSIENT);

    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        out.id = sqlite3_column_int64(stmt, 0);
        out.account = (const char*)sqlite3_column_text(stmt, 1);
        out.ikPub = (const char*)sqlite3_column_text(stmt, 2);
        out.ikPriv = (const char*)sqlite3_column_text(stmt, 3);
        out.ikPassword = sqlite3_column_text(stmt, 4) ? (const char*)sqlite3_column_text(stmt, 4) : "";
        out.sessionUuid = sqlite3_column_text(stmt, 5) ? (const char*)sqlite3_column_text(stmt, 5) : "";
        found = true;
    }
    sqlite3_finalize(stmt);
    return found;
}

bool SignalKeyRepo::loadIdentityByPub(const std::string& pubPem, SignalIdentityRecord& out) {
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db || pubPem.empty()) return false;

    const char* sql =
        "SELECT id, account, ik_pub, ik_priv, ik_password, session_uuid "
        "FROM signal_identity WHERE ik_pub = ? LIMIT 1;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, pubPem.c_str(), -1, SQLITE_TRANSIENT);

    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        out.id = sqlite3_column_int64(stmt, 0);
        out.account = (const char*)sqlite3_column_text(stmt, 1);
        out.ikPub = (const char*)sqlite3_column_text(stmt, 2);
        out.ikPriv = (const char*)sqlite3_column_text(stmt, 3);
        out.ikPassword = sqlite3_column_text(stmt, 4) ? (const char*)sqlite3_column_text(stmt, 4) : "";
        out.sessionUuid = sqlite3_column_text(stmt, 5) ? (const char*)sqlite3_column_text(stmt, 5) : "";
        found = true;
    }
    sqlite3_finalize(stmt);
    return found;
}

bool SignalKeyRepo::insertSignedPrekeyForSession(const std::string& sessionUuid,
                                                 const std::string& pub,
                                                 const std::string& priv,
                                                 const std::string& password,
                                                 const std::string& signature) {
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db) return false;

    sqlite3_exec(db, "BEGIN;", NULL, NULL, NULL);

    const char* delSql = "DELETE FROM signal_prekey WHERE account = ? AND key_type = 0;";
    sqlite3_stmt* delStmt;
    if (sqlite3_prepare_v2(db, delSql, -1, &delStmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(delStmt, 1, sessionUuid.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(delStmt);
        sqlite3_finalize(delStmt);
    }

    const char* sql =
        "INSERT INTO signal_prekey (account, key_type, pub, priv, password, signature, used, session_uuid) "
        "VALUES (?, 0, ?, ?, ?, ?, 0, ?);";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
        return false;
    }

    sqlite3_bind_text(stmt, 1, sessionUuid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, pub.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, priv.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, password.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, signature.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, sessionUuid.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc == SQLITE_DONE) {
        sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL);
        return true;
    } else {
        sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
        return false;
    }
}

bool SignalKeyRepo::loadSignedPrekeyForSession(const std::string& sessionUuid, SignalPrekeyRecord& out) {
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db) return false;

    const char* sql =
        "SELECT id, account, key_type, pub, priv, password, signature, used, session_uuid "
        "FROM signal_prekey WHERE account = ? AND key_type = 0 "
        "ORDER BY id DESC LIMIT 1;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, sessionUuid.c_str(), -1, SQLITE_TRANSIENT);

    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        out.id = sqlite3_column_int64(stmt, 0);
        out.account = (const char*)sqlite3_column_text(stmt, 1);
        out.keyType = sqlite3_column_int(stmt, 2);
        out.pub = (const char*)sqlite3_column_text(stmt, 3);
        out.priv = (const char*)sqlite3_column_text(stmt, 4);
        out.password = sqlite3_column_text(stmt, 5) ? (const char*)sqlite3_column_text(stmt, 5) : "";
        out.signature = sqlite3_column_text(stmt, 6) ? (const char*)sqlite3_column_text(stmt, 6) : "";
        out.used = sqlite3_column_int(stmt, 7);
        out.sessionUuid = sqlite3_column_text(stmt, 8) ? (const char*)sqlite3_column_text(stmt, 8) : "";
        found = true;
    }
    sqlite3_finalize(stmt);
    return found;
}

bool SignalKeyRepo::loadSignedPrekeyByPub(const std::string& pubPem, SignalPrekeyRecord& out) {
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db || pubPem.empty()) return false;

    const char* sql =
        "SELECT id, account, key_type, pub, priv, password, signature, used, session_uuid "
        "FROM signal_prekey WHERE pub = ? AND key_type = 0 "
        "ORDER BY id DESC LIMIT 1;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, pubPem.c_str(), -1, SQLITE_TRANSIENT);

    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        out.id = sqlite3_column_int64(stmt, 0);
        out.account = (const char*)sqlite3_column_text(stmt, 1);
        out.keyType = sqlite3_column_int(stmt, 2);
        out.pub = (const char*)sqlite3_column_text(stmt, 3);
        out.priv = (const char*)sqlite3_column_text(stmt, 4);
        out.password = sqlite3_column_text(stmt, 5) ? (const char*)sqlite3_column_text(stmt, 5) : "";
        out.signature = sqlite3_column_text(stmt, 6) ? (const char*)sqlite3_column_text(stmt, 6) : "";
        out.used = sqlite3_column_int(stmt, 7);
        out.sessionUuid = sqlite3_column_text(stmt, 8) ? (const char*)sqlite3_column_text(stmt, 8) : "";
        found = true;
    }
    sqlite3_finalize(stmt);
    return found;
}

bool SignalKeyRepo::insertOneTimePrekeyForSession(const std::string& sessionUuid,
                                                  const std::string& pub,
                                                  const std::string& priv,
                                                  const std::string& password) {
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db) return false;

    const char* sql =
        "INSERT INTO signal_prekey (account, key_type, pub, priv, password, used, session_uuid) "
        "VALUES (?, 1, ?, ?, ?, 0, ?);";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, sessionUuid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, pub.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, priv.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, password.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, sessionUuid.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool SignalKeyRepo::claimOneTimePrekeyForSession(const std::string& sessionUuid, SignalPrekeyRecord& out) {
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db) return false;

    const char* findSql =
        "SELECT id, account, key_type, pub, priv, password, used, session_uuid "
        "FROM signal_prekey WHERE account = ? AND key_type = 1 AND used = 0 "
        "ORDER BY id ASC LIMIT 1;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, findSql, -1, &stmt, NULL) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, sessionUuid.c_str(), -1, SQLITE_TRANSIENT);

    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        out.id = sqlite3_column_int64(stmt, 0);
        out.account = (const char*)sqlite3_column_text(stmt, 1);
        out.keyType = sqlite3_column_int(stmt, 2);
        out.pub = (const char*)sqlite3_column_text(stmt, 3);
        out.priv = (const char*)sqlite3_column_text(stmt, 4);
        out.password = sqlite3_column_text(stmt, 5) ? (const char*)sqlite3_column_text(stmt, 5) : "";
        out.signature = "";
        out.used = sqlite3_column_int(stmt, 6);
        out.sessionUuid = sqlite3_column_text(stmt, 7) ? (const char*)sqlite3_column_text(stmt, 7) : "";
        found = true;
    }
    sqlite3_finalize(stmt);

    if (!found) return false;

    const char* updateSql = "UPDATE signal_prekey SET used = 1 WHERE id = ?;";
    if (sqlite3_prepare_v2(db, updateSql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, out.id);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    return true;
}

bool SignalKeyRepo::upsertPeerPrekeyForSession(const std::string& sessionUuid,
                                               const std::string& peerEmail,
                                               const std::string& ikPub,
                                               const std::string& spkPub,
                                               const std::string& spkSig,
                                               const std::string& opkPub) {
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db) return false;

    const char* sql =
        "INSERT INTO signal_peer_prekey (account, peer_email, ik_pub, spk_pub, spk_sig, opk_pub, version, session_uuid) "
        "VALUES (?, ?, ?, ?, ?, ?, 1, ?) "
        "ON CONFLICT(account, peer_email) DO UPDATE SET "
        "ik_pub=excluded.ik_pub, spk_pub=excluded.spk_pub, spk_sig=excluded.spk_sig, "
        "opk_pub=excluded.opk_pub, version=excluded.version, session_uuid=excluded.session_uuid, "
        "received_at=datetime('now','localtime');";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, sessionUuid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, peerEmail.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, ikPub.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, spkPub.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, spkSig.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, opkPub.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, sessionUuid.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool SignalKeyRepo::loadPeerPrekeyForSession(const std::string& sessionUuid,
                                             const std::string& peerEmail,
                                             SignalPeerPrekeyRecord& out) {
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db) return false;

    const char* sql =
        "SELECT id, account, peer_email, ik_pub, spk_pub, spk_sig, opk_pub, version, session_uuid "
        "FROM signal_peer_prekey WHERE account = ? AND peer_email = ? LIMIT 1;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, sessionUuid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, peerEmail.c_str(), -1, SQLITE_TRANSIENT);

    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        out.id = sqlite3_column_int64(stmt, 0);
        out.account = (const char*)sqlite3_column_text(stmt, 1);
        out.peerEmail = (const char*)sqlite3_column_text(stmt, 2);
        out.ikPub = (const char*)sqlite3_column_text(stmt, 3);
        out.spkPub = (const char*)sqlite3_column_text(stmt, 4);
        out.spkSig = sqlite3_column_text(stmt, 5) ? (const char*)sqlite3_column_text(stmt, 5) : "";
        out.opkPub = sqlite3_column_text(stmt, 6) ? (const char*)sqlite3_column_text(stmt, 6) : "";
        out.version = sqlite3_column_int(stmt, 7);
        out.sessionUuid = sqlite3_column_text(stmt, 8) ? (const char*)sqlite3_column_text(stmt, 8) : "";
        found = true;
    }
    sqlite3_finalize(stmt);
    return found;
}
