#include "signal_session_repo.h"
#include "db_connection.h"
#include "logger.h"
#include <sqlite3.h>

bool SignalSessionRepo::saveSession(const SignalSessionRecord& rec) {
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db) return false;

    const char* sql = "INSERT INTO signal_session "
                      "(account, peer_email, session_id, root_key, send_chain_key, recv_chain_key, "
                      "send_n, recv_n, prev_recv_n, dh_self_priv, dh_self_pub, dh_self_password, "
                      "dh_peer_pub, status, updated_at) "
                      "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, datetime('now','localtime')) "
                      "ON CONFLICT(account, peer_email, session_id) DO UPDATE SET "
                      "root_key=excluded.root_key, send_chain_key=excluded.send_chain_key, "
                      "recv_chain_key=excluded.recv_chain_key, send_n=excluded.send_n, "
                      "recv_n=excluded.recv_n, prev_recv_n=excluded.prev_recv_n, "
                      "dh_self_priv=excluded.dh_self_priv, dh_self_pub=excluded.dh_self_pub, "
                      "dh_self_password=excluded.dh_self_password, dh_peer_pub=excluded.dh_peer_pub, "
                      "status=excluded.status, updated_at=datetime('now','localtime');";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, rec.account.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, rec.peerEmail.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, rec.sessionId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, rec.rootKey.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, rec.sendChainKey.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, rec.recvChainKey.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 7, rec.sendN);
    sqlite3_bind_int(stmt, 8, rec.recvN);
    sqlite3_bind_int(stmt, 9, rec.prevRecvN);
    sqlite3_bind_text(stmt, 10, rec.dhSelfPriv.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 11, rec.dhSelfPub.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 12, rec.dhSelfPassword.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 13, rec.dhPeerPub.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 14, rec.status);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

std::string SignalSessionRepo::getPeerEmail(const std::string& account, const std::string& sessionId) {
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db || sessionId.empty()) return "";

    const char* sql = "SELECT peer_email FROM signal_session "
                      "WHERE account = ? AND session_id = ? AND status = 0 LIMIT 1;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return "";

    sqlite3_bind_text(stmt, 1, account.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, sessionId.c_str(), -1, SQLITE_TRANSIENT);

    std::string peerEmail;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* p = (const char*)sqlite3_column_text(stmt, 0);
        if (p) peerEmail = p;
    }
    sqlite3_finalize(stmt);
    return peerEmail;
}

bool SignalSessionRepo::loadSession(const std::string& account, const std::string& peerEmail,
                                    const std::string& sessionId, SignalSessionRecord& out) {
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db) return false;

    const char* sql = "SELECT id, account, peer_email, session_id, root_key, send_chain_key, "
                      "recv_chain_key, send_n, recv_n, prev_recv_n, dh_self_priv, dh_self_pub, "
                      "dh_self_password, dh_peer_pub, status "
                      "FROM signal_session WHERE account = ? AND peer_email = ? AND session_id = ? LIMIT 1;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, account.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, peerEmail.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, sessionId.c_str(), -1, SQLITE_TRANSIENT);

    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        out.id = sqlite3_column_int64(stmt, 0);
        out.account = (const char*)sqlite3_column_text(stmt, 1);
        out.peerEmail = (const char*)sqlite3_column_text(stmt, 2);
        out.sessionId = (const char*)sqlite3_column_text(stmt, 3);
        out.rootKey = (const char*)sqlite3_column_text(stmt, 4);
        out.sendChainKey = sqlite3_column_text(stmt, 5) ? (const char*)sqlite3_column_text(stmt, 5) : "";
        out.recvChainKey = sqlite3_column_text(stmt, 6) ? (const char*)sqlite3_column_text(stmt, 6) : "";
        out.sendN = sqlite3_column_int(stmt, 7);
        out.recvN = sqlite3_column_int(stmt, 8);
        out.prevRecvN = sqlite3_column_int(stmt, 9);
        out.dhSelfPriv = sqlite3_column_text(stmt, 10) ? (const char*)sqlite3_column_text(stmt, 10) : "";
        out.dhSelfPub = sqlite3_column_text(stmt, 11) ? (const char*)sqlite3_column_text(stmt, 11) : "";
        out.dhSelfPassword = sqlite3_column_text(stmt, 12) ? (const char*)sqlite3_column_text(stmt, 12) : "";
        out.dhPeerPub = sqlite3_column_text(stmt, 13) ? (const char*)sqlite3_column_text(stmt, 13) : "";
        out.status = sqlite3_column_int(stmt, 14);
        found = true;
    }
    sqlite3_finalize(stmt);
    return found;
}

