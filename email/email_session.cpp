#include "email_core_common.h"
#include "email_core.h"
#include "logger.h"
#include "db_connection.h"
#include "email_repo.h"
#include "session_repo.h"
#include "key_repo.h"
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

static EmailRepo s_emailRepo;
static SessionRepo s_sessionRepo;
static KeyRepo s_keyRepo;

// Generate session records for existing emails
// Now a no-op: sessions are only created via X-Session-Chart=new in download_pending_bodies
extern "C" int email_generate_sessions(const char* account, char* outJson, int outSize) {
    if (outJson && outSize > 0) {
        snprintf(outJson, outSize, R"({"status":"success","message":"no-op"})");
    }
    return 0;
}

// Create a user-defined session (auto=0) with message_id as index_uuid
extern "C" int email_create_session(const char* account, const char* subject, const char* members, const char* message_id, int encrypt_method, char* outJson, int outSize) {
    auto& conn = DbConnection::instance();
    if (!conn.get()) {
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

        // Store keypair in keyinfo with session_uuid
        if (!s_keyRepo.insertKeyInfo(pubPem, privPem, password, session_id, account ? account : "")) {
            LOG_INFO("[DB] email_create_session: keyinfo insert failed\n");
            if (outJson && outSize > 0) {
                snprintf(outJson, outSize, R"({"status":"failed","error":"keyinfo_insert_failed"})");
            }
            return -1;
        }
        LOG_INFO("[DB] email_create_session: ECC key pair generated and inserted into keyinfo\n");

        // Also store own pubkey in code table for this session (code holds all members' pubkeys)
        if (!s_keyRepo.upsertCode(account ? account : "", pubPem, "", session_id)) {
            LOG_INFO("[DB] email_create_session: failed to insert own pubkey into code table\n");
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
    auto& conn = DbConnection::instance();
    if (!conn.get()) {
        if (outJson && outSize > 0) {
            snprintf(outJson, outSize, R"({"status":"failed","error":"database_not_initialized"})");
        }
        return -1;
    }

    LOG_INFO("[DB] email_insert_sent_email: account='%s', message_id='%s', subject='%s'\n",
             account ? account : "null", message_id ? message_id : "null", subject ? subject : "null");

    std::string existing_uuid;
    if (message_id && *message_id) {
        existing_uuid = s_emailRepo.findUuidByMessageId(message_id, account ? account : "");
    }

    if (!existing_uuid.empty()) {
        int64_t rowid = s_emailRepo.findRowidByUuid(existing_uuid);
        std::string rowidStr = std::to_string(rowid);
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

    int64_t rowid = s_emailRepo.insertSentEmail(
        account ? account : "",
        sender ? sender : "",
        from_addr ? from_addr : "",
        to_addr ? to_addr : "",
        subject ? subject : "",
        date ? date : "",
        message_id ? message_id : "",
        in_reply_to ? in_reply_to : "",
        bodystructureStr,
        filename);
    if (rowid > 0) {
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
                emlFile << "Mime-Version: 1.0\n";
                emlFile << "Content-Type: text/plain; charset=UTF-8\n";
                emlFile << "Content-Transfer-Encoding: 8bit\n";
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
    if (!messageId || !*messageId) {
        if (outSessionId && outSize > 0) outSessionId[0] = '\0';
        return -1;
    }

    std::string sid = s_sessionRepo.querySessionByMessageId(messageId, account ? account : "");
    if (outSessionId && outSize > 0) {
        snprintf(outSessionId, outSize, "%s", sid.c_str());
    }
    return 0;
}

extern "C" int email_add_email_to_session(const char* sessionId, const char* uuid, const char* account, int encrypt_method, char* outJson, int outSize) {
    auto& conn = DbConnection::instance();
    if (!conn.get()) {
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

    bool ok = s_sessionRepo.addEmailToSession(sessionId ? sessionId : "", emailId, encrypt_method);
    if (!ok) {
        LOG_INFO("[DB] email_add_email_to_session: failed, session_id=%s, email_id=%lld\n", sessionId ? sessionId : "", emailId);
    } else {
        LOG_INFO("[DB] email_add_email_to_session: success, session_id=%s, email_id=%lld\n", sessionId ? sessionId : "", emailId);
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
    auto& conn = DbConnection::instance();
    if (!conn.get()) {
        if (outJson && outSize > 0) {
            snprintf(outJson, outSize, R"({"status":"failed","error":"database_not_initialized"})");
        }
        return -1;
    }

    int64_t emailId = s_sessionRepo.queryFirstEmailId(sessionId ? sessionId : "");
    std::string indexUuid = std::to_string(emailId);

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
    auto& conn = DbConnection::instance();
    if (!conn.get()) {
        if (outJson && outSize > 0) {
            snprintf(outJson, outSize, R"({"status":"failed","error":"database_not_initialized"})");
        }
        return -1;
    }

    LOG_INFO("[DB] email_query_thread called with session_id: '%s'\n", sessionId ? sessionId : "null");

    auto emails = s_emailRepo.queryThread(sessionId ? sessionId : "");

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

    LOG_INFO("[DB] email_query_thread found %zu emails in thread\n", emails_array.size());

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

    auto& conn = DbConnection::instance();
    if (!conn.get()) return -2;

    std::lock_guard<std::mutex> lock(conn.mutex());

    if (!s_sessionRepo.updateRead(sessionId)) return -4;

    LOG_INFO("email_update_session_read: updated session %s to isread=1\n", sessionId);
    return 0;
}

extern "C" int email_query_session_unread(const char* sessionId) {
    if (!sessionId) return -1;

    auto& conn = DbConnection::instance();
    if (!conn.get()) return -2;

    std::lock_guard<std::mutex> lock(conn.mutex());

    int count = s_sessionRepo.countUnread(sessionId);

    LOG_INFO("email_query_session_unread: session %s unread count = %d\n", sessionId, count);
    return count;
}

extern "C" int email_hide_session(const char* sessionId) {
    if (!sessionId) return -1;

    auto& conn = DbConnection::instance();
    if (!conn.get()) return -2;

    std::lock_guard<std::mutex> lock(conn.mutex());

    if (!s_sessionRepo.hideSession(sessionId)) return -4;

    LOG_INFO("email_hide_session: session %s hidden\n", sessionId);
    return 0;
}
