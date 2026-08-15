#include "email_core_common.h"
#include "email_core.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <sstream>
#include <chrono>

using json = nlohmann::json;

// Generate session records for existing emails
// Now a no-op: sessions are only created via X-Start-New=new in download_pending_bodies
extern "C" int email_generate_sessions(const char* account, char* outJson, int outSize) {
    if (outJson && outSize > 0) {
        snprintf(outJson, outSize, R"({"status":"success","message":"no-op"})");
    }
    return 0;
}

// Create a user-defined session (auto=0) with message_id as index_uuid
extern "C" int email_create_session(const char* account, const char* subject, const char* members, const char* message_id, int encrypt_method, char* outJson, int outSize) {
    if (!g_db) {
        if (outJson && outSize > 0) {
            snprintf(outJson, outSize, R"({"status":"failed","error":"database_not_initialized"})");
        }
        return -1;
    }

    if (!account || !*account) {
        if (outJson && outSize > 0) {
            snprintf(outJson, outSize, R"({"status":"failed","error":"account_is_empty"})");
        }
        return -1;
    }

    LOG_INFO("[DB] email_create_session called with account: '%s', subject: '%s', members: '%s', message_id: '%s', encrypt_method: %d\n",
             account ? account : "null", subject ? subject : "null", members ? members : "null", message_id ? message_id : "null", encrypt_method);

    // Generate a unique session_id without inserting into session table
    // The session row will be created later by email_add_email_to_session
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    std::string session_id = "session_" + std::to_string(ms) + "_" + std::to_string(rand() % 10000);
    LOG_INFO("[DB] email_create_session: generated session_id=%s\n", session_id.c_str());

    if (encrypt_method == 1) {
        std::string password = generate_random_password(32);
        std::string pubPem, privPem;
        if (!generate_ecc_keypair(pubPem, privPem, password)) {
            LOG_INFO("[DB] email_create_session: ECC key generation failed\n");
            if (outJson && outSize > 0) {
                snprintf(outJson, outSize, R"({"status":"failed","error":"ecc_keygen_failed"})");
            }
            return -1;
        }

        // Store keypair in keyinfo with session_id=0 (will be updated when session row is created)
        const char* keyinfo_sql = "INSERT INTO keyinfo (pub, key, password, session_id, account) VALUES (?, ?, ?, 0, ?);";
        sqlite3_stmt* keyinfo_stmt;
        if (sqlite3_prepare_v2(g_db, keyinfo_sql, -1, &keyinfo_stmt, NULL) == SQLITE_OK) {
            sqlite3_bind_text(keyinfo_stmt, 1, pubPem.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(keyinfo_stmt, 2, privPem.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(keyinfo_stmt, 3, password.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(keyinfo_stmt, 4, account ? account : "", -1, SQLITE_STATIC);
            if (sqlite3_step(keyinfo_stmt) != SQLITE_DONE) {
                LOG_INFO("[DB] email_create_session: keyinfo insert failed: %s\n", sqlite3_errmsg(g_db));
                sqlite3_finalize(keyinfo_stmt);
                if (outJson && outSize > 0) {
                    snprintf(outJson, outSize, R"({"status":"failed","error":"keyinfo_insert_failed"})");
                }
                return -1;
            }
            sqlite3_finalize(keyinfo_stmt);
            LOG_INFO("[DB] email_create_session: ECC key pair generated and inserted into keyinfo\n");
        } else {
            LOG_INFO("[DB] email_create_session: keyinfo prepare failed\n");
            if (outJson && outSize > 0) {
                snprintf(outJson, outSize, R"({"status":"failed","error":"keyinfo_prepare_failed"})");
            }
            return -1;
        }

        // Generate 12-char session password and sign it with the private key
        std::string sessionPassword = generate_random_password(12);
        std::string secretKey = sign_with_ecc_private_key(privPem, password, sessionPassword);
        if (secretKey.empty()) {
            LOG_INFO("[DB] email_create_session: failed to sign session password\n");
            if (outJson && outSize > 0) {
                snprintf(outJson, outSize, R"({"status":"failed","error":"sign_session_password_failed"})");
            }
            return -1;
        }
        LOG_INFO("[DB] email_create_session: session password signed, secretkey length=%zu\n", secretKey.size());

        // Build response with pubkey and secretkey
        if (outJson && outSize > 0) {
            json resp;
            resp["status"] = "success";
            resp["session_id"] = session_id;
            resp["pubkey"] = pubPem;
            resp["secretkey"] = secretKey;
            resp["session_password"] = sessionPassword;
            std::string jsonStr = resp.dump();
            if ((int)jsonStr.size() >= outSize) {
                // Response too large, try without pubkey
                resp.erase("pubkey");
                jsonStr = resp.dump();
            }
            snprintf(outJson, outSize, "%s", jsonStr.c_str());
        }
        return 0;
    }

    json response;
    response["status"] = "success";
    response["session_id"] = session_id;

    std::string jsonStr = response.dump();
    if (outJson && outSize > 0) {
        snprintf(outJson, outSize, "%s", jsonStr.c_str());
    }
    return 0;
}

// Insert a sent email record into localemail with a pending uuid
extern "C" int email_insert_sent_email(const char* account, const char* sender, const char* from_addr, const char* to_addr, const char* subject, const char* date, const char* message_id, const char* in_reply_to, const char* body, const char* storageDir, char* outJson, int outSize) {
    if (!g_db) {
        if (outJson && outSize > 0) {
            snprintf(outJson, outSize, R"({"status":"failed","error":"database_not_initialized"})");
        }
        return -1;
    }

    LOG_INFO("[DB] email_insert_sent_email: account='%s', message_id='%s', subject='%s'\n",
             account ? account : "null", message_id ? message_id : "null", subject ? subject : "null");

    std::string existing_uuid;
    if (message_id && *message_id) {
        const char* check_sql = "SELECT uuid FROM localemail WHERE message_id = ? AND account = ? LIMIT 1;";
        sqlite3_stmt* check_stmt;
        if (sqlite3_prepare_v2(g_db, check_sql, -1, &check_stmt, NULL) == SQLITE_OK) {
            sqlite3_bind_text(check_stmt, 1, message_id ? message_id : "", -1, SQLITE_STATIC);
            sqlite3_bind_text(check_stmt, 2, account ? account : "", -1, SQLITE_STATIC);
            if (sqlite3_step(check_stmt) == SQLITE_ROW) {
                const char* val = (const char*)sqlite3_column_text(check_stmt, 0);
                if (val) existing_uuid = val;
            }
            sqlite3_finalize(check_stmt);
        }
    }

    if (!existing_uuid.empty()) {
        const char* find_rowid_sql = "SELECT id FROM localemail WHERE uuid = ? LIMIT 1;";
        sqlite3_stmt* find_rowid_stmt;
        std::string rowidStr;
        if (sqlite3_prepare_v2(g_db, find_rowid_sql, -1, &find_rowid_stmt, NULL) == SQLITE_OK) {
            sqlite3_bind_text(find_rowid_stmt, 1, existing_uuid.c_str(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(find_rowid_stmt) == SQLITE_ROW) {
                rowidStr = std::to_string(sqlite3_column_int64(find_rowid_stmt, 0));
            }
            sqlite3_finalize(find_rowid_stmt);
        }
        LOG_INFO("[DB] email_insert_sent_email: already exists with uuid='%s', rowid='%s'\n", existing_uuid.c_str(), rowidStr.c_str());
        if (outJson && outSize > 0) {
            json response;
            response["status"] = "success";
            response["uuid"] = rowidStr;
            response["exists"] = true;
            std::string jsonStr = response.dump();
            snprintf(outJson, outSize, "%s", jsonStr.c_str());
        }
        return 0;
    }

    std::string pending_uuid = "0";

    json bs;
    bs["type"] = "text";
    bs["subtype"] = "plain";
    bs["size"] = body ? strlen(body) : 0;
    bs["encoding"] = "7bit";
    std::string bodystructureStr = bs.dump();

    std::string filename = message_id ? message_id : "";
    size_t start = filename.find('<');
    size_t end = filename.find('>');
    if (start != std::string::npos && end != std::string::npos && end > start) {
        filename = filename.substr(start + 1, end - start - 1);
    }

    const char* insert_sql = "INSERT INTO localemail "
                             "(uuid, account, sender, from_addr, to_addr, subject, date, bodystructure, reply_to, in_reply_to, message_id, flags, folder, islocal, servicerecvtime, file) "
                             "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, '[]', 'INBOX', 0, ?, ?);";
    sqlite3_stmt* insert_stmt;
    if (sqlite3_prepare_v2(g_db, insert_sql, -1, &insert_stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(insert_stmt, 1, pending_uuid.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(insert_stmt, 2, account ? account : "", -1, SQLITE_STATIC);
        sqlite3_bind_text(insert_stmt, 3, sender ? sender : "", -1, SQLITE_STATIC);
        sqlite3_bind_text(insert_stmt, 4, from_addr ? from_addr : "", -1, SQLITE_STATIC);
        sqlite3_bind_text(insert_stmt, 5, to_addr ? to_addr : "", -1, SQLITE_STATIC);
        sqlite3_bind_text(insert_stmt, 6, subject ? subject : "", -1, SQLITE_STATIC);
        sqlite3_bind_text(insert_stmt, 7, date ? date : "", -1, SQLITE_STATIC);
        sqlite3_bind_text(insert_stmt, 8, bodystructureStr.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(insert_stmt, 9, in_reply_to ? in_reply_to : "", -1, SQLITE_STATIC);
        sqlite3_bind_text(insert_stmt, 10, in_reply_to ? in_reply_to : "", -1, SQLITE_STATIC);
        sqlite3_bind_text(insert_stmt, 11, message_id ? message_id : "", -1, SQLITE_STATIC);
        sqlite3_bind_text(insert_stmt, 12, date ? date : "", -1, SQLITE_STATIC);
        sqlite3_bind_text(insert_stmt, 13, filename.c_str(), -1, SQLITE_STATIC);
        sqlite3_step(insert_stmt);
        sqlite3_finalize(insert_stmt);
        int64_t rowid = sqlite3_last_insert_rowid(g_db);
        std::string rowidStr = std::to_string(rowid);
        LOG_INFO("[DB] email_insert_sent_email: inserted with pending uuid='%s', id='%s', file='%s'\n", pending_uuid.c_str(), rowidStr.c_str(), filename.c_str());

        if (storageDir && account && message_id && body) {
            std::string accountStr(account);
            std::string messageIdStr(message_id);
            std::string storageDirStr(storageDir);

            std::string accountDir = storageDirStr + "/" + accountStr;
            std::filesystem::create_directories(accountDir);

            std::string filename = messageIdStr;
            size_t start = filename.find('<');
            size_t end = filename.find('>');
            if (start != std::string::npos && end != std::string::npos && end > start) {
                filename = filename.substr(start + 1, end - start - 1);
            }

            std::string filePath = accountDir + "/" + filename + ".eml";
            std::ofstream emlFile(filePath);
            if (emlFile.is_open()) {
                emlFile << "Message-ID: " << messageIdStr << "\n";
                emlFile << "X-Message-ID: " << messageIdStr << "\n";
                emlFile << "From: " << (from_addr ? from_addr : "") << "\n";
                emlFile << "To: " << (to_addr ? to_addr : "") << "\n";
                emlFile << "Subject: " << (subject ? subject : "") << "\n";
                emlFile << "Date: " << (date ? date : "") << "\n";
                if (in_reply_to && *in_reply_to) {
                    emlFile << "In-Reply-To: " << in_reply_to << "\n";
                }
                emlFile << "\n";
                emlFile << (body ? body : "");
                emlFile.close();
                LOG_INFO("[DB] email_insert_sent_email: saved to %s\n", filePath.c_str());
            }
        }

        if (outJson && outSize > 0) {
            json response;
            response["status"] = "success";
            response["uuid"] = rowidStr;
            std::string jsonStr = response.dump();
            snprintf(outJson, outSize, "%s", jsonStr.c_str());
        }
        return 0;
    }

    return 0;
}

extern "C" int email_query_session_by_message_id(const char* messageId, const char* account, char* outSessionId, int outSize) {
    if (!g_db || !messageId || !*messageId) {
        if (outSessionId && outSize > 0) outSessionId[0] = '\0';
        return -1;
    }

    const char* sql =
        "SELECT s.session_id FROM session s "
        "INNER JOIN localemail l ON l.id = s.email_id "
        "WHERE l.message_id = ? AND l.account = ? LIMIT 1;";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        if (outSessionId && outSize > 0) outSessionId[0] = '\0';
        return -2;
    }
    sqlite3_bind_text(stmt, 1, messageId, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, account ? account : "", -1, SQLITE_STATIC);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* sid = (const char*)sqlite3_column_text(stmt, 0);
        if (sid && outSessionId && outSize > 0) {
            snprintf(outSessionId, outSize, "%s", sid);
        }
    } else {
        if (outSessionId && outSize > 0) outSessionId[0] = '\0';
    }
    sqlite3_finalize(stmt);
    return 0;
}

extern "C" int email_add_email_to_session(const char* sessionId, const char* uuid, const char* account, int encrypt_method, char* outJson, int outSize) {
    if (!g_db) {
        if (outJson && outSize > 0) {
            snprintf(outJson, outSize, R"({"status":"failed","error":"database_not_initialized"})");
        }
        return -1;
    }

    LOG_INFO("[DB] email_add_email_to_session: session_id='%s', uuid='%s', account='%s', encrypt_method=%d\n",
             sessionId ? sessionId : "null", uuid ? uuid : "null", account ? account : "null", encrypt_method);

    if (!uuid || !*uuid) {
        if (outJson && outSize > 0) {
            snprintf(outJson, outSize, R"({"status":"failed","error":"empty_uuid"})");
        }
        return -2;
    }

    int64_t emailId = 0;
    if (uuid && *uuid) {
        emailId = std::stoll(std::string(uuid));
    }

    const char* check_sql = "SELECT id, auto FROM session WHERE email_id = ?;";
    sqlite3_stmt* check_stmt;
    bool exists = false;
    int existingId = 0;
    if (sqlite3_prepare_v2(g_db, check_sql, -1, &check_stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int64(check_stmt, 1, emailId);
        if (sqlite3_step(check_stmt) == SQLITE_ROW) {
            exists = true;
            existingId = sqlite3_column_int(check_stmt, 0);
            int existingAuto = sqlite3_column_int(check_stmt, 1);
        }
        sqlite3_finalize(check_stmt);
    }

    if (exists) {
        const char* update_sql = "UPDATE session SET session_id = ?, auto = 1, encrypt_method = ? WHERE email_id = ?;";
        sqlite3_stmt* update_stmt;
        if (sqlite3_prepare_v2(g_db, update_sql, -1, &update_stmt, NULL) == SQLITE_OK) {
            sqlite3_bind_text(update_stmt, 1, sessionId ? sessionId : "", -1, SQLITE_STATIC);
            sqlite3_bind_int(update_stmt, 2, encrypt_method);
            sqlite3_bind_int64(update_stmt, 3, emailId);
            int update_rc = sqlite3_step(update_stmt);
            if (update_rc != SQLITE_DONE) {
                LOG_INFO("[DB] email_add_email_to_session: UPDATE failed: rc=%d, err=%s\n", update_rc, sqlite3_errmsg(g_db));
            } else {
                LOG_INFO("[DB] email_add_email_to_session: UPDATE success, session_id=%s, email_id=%lld\n", sessionId ? sessionId : "", emailId);
            }
            sqlite3_finalize(update_stmt);
        } else {
            LOG_INFO("[DB] email_add_email_to_session: UPDATE prepare failed: %s\n", sqlite3_errmsg(g_db));
        }
    } else {
        const char* insert_sql = "INSERT INTO session (session_id, email_id, visible, auto, isread, encrypt_method) VALUES (?, ?, 1, 0, 0, ?);";
        sqlite3_stmt* insert_stmt;
        if (sqlite3_prepare_v2(g_db, insert_sql, -1, &insert_stmt, NULL) == SQLITE_OK) {
            sqlite3_bind_text(insert_stmt, 1, sessionId ? sessionId : "", -1, SQLITE_STATIC);
            sqlite3_bind_int64(insert_stmt, 2, emailId);
            sqlite3_bind_int(insert_stmt, 3, encrypt_method);
            int insert_rc = sqlite3_step(insert_stmt);
            if (insert_rc != SQLITE_DONE) {
                LOG_INFO("[DB] email_add_email_to_session: INSERT failed: rc=%d, err=%s\n", insert_rc, sqlite3_errmsg(g_db));
            } else {
                LOG_INFO("[DB] email_add_email_to_session: INSERT success, session_id=%s, email_id=%lld\n", sessionId ? sessionId : "", emailId);
            }
            sqlite3_finalize(insert_stmt);
        } else {
            LOG_INFO("[DB] email_add_email_to_session: INSERT prepare failed: %s\n", sqlite3_errmsg(g_db));
        }
    }

    if (outJson && outSize > 0) {
        json response;
        response["status"] = "success";
        response["uuid"] = uuid;
        std::string jsonStr = response.dump();
        snprintf(outJson, outSize, "%s", jsonStr.c_str());
    }
    return 0;
}

extern "C" int email_query_session_index_uuid(const char* sessionId, char* outJson, int outSize) {
    if (!g_db) {
        if (outJson && outSize > 0) {
            snprintf(outJson, outSize, R"({"status":"failed","error":"database_not_initialized"})");
        }
        return -1;
    }

    const char* sql = "SELECT email_id FROM session WHERE session_id = ? LIMIT 1;";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        if (outJson && outSize > 0) {
            snprintf(outJson, outSize, R"({"status":"failed","error":"prepare_failed"})");
        }
        return -2;
    }

    sqlite3_bind_text(stmt, 1, sessionId ? sessionId : "", -1, SQLITE_STATIC);

    std::string indexUuid = "";
    if ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        indexUuid = std::to_string(sqlite3_column_int64(stmt, 0));
    }
    sqlite3_finalize(stmt);

    json response;
    response["status"] = "success";
    response["index_uuid"] = indexUuid;

    std::string jsonStr = response.dump();
    if (outJson && outSize > 0) {
        snprintf(outJson, outSize, "%s", jsonStr.c_str());
    }
    return 0;
}

extern "C" int email_query_thread(const char* sessionId, char* outJson, int outSize) {
    if (!g_db) {
        if (outJson && outSize > 0) {
            snprintf(outJson, outSize, R"({"status":"failed","error":"database_not_initialized"})");
        }
        return -1;
    }

    LOG_INFO("[DB] email_query_thread called with session_id: '%s'\n", sessionId ? sessionId : "null");

    const char* sql =
        "SELECT l.uuid, l.account, l.sender, l.from_addr, l.subject, l.date, l.bodystructure, l.reply_to, l.in_reply_to, l.message_id, l.flags, l.folder, l.islocal, s.session_id, l.servicerecvtime, l.to_addr, l.id, l.file "
        "FROM localemail l "
        "INNER JOIN session s ON l.id = s.email_id "
        "WHERE s.session_id = ? AND s.visible = 1 "
        "ORDER BY l.id ASC;";

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        LOG_INFO("[DB] prepare failed: %s\n", sqlite3_errmsg(g_db));
        if (outJson && outSize > 0) {
            snprintf(outJson, outSize, R"({"status":"failed","error":"prepare_failed"})");
        }
        return -2;
    }

    sqlite3_bind_text(stmt, 1, sessionId ? sessionId : "", -1, SQLITE_STATIC);

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

    LOG_INFO("[DB] email_query_thread step result: %d, found %zu emails in thread\n", rc, emails_array.size());
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

extern "C" int email_update_session_read(const char* sessionId) {
    if (!sessionId) return -1;

    sqlite3* db = email_core_get_db();
    if (!db) return -2;

    std::lock_guard<std::mutex> lock(email_core_get_db_mutex());

    const char* sql = "UPDATE session SET isread = 1 WHERE session_id = ?;";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) return -3;

    sqlite3_bind_text(stmt, 1, sessionId, -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) return -4;

    LOG_INFO("email_update_session_read: updated session %s to isread=1\n", sessionId);
    return 0;
}

extern "C" int email_query_session_unread(const char* sessionId) {
    if (!sessionId) return -1;

    sqlite3* db = email_core_get_db();
    if (!db) return -2;

    std::lock_guard<std::mutex> lock(email_core_get_db_mutex());

    const char* sql = "SELECT COUNT(*) FROM session WHERE session_id = ? AND isread = 0;";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) return -3;

    sqlite3_bind_text(stmt, 1, sessionId, -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    int count = 0;
    if (rc == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);

    LOG_INFO("email_query_session_unread: session %s unread count = %d\n", sessionId, count);
    return count;
}

extern "C" int email_hide_session(const char* sessionId) {
    if (!sessionId) return -1;

    sqlite3* db = email_core_get_db();
    if (!db) return -2;

    std::lock_guard<std::mutex> lock(email_core_get_db_mutex());

    const char* sql = "UPDATE session SET visible = 0 WHERE session_id = ?;";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) return -3;

    sqlite3_bind_text(stmt, 1, sessionId, -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    int affected = sqlite3_changes(db);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) return -4;

    LOG_INFO("email_hide_session: session %s hidden, %d rows affected\n", sessionId, affected);
    return 0;
}