bool SignalSessionRepo::loadActiveSession(const std::string& account, const std::string& peerEmail,
                                          SignalSessionRecord& out) {
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db) return false;

    const char* sql = "SELECT id, account, peer_email, session_id, root_key, send_chain_key, "
                      "recv_chain_key, send_n, recv_n, prev_recv_n, dh_self_priv, dh_self_pub, "
                      "dh_self_password, dh_peer_pub, status "
                      "FROM signal_session WHERE account = ? AND peer_email = ? AND status = 0 "
                      "ORDER BY updated_at DESC LIMIT 1;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, account.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, peerEmail.c_str(), -1, SQLITE_TRANSIENT);

    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        out.id = sqlite3_column_int64(stmt, 0);
        out.account = (const char*)sqlite3_column_text(stmt, 1);
        out.peerEmail = (const char*)sqlite3_column_text(stmt, 2);
        out.sessionId = (const char*)sqlite3_column_text(stmt, 3);
        out.rootKey = (const char*)sqlite3_column_text(stmt, 4);
        out.sendChainKey = sqlite3_column_text(stmt, 5) ? (const char*)sqlite3_column_text(stmt, 5) : "";
        out.recvChainKey = sqlite3_column_text(stmt, 6) ? (const char*)sqlite3_column_text(stmt, 6) : "";
        out.sendN = sqlite3_column_int(stmt, 7);
        out.recvN = sqlite3_column_int(stmt, 8);
        out.prevRecvN = sqlite3_column_int(stmt, 9);
        out.dhSelfPriv = sqlite3_column_text(stmt, 10) ? (const char*)sqlite3_column_text(stmt, 10) : "";
        out.dhSelfPub = sqlite3_column_text(stmt, 11) ? (const char*)sqlite3_column_text(stmt, 11) : "";
        out.dhSelfPassword = sqlite3_column_text(stmt, 12) ? (const char*)sqlite3_column_text(stmt, 12) : "";
        out.dhPeerPub = sqlite3_column_text(stmt, 13) ? (const char*)sqlite3_column_text(stmt, 13) : "";
        out.status = sqlite3_column_int(stmt, 14);
        found = true;
    }
    sqlite3_finalize(stmt);
    return found;
}

bool SignalSessionRepo::sessionExists(const std::string& account, const std::string& peerEmail,
                                      const std::string& sessionId) {
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db) return false;

    const char* sql = "SELECT 1 FROM signal_session WHERE account = ? AND peer_email = ? AND session_id = ? LIMIT 1;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, account.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, peerEmail.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, sessionId.c_str(), -1, SQLITE_TRANSIENT);

    bool exists = (sqlite3_step(stmt) == SQLITE_ROW);
    sqlite3_finalize(stmt);
    return exists;
}

bool SignalSessionRepo::hasReceivedBySessionId(const std::string& account, const std::string& sessionId) {
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db || sessionId.empty()) return false;

    const char* sql = "SELECT recv_n, recv_chain_key FROM signal_session "
                      "WHERE account = ? AND session_id = ? AND status = 0 LIMIT 1;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, account.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, sessionId.c_str(), -1, SQLITE_TRANSIENT);

    bool hasReceived = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        int recvN = sqlite3_column_int(stmt, 0);
        const char* recvKey = (const char*)sqlite3_column_text(stmt, 1);
        hasReceived = (recvN > 0) || (recvKey && recvKey[0] != '\0');
    }
    sqlite3_finalize(stmt);
    return hasReceived;
}

bool SignalSessionRepo::canSendBySessionId(const std::string& account, const std::string& sessionId) {
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db || sessionId.empty()) return false;

    const char* sql = "SELECT send_n, recv_n FROM signal_session "
                      "WHERE account = ? AND session_id = ? AND status = 0 LIMIT 1;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, account.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, sessionId.c_str(), -1, SQLITE_TRANSIENT);

    bool canSend = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        int sendN = sqlite3_column_int(stmt, 0);
        int recvN = sqlite3_column_int(stmt, 1);
        // Can send if: haven't sent yet (send_n == 0, e.g. responder after receiving init)
        //          OR  peer has responded (recv_n > 0)
        canSend = (sendN == 0) || (recvN > 0);
    }
    sqlite3_finalize(stmt);
    return canSend;
}

bool SignalSessionRepo::markReceivedBySessionId(const std::string& account, const std::string& sessionId) {
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db || sessionId.empty()) return false;

    const char* sql = "UPDATE signal_session SET recv_n = CASE WHEN recv_n < 1 THEN 1 ELSE recv_n END "
                      "WHERE account = ? AND session_id = ? AND status = 0;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, account.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, sessionId.c_str(), -1, SQLITE_TRANSIENT);

    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

bool SignalSessionRepo::closeSession(const std::string& account, const std::string& peerEmail,
                                     const std::string& sessionId) {
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db) return false;

    const char* sql = "UPDATE signal_session SET status = 1, updated_at = datetime('now','localtime') "
                      "WHERE account = ? AND peer_email = ? AND session_id = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, account.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, peerEmail.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, sessionId.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool SignalSessionRepo::insertSkippedKey(const std::string& sessionId, const std::string& peerDhPub,
                                         int msgIndex, const std::string& messageKey) {
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db) return false;

    const char* sql = "INSERT OR REPLACE INTO signal_skipped_key (session_id, peer_dh_pub, msg_index, message_key) "
                      "VALUES (?, ?, ?, ?);";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, sessionId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, peerDhPub.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, msgIndex);
    sqlite3_bind_text(stmt, 4, messageKey.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool SignalSessionRepo::findSkippedKey(const std::string& sessionId, const std::string& peerDhPub,
                                       int msgIndex, SignalSkippedKeyRecord& out) {
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db) return false;

    const char* sql = "SELECT id, session_id, peer_dh_pub, msg_index, message_key "
                      "FROM signal_skipped_key WHERE session_id = ? AND peer_dh_pub = ? AND msg_index = ? LIMIT 1;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, sessionId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, peerDhPub.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, msgIndex);

    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        out.id = sqlite3_column_int64(stmt, 0);
        out.sessionId = (const char*)sqlite3_column_text(stmt, 1);
        out.peerDhPub = (const char*)sqlite3_column_text(stmt, 2);
        out.msgIndex = sqlite3_column_int(stmt, 3);
        out.messageKey = (const char*)sqlite3_column_text(stmt, 4);
        found = true;
    }
    sqlite3_finalize(stmt);
    return found;
}

bool SignalSessionRepo::deleteSkippedKey(const std::string& sessionId, const std::string& peerDhPub,
                                         int msgIndex) {
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db) return false;

    const char* sql = "DELETE FROM signal_skipped_key WHERE session_id = ? AND peer_dh_pub = ? AND msg_index = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, sessionId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, peerDhPub.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, msgIndex);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

int SignalSessionRepo::countSkippedKeys(const std::string& sessionId) {
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db) return 0;

    const char* sql = "SELECT COUNT(*) FROM signal_skipped_key WHERE session_id = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return 0;

    sqlite3_bind_text(stmt, 1, sessionId.c_str(), -1, SQLITE_TRANSIENT);

    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return count;
}
