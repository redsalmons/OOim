#include "email_handler_c.h"
#include "email_handler.h"
#include "email.h"
#include "email_opt_163_impl.h"
#include "email_opt_outlook_impl.h"
#include "logger.h"
#include <string>
#include <nlohmann/json.hpp>
#include <cstring>
#include <sqlite3.h>
#include <mutex>


// Forward declaration for db handle and mutex from email_core.cpp
extern "C" sqlite3* email_core_get_db();
extern "C" std::mutex& email_core_get_db_mutex();

// Forward declaration for email_query_thread_roots from email_core.cpp
extern "C" int email_query_thread_roots(const char* account, char* outJson, int outSize);

extern "C" {

// Global C-style callback function pointer
typedef void (*CNotificationCallback)(int, int, const char*);
static CNotificationCallback g_cNotificationCallback = nullptr;

extern "C" void oemailim_set_callback_impl(void* callback) {
    g_cNotificationCallback = reinterpret_cast<CNotificationCallback>(callback);
}

// Non-static function to set the callback (can be called from other files)
void set_c_notification_callback(void* callback) {
    g_cNotificationCallback = reinterpret_cast<CNotificationCallback>(callback);
}

int systemOpen_c(const char* dataDir,
                 const char* configDir,
                 const char* logDir) {
    if (!dataDir || !configDir || !logDir) {
        return 0;
    }

    try {
        // Get singleton instance and call systemOpen with callback
        auto handler = oemailim::EmailHandler::getInstance();
        
        // Create a lambda that calls the C-style callback
        oemailim::NotificationCallback callback = nullptr;
        CNotificationCallback localCallback = g_cNotificationCallback;
        if (localCallback) {
            callback = [localCallback](int configIndex, int messageType, const std::string& json) {
                localCallback(configIndex, messageType, json.c_str());
            };
        }
        
        return handler->systemOpen(
            std::string(dataDir),
            std::string(configDir),
            std::string(logDir),
            callback ? callback : [](int, int, const std::string&) {}
        ) ? 1 : 0;
    } catch (...) {
        return 0;
    }
}

int OpenNewEmail_c(const char* email_id) {
    try {
        LOG_INFO("OpenNewEmail_c called with email_id: %s\n", email_id ? email_id : "NULL");
        auto handler = oemailim::EmailHandler::getInstance();
        if (!email_id) {
            LOG_INFO("OpenNewEmail_c: email_id is NULL\n");
            return -1;
        }
        LOG_INFO("OpenNewEmail_c: Calling handler->OpenNewEmail\n");
        int result = handler->OpenNewEmail(std::string(email_id));
        LOG_INFO("OpenNewEmail_c: Result = %d\n", result);
        return result;
    } catch (const std::exception& e) {
        LOG_INFO("OpenNewEmail_c exception: %s\n", e.what());
        return -1;
    } catch (...) {
        LOG_INFO("OpenNewEmail_c unknown exception\n");
        return -1;
    }
}

int Go_c(int configIndex) {
    // Simplified: don't start thread, just return success
    try {
        auto handler = oemailim::EmailHandler::getInstance();
        
        // Check if configIndex is valid
        if (configIndex < 0 || configIndex >= static_cast<int>(oemailim::EmailHandler::g_EmailConfigIndices.size())) {
            return -1;
        }
        
        // Get the email object from the global vector
        auto email = oemailim::EmailHandler::g_EmailConfigIndices[configIndex];
        if (!email) {
            return -2;
        }
        
        // Don't start thread, just return success
        return 0;
    } catch (...) {
        return 0;
    }
}

int AddOutlookEmail_c() {
    try {
        auto handler = oemailim::EmailHandler::getInstance();
        return handler->AddOutlookEmail();
    } catch (const std::exception& e) {
        LOG_INFO("AddOutlookEmail_c exception: %s\n", e.what());
        return -1;
    } catch (...) {
        LOG_INFO("AddOutlookEmail_c unknown exception\n");
        return -1;
    }
}

int Authority_c(int configIndex) {
    LOG_INFO("Authority_c called with configIndex: %d\n", configIndex);
    try {
        auto handler = oemailim::EmailHandler::getInstance();
        
        // Check if configIndex is valid
        if (configIndex < 0 || configIndex >= static_cast<int>(oemailim::EmailHandler::g_EmailConfigIndices.size())) {
            LOG_INFO("Authority_c: Invalid configIndex: %d, size: %zu\n", configIndex, oemailim::EmailHandler::g_EmailConfigIndices.size());
            return -1;
        }
        
        LOG_INFO("Authority_c: configIndex is valid\n");
        
        // Get the email object from the global vector
        auto email = oemailim::EmailHandler::g_EmailConfigIndices[configIndex];
        if (!email) {
            LOG_INFO("Authority_c: Email object is null\n");
            return -2;
        }
        
        LOG_INFO("Authority_c: Calling email->authority()\n");
        // Call authority directly
        bool authorityResult = email->authority();
        LOG_INFO("Authority_c: authority() returned: %d\n", authorityResult);
        if (authorityResult) {
            // After successful OAuth, establish TCP connection
            bool connectResult = email->connect();
            LOG_INFO("Authority_c: connect() returned: %d\n", connectResult);
            if (connectResult) {
                return 0;
            } else {
                return -3;
            }
        } else {
            return -4;
        }
    } catch (...) {
        return -5;
    }
}

int GetEmailAddress_c(int configIndex, char* outEmail, int outSize) {
    LOG_INFO("GetEmailAddress_c called with configIndex: %d\n", configIndex);
    try {
        auto handler = oemailim::EmailHandler::getInstance();
        if (configIndex < 0 || configIndex >= static_cast<int>(oemailim::EmailHandler::g_EmailConfigIndices.size())) {
            LOG_INFO("GetEmailAddress_c: Invalid configIndex: %d, size: %zu\n", configIndex, oemailim::EmailHandler::g_EmailConfigIndices.size());
            return -1;
        }
        auto email = oemailim::EmailHandler::g_EmailConfigIndices[configIndex];
        if (!email) {
            LOG_INFO("GetEmailAddress_c: Email object is null\n");
            return -2;
        }
        std::string email_str = email->get_address();
        LOG_INFO("GetEmailAddress_c: Email: %s\n", email_str.c_str());
        if (outEmail && outSize > 0) {
            snprintf(outEmail, outSize, "%s", email_str.c_str());
        }
        return 0;
    } catch (...) {
        return -3;
    }
}

int GetRefreshToken_c(int configIndex, char* outToken, int outSize) {
    LOG_INFO("GetRefreshToken_c called with configIndex: %d\n", configIndex);
    try {
        auto handler = oemailim::EmailHandler::getInstance();
        if (configIndex < 0 || configIndex >= static_cast<int>(oemailim::EmailHandler::g_EmailConfigIndices.size())) {
            LOG_INFO("GetRefreshToken_c: Invalid configIndex: %d, size: %zu\n", configIndex, oemailim::EmailHandler::g_EmailConfigIndices.size());
            return -1;
        }
        auto email = oemailim::EmailHandler::g_EmailConfigIndices[configIndex];
        if (!email) {
            LOG_INFO("GetRefreshToken_c: Email object is null\n");
            return -2;
        }
        auto delegate = email->get_delegate();
        if (!delegate) {
            LOG_INFO("GetRefreshToken_c: Delegate is null\n");
            return -3;
        }
        std::string token_str = delegate->get_refresh_token();
        LOG_INFO("GetRefreshToken_c: Token length: %zu\n", token_str.length());
        if (outToken && outSize > 0) {
            snprintf(outToken, outSize, "%s", token_str.c_str());
        }
        return 0;
    } catch (...) {
        return -4;
    }
}

int SetRefreshToken_c(int configIndex, const char* token) {
    LOG_INFO("SetRefreshToken_c called with configIndex: %d\n", configIndex);
    try {
        auto handler = oemailim::EmailHandler::getInstance();
        if (configIndex < 0 || configIndex >= static_cast<int>(oemailim::EmailHandler::g_EmailConfigIndices.size())) {
            LOG_INFO("SetRefreshToken_c: Invalid configIndex: %d, size: %zu\n", configIndex, oemailim::EmailHandler::g_EmailConfigIndices.size());
            return -1;
        }
        auto email = oemailim::EmailHandler::g_EmailConfigIndices[configIndex];
        if (!email) {
            LOG_INFO("SetRefreshToken_c: Email object is null\n");
            return -2;
        }
        auto delegate = email->get_delegate();
        if (!delegate) {
            LOG_INFO("SetRefreshToken_c: Delegate is null\n");
            return -3;
        }
        // Cast to EmailOptOutlookImpl to set refresh token
        auto outlookDelegate = std::dynamic_pointer_cast<EmailComm::EmailOptOutlookImpl>(delegate);
        if (outlookDelegate) {
            outlookDelegate->set_refresh_token(std::string(token));
            LOG_INFO("SetRefreshToken_c: Successfully set refresh token\n");
            return 0;
        } else {
            LOG_INFO("SetRefreshToken_c: Delegate is not EmailOptOutlookImpl\n");
            return -4;
        }
    } catch (...) {
        return -5;
    }
}

int RefreshToken_c(int configIndex) {
    LOG_INFO("RefreshToken_c called with configIndex: %d\n", configIndex);
    try {
        auto handler = oemailim::EmailHandler::getInstance();
        if (configIndex < 0 || configIndex >= static_cast<int>(oemailim::EmailHandler::g_EmailConfigIndices.size())) {
            LOG_INFO("RefreshToken_c: Invalid configIndex: %d, size: %zu\n", configIndex, oemailim::EmailHandler::g_EmailConfigIndices.size());
            return -1;
        }
        auto email = oemailim::EmailHandler::g_EmailConfigIndices[configIndex];
        if (!email) {
            LOG_INFO("RefreshToken_c: Email object is null\n");
            return -2;
        }
        auto delegate = email->get_delegate();
        if (!delegate) {
            LOG_INFO("RefreshToken_c: Delegate is null\n");
            return -3;
        }
        LOG_INFO("RefreshToken_c: Delegate type name: %s\n", typeid(*delegate).name());
        // Cast to EmailOptOutlookImpl to refresh token
        auto outlookDelegate = std::dynamic_pointer_cast<EmailComm::EmailOptOutlookImpl>(delegate);
        if (outlookDelegate) {
            bool result = outlookDelegate->refresh_token();
            LOG_INFO("RefreshToken_c: Refresh result: %d\n", result);
            return result ? 0 : -4;
        } else {
            LOG_INFO("RefreshToken_c: Delegate is not EmailOptOutlookImpl\n");
            return -4;
        }
    } catch (...) {
        return -5;
    }
}

int SetImapServer_c(int configIndex, const char* server, int port) {
    LOG_INFO("SetImapServer_c called with configIndex: %d, server: %s, port: %d\n", configIndex, server, port);
    try {
        auto handler = oemailim::EmailHandler::getInstance();
        if (configIndex < 0 || configIndex >= static_cast<int>(oemailim::EmailHandler::g_EmailConfigIndices.size())) {
            LOG_INFO("SetImapServer_c: Invalid configIndex: %d, size: %zu\n", configIndex, oemailim::EmailHandler::g_EmailConfigIndices.size());
            return -1;
        }
        auto email = oemailim::EmailHandler::g_EmailConfigIndices[configIndex];
        if (!email) {
            LOG_INFO("SetImapServer_c: Email object is null\n");
            return -2;
        }
        auto delegate = email->get_delegate();
        if (!delegate) {
            LOG_INFO("SetImapServer_c: Delegate is null\n");
            return -3;
        }
        // Cast to EmailOptOutlookImpl to set IMAP server
        auto outlookDelegate = std::dynamic_pointer_cast<EmailComm::EmailOptOutlookImpl>(delegate);
        if (outlookDelegate) {
            outlookDelegate->set_imap_server(server, port);
            LOG_INFO("SetImapServer_c: Successfully set IMAP server\n");
            return 0;
        } else {
            LOG_INFO("SetImapServer_c: Delegate is not EmailOptOutlookImpl\n");
            return -4;
        }
    } catch (...) {
        return -5;
    }
}

int SetSmtpServer_c(int configIndex, const char* server, int port) {
    LOG_INFO("SetSmtpServer_c called with configIndex: %d, server: %s, port: %d\n", configIndex, server, port);
    try {
        auto handler = oemailim::EmailHandler::getInstance();
        if (configIndex < 0 || configIndex >= static_cast<int>(oemailim::EmailHandler::g_EmailConfigIndices.size())) {
            LOG_INFO("SetSmtpServer_c: Invalid configIndex: %d, size: %zu\n", configIndex, oemailim::EmailHandler::g_EmailConfigIndices.size());
            return -1;
        }
        auto email = oemailim::EmailHandler::g_EmailConfigIndices[configIndex];
        if (!email) {
            LOG_INFO("SetSmtpServer_c: Email object is null\n");
            return -2;
        }
        auto delegate = email->get_delegate();
        if (!delegate) {
            LOG_INFO("SetSmtpServer_c: Delegate is null\n");
            return -3;
        }
        // Cast to EmailOpt163Impl to set SMTP server
        auto impl163 = std::dynamic_pointer_cast<EmailComm::EmailOpt163Impl>(delegate);
        if (impl163) {
            impl163->set_smtp_server(server ? server : "", port);
            LOG_INFO("SetSmtpServer_c: Successfully set SMTP server\n");
            return 0;
        } else {
            LOG_INFO("SetSmtpServer_c: Delegate is not EmailOpt163Impl\n");
            return -4;
        }
    } catch (...) {
        return -5;
    }
}

int SetAccountType_c(int configIndex, const char* accountType) {
    LOG_INFO("SetAccountType_c called with configIndex: %d, accountType: %s\n", configIndex, accountType ? accountType : "NULL");
    try {
        auto handler = oemailim::EmailHandler::getInstance();
        if (configIndex < 0 || configIndex >= static_cast<int>(oemailim::EmailHandler::g_EmailConfigIndices.size())) {
            LOG_INFO("SetAccountType_c: Invalid configIndex: %d, size: %zu\n", configIndex, oemailim::EmailHandler::g_EmailConfigIndices.size());
            return -1;
        }
        auto email = oemailim::EmailHandler::g_EmailConfigIndices[configIndex];
        if (!email) {
            LOG_INFO("SetAccountType_c: Email object is null\n");
            return -2;
        }
        auto delegate = email->get_delegate();
        if (!delegate) {
            LOG_INFO("SetAccountType_c: Delegate is null\n");
            return -3;
        }
        // Cast to EmailOptOutlookImpl to set account type
        auto outlookDelegate = std::dynamic_pointer_cast<EmailComm::EmailOptOutlookImpl>(delegate);
        if (outlookDelegate) {
            outlookDelegate->set_account_type(accountType ? accountType : "personal");
            LOG_INFO("SetAccountType_c: Successfully set account type to %s\n", accountType ? accountType : "personal");
            return 0;
        } else {
            LOG_INFO("SetAccountType_c: Delegate is not EmailOptOutlookImpl\n");
            return -4;
        }
    } catch (...) {
        return -5;
    }
}

void systemClose_c(int configIndex) {
    try {
        // Check if configIndex is valid
        if (configIndex < 0 || configIndex >= static_cast<int>(oemailim::EmailHandler::g_EmailConfigIndices.size())) {
            return;
        }
        
        // Get the email object from the global vector
        auto email = oemailim::EmailHandler::g_EmailConfigIndices[configIndex];
        if (!email) {
            return;
        }
        
        // Don't stop thread (we don't use threads anymore)
    } catch (...) {
        // Ignore exceptions
    }
}

int Email_List_c(int configIndex, const char* path, char* outJson, int outSize) {
    // Simplified: call directly without thread, return JSON data
    try {
        // Check if configIndex is valid
        if (configIndex < 0 || configIndex >= static_cast<int>(oemailim::EmailHandler::g_EmailConfigIndices.size())) {
            if (outJson && outSize > 0) {
                snprintf(outJson, outSize, R"({"status":"failed", "error":"invalid_config_index"})");
            }
            return -1;
        }
        
        // Get the email object from the global vector
        auto email = oemailim::EmailHandler::g_EmailConfigIndices[configIndex];
        if (!email) {
            if (outJson && outSize > 0) {
                snprintf(outJson, outSize, R"({"status":"failed", "error":"email_object_null"})");
            }
            return -2;
        }
        
        // Default path to "*" if not provided
        std::string folderPath = (path && strlen(path) > 0) ? path : "*";
        
        // Get the delegate to call select_folder
        auto delegate = email->get_delegate();
        if (!delegate) {
            if (outJson && outSize > 0) {
                snprintf(outJson, outSize, R"({"status":"failed", "error":"delegate_null"})");
            }
            return -3;
        }
        
        // Select the folder
        bool selectResult = delegate->select_folder(folderPath);
        if (!selectResult) {
            if (outJson && outSize > 0) {
                snprintf(outJson, outSize, R"({"status":"failed", "error":"select_folder_failed"})");
            }
            return -4;
        }
        
        // Fetch emails since UID (empty string for all emails)
        std::vector<std::string> emailUids = delegate->fetch_emails_since_uid(folderPath, "");
        
        // Build JSON response with email UIDs
        nlohmann::json response;
        response["status"] = "success";
        response["folder"] = folderPath;
        response["count"] = emailUids.size();
        response["uids"] = emailUids;
        
        // Copy JSON to output buffer
        std::string jsonStr = response.dump();
        if (outJson && outSize > 0) {
            snprintf(outJson, outSize, "%s", jsonStr.c_str());
        }
        
        return 0;
    } catch (const std::exception& e) {
        if (outJson && outSize > 0) {
            snprintf(outJson, outSize, R"({"status":"failed", "error":"%s"})", e.what());
        }
        return -5;
    } catch (...) {
        if (outJson && outSize > 0) {
            snprintf(outJson, outSize, R"({"status":"failed", "error":"exception"})");
        }
        return -6;
    }
}

int Email_Select_c(int configIndex, const char* path, char* outJson, int outSize) {
    // Simplified: call directly without thread, return JSON data
    try {
        // Check if configIndex is valid
        if (configIndex < 0 || configIndex >= static_cast<int>(oemailim::EmailHandler::g_EmailConfigIndices.size())) {
            if (outJson && outSize > 0) {
                snprintf(outJson, outSize, R"({"status":"failed", "error":"invalid_config_index"})");
            }
            return -1;
        }
        
        // Get the email object from the global vector
        auto email = oemailim::EmailHandler::g_EmailConfigIndices[configIndex];
        if (!email) {
            if (outJson && outSize > 0) {
                snprintf(outJson, outSize, R"({"status":"failed", "error":"email_object_null"})");
            }
            return -2;
        }
        
        // Check if path is provided
        if (!path || strlen(path) == 0) {
            if (outJson && outSize > 0) {
                snprintf(outJson, outSize, R"({"status":"failed", "error":"invalid_path"})");
            }
            return -3;
        }
        
        std::string folderPath = path;
        
        // Get the delegate to call select_folder
        auto delegate = email->get_delegate();
        if (!delegate) {
            if (outJson && outSize > 0) {
                snprintf(outJson, outSize, R"({"status":"failed", "error":"delegate_null"})");
            }
            return -4;
        }
        
        // Select the folder
        bool selectResult = delegate->select_folder(folderPath);
        if (selectResult) {
            nlohmann::json response;
            response["status"] = "success";
            response["folder"] = folderPath;
            
            // Copy JSON to output buffer
            std::string jsonStr = response.dump();
            if (outJson && outSize > 0) {
                snprintf(outJson, outSize, "%s", jsonStr.c_str());
            }
            return 0;
        } else {
            if (outJson && outSize > 0) {
                snprintf(outJson, outSize, R"({"status":"failed", "error":"select_folder_failed"})");
            }
            return -5;
        }
    } catch (const std::exception& e) {
        if (outJson && outSize > 0) {
            snprintf(outJson, outSize, R"({"status":"failed", "error":"%s"})", e.what());
        }
        return -6;
    } catch (...) {
        if (outJson && outSize > 0) {
            snprintf(outJson, outSize, R"({"status":"failed", "error":"exception"})");
        }
        return -7;
    }
}

int SetCredentials_c(int configIndex, const char* email, const char* authCode) {
    try {
        if (configIndex < 0 || configIndex >= static_cast<int>(oemailim::EmailHandler::g_EmailConfigIndices.size())) {
            return -1;
        }

        auto emailObj = oemailim::EmailHandler::g_EmailConfigIndices[configIndex];
        if (!emailObj) {
            return -2;
        }

        auto delegate = emailObj->get_delegate();
        if (!delegate) {
            return -3;
        }

        // Use the 163-specific interface to set credentials
        auto impl163 = std::dynamic_pointer_cast<EmailComm::EmailOpt163Impl>(delegate);
        if (impl163) {
            impl163->set_email(email ? email : "");
            impl163->set_auth_code(authCode ? authCode : "");
            emailObj->set_address(email ? email : "");
            return 0;
        }

        // Use the Outlook-specific interface
        auto implOutlook = std::dynamic_pointer_cast<EmailComm::EmailOptOutlookImpl>(delegate);
        if (implOutlook) {
            implOutlook->set_email(email ? email : "");
            // Outlook uses refresh token which is handled separately
            emailObj->set_address(email ? email : "");
            return 0;
        }

        return -4;
    } catch (...) {
        return -5;
    }
}

int GetEmail_c(int configIndex, const char* folder, const char* uid, char* outJson, int outSize) {
    try {
        if (configIndex < 0 || configIndex >= static_cast<int>(oemailim::EmailHandler::g_EmailConfigIndices.size())) {
            if (outJson && outSize > 0) {
                snprintf(outJson, outSize, R"({"status":"failed", "error":"invalid_config_index"})");
            }
            return -1;
        }

        auto emailObj = oemailim::EmailHandler::g_EmailConfigIndices[configIndex];
        if (!emailObj) {
            if (outJson && outSize > 0) {
                snprintf(outJson, outSize, R"({"status":"failed", "error":"email_object_null"})");
            }
            return -2;
        }

        auto delegate = emailObj->get_delegate();
        if (!delegate) {
            if (outJson && outSize > 0) {
                snprintf(outJson, outSize, R"({"status":"failed", "error":"delegate_null"})");
            }
            return -3;
        }

        std::string folderStr = folder ? folder : "INBOX";
        std::string uidStr = uid ? uid : "";

        std::string content = delegate->get_email(folderStr, uidStr);
        if (content.empty()) {
            if (outJson && outSize > 0) {
                snprintf(outJson, outSize, R"({"status":"failed", "error":"email_not_found"})");
            }
            return -4;
        }

        nlohmann::json response;
        response["status"] = "success";
        response["folder"] = folderStr;
        response["uid"] = uidStr;
        response["content"] = content;

        std::string jsonStr = response.dump();
        if (outJson && outSize > 0) {
            snprintf(outJson, outSize, "%s", jsonStr.c_str());
        }
        return 0;
    } catch (const std::exception& e) {
        if (outJson && outSize > 0) {
            snprintf(outJson, outSize, R"({"status":"failed", "error":"%s"})", e.what());
        }
        return -5;
    } catch (...) {
        if (outJson && outSize > 0) {
            snprintf(outJson, outSize, R"({"status":"failed", "error":"exception"})");
        }
        return -6;
    }
}

int GetEmailToFile_c(int configIndex, const char* folder, const char* uid, const char* filePath) {
    try {
        if (configIndex < 0 || configIndex >= static_cast<int>(oemailim::EmailHandler::g_EmailConfigIndices.size())) {
            return -1;
        }

        auto emailObj = oemailim::EmailHandler::g_EmailConfigIndices[configIndex];
        if (!emailObj) {
            return -2;
        }

        auto delegate = emailObj->get_delegate();
        if (!delegate) {
            return -3;
        }

        std::string folderStr = folder ? folder : "INBOX";
        std::string uidStr = uid ? uid : "";
        std::string filePathStr = filePath ? filePath : "";

        std::string content = delegate->get_email(folderStr, uidStr);
        if (content.empty()) {
            // Check if this was a network error or a non-network error
            std::string lastErr = delegate->get_last_error();
            // Network-related keywords in error message
            bool isNetwork = lastErr.find("EPIPE") != std::string::npos ||
                             lastErr.find("broken pipe") != std::string::npos ||
                             lastErr.find("ECONNRESET") != std::string::npos ||
                             lastErr.find("connection reset") != std::string::npos ||
                             lastErr.find("connect failed") != std::string::npos ||
                             lastErr.find("not connected") != std::string::npos ||
                             lastErr.find("timeout") != std::string::npos ||
                             lastErr.find("socket") != std::string::npos;
            LOG_INFO("GetEmailToFile_c: get_email returned empty, last_error='%s', isNetwork=%d\n", lastErr.c_str(), isNetwork);
            return isNetwork ? -10 : -4;
        }

        std::ofstream outFile(filePathStr, std::ios::binary);
        if (!outFile.is_open()) {
            return -7;
        }
        outFile << content;
        outFile.close();

        LOG_INFO("GetEmailToFile_c: saved uid=%s to %s (%zu bytes)\n", uidStr.c_str(), filePathStr.c_str(), content.size());
        return 0;
    } catch (const std::exception& e) {
        LOG_INFO("GetEmailToFile_c: exception: %s\n", e.what());
        return -5;
    } catch (...) {
        return -6;
    }
}

int FetchAndStore_c(int configIndex, const char* folder, const char* startUid,
                    const char* account, char* outJson, int outSize) {
    try {
        if (configIndex < 0 || configIndex >= static_cast<int>(oemailim::EmailHandler::g_EmailConfigIndices.size())) {
            if (outJson && outSize > 0) {
                snprintf(outJson, outSize, R"({"status":"failed", "error":"invalid_config_index"})");
            }
            return -1;
        }

        auto emailObj = oemailim::EmailHandler::g_EmailConfigIndices[configIndex];
        if (!emailObj) {
            if (outJson && outSize > 0) {
                snprintf(outJson, outSize, R"({"status":"failed", "error":"email_object_null"})");
            }
            return -2;
        }

        auto delegate = emailObj->get_delegate();
        if (!delegate) {
            LOG_INFO("FetchAndStore_c: Delegate is null for configIndex %d", configIndex);
            if (outJson && outSize > 0) {
                snprintf(outJson, outSize, R"({"status":"failed", "error":"delegate_null"})");
            }
            return -3;
        }

        LOG_INFO("FetchAndStore_c: configIndex=%d, delegate type=%s", configIndex, typeid(*delegate).name());

        std::string folderStr = folder ? folder : "INBOX";
        std::string startUidStr = startUid ? startUid : "";
        std::string accountStr = account ? account : "";

        std::string headersJson;
        
        // Use the 163-specific implementation for fetch_email_headers
        auto impl163 = std::dynamic_pointer_cast<EmailComm::EmailOpt163Impl>(delegate);
        if (impl163) {
            LOG_INFO("FetchAndStore_c: Using 163 implementation");
            headersJson = impl163->fetch_email_headers(folderStr, startUidStr);
        } else {
            auto implOutlook = std::dynamic_pointer_cast<EmailComm::EmailOptOutlookImpl>(delegate);
            if (implOutlook) {
                LOG_INFO("FetchAndStore_c: Using Outlook implementation");
                headersJson = implOutlook->fetch_email_headers(folderStr, startUidStr);
            } else {
                LOG_INFO("FetchAndStore_c: Unsupported provider type for configIndex %d", configIndex);
                if (outJson && outSize > 0) {
                    snprintf(outJson, outSize, R"({"status":"failed", "error":"unsupported_provider"})");
                }
                return -4;
            }
        }

        // Parse the returned JSON and store each email in localemail table
        nlohmann::json response = nlohmann::json::parse(headersJson);

        if (response["status"] != "success") {
            if (outJson && outSize > 0) {
                snprintf(outJson, outSize, "%s", headersJson.c_str());
            }
            return -5;
        }

        // Get the global sqlite db handle from email_core.cpp
        sqlite3* db = email_core_get_db();
        if (!db) {
            if (outJson && outSize > 0) {
                snprintf(outJson, outSize, R"({"status":"failed", "error":"database_not_initialized"})");
            }
            return -6;
        }

        // WAL mode enables concurrent read/write — no global mutex needed here.
        // Holding g_db_mutex during the entire fetch loop was blocking UI thread queries.

        int stored_count = 0;
        auto emails = response["emails"];
        for (const auto& email_data : emails) {
            std::string uuid = email_data.value("uuid", "");
            std::string from_addr = email_data.value("from", "");
            std::string sender = email_data.value("sender", "");
            std::string subject = email_data.value("subject", "");
            std::string date = email_data.value("date", "");
            std::string reply_to = email_data.value("reply_to", "");
            std::string in_reply_to = email_data.value("in_reply_to", "");
            std::string message_id = email_data.value("message_id", "");
            std::string x_message_id = email_data.value("x_message_id", "");
            std::string x_session_id = email_data.value("x_session_id", "");

            // If X-Message-ID header exists, use it as message_id for matching
            // This allows sent emails (which have our locally generated X-Message-ID) to be matched
            if (!x_message_id.empty()) {
                LOG_INFO("FetchAndStore_c: using x_message_id='%s' as message_id (original message_id='%s')\n",
                         x_message_id.c_str(), message_id.c_str());
                message_id = x_message_id;
            }
            std::string servicerecvtime = email_data.value("servicerecvtime", "");

            // bodystructure is now a JSON object, serialize it to string
            std::string bodystructure;
            if (email_data.contains("bodystructure") && email_data["bodystructure"].is_object()) {
                bodystructure = email_data["bodystructure"].dump();
            } else {
                bodystructure = email_data.value("bodystructure", "");
            }

            // flags is a JSON array, serialize it to string
            std::string flags;
            if (email_data.contains("flags") && email_data["flags"].is_array()) {
                flags = email_data["flags"].dump();
            } else {
                flags = email_data.value("flags", "");
            }

            // Use the folder parameter from the function call
            std::string folder = folderStr;

            if (uuid.empty()) continue;

            // Check if a record with the same message_id already exists (from sent email or previous sync)
            bool found_existing = false;
            int64_t existing_id = 0;
            if (!message_id.empty()) {
                const char* check_msgid_sql = "SELECT id FROM localemail WHERE message_id = ? AND account = ? LIMIT 1;";
                sqlite3_stmt* check_msgid_stmt;
                if (sqlite3_prepare_v2(db, check_msgid_sql, -1, &check_msgid_stmt, NULL) == SQLITE_OK) {
                    sqlite3_bind_text(check_msgid_stmt, 1, message_id.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(check_msgid_stmt, 2, accountStr.c_str(), -1, SQLITE_TRANSIENT);
                    if (sqlite3_step(check_msgid_stmt) == SQLITE_ROW) {
                        found_existing = true;
                        existing_id = sqlite3_column_int64(check_msgid_stmt, 0);
                    }
                    sqlite3_finalize(check_msgid_stmt);
                }
            }

            if (found_existing) {
                // Update the existing record with real data from IMAP
                const char* update_sql = "UPDATE localemail SET uuid = ?, sender = ?, from_addr = ?, subject = ?, date = ?, bodystructure = ?, reply_to = ?, in_reply_to = ?, flags = ?, folder = ?, servicerecvtime = ?, to_addr = ? WHERE id = ?;";
                sqlite3_stmt* update_stmt;
                if (sqlite3_prepare_v2(db, update_sql, -1, &update_stmt, NULL) == SQLITE_OK) {
                    sqlite3_bind_text(update_stmt, 1, uuid.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(update_stmt, 2, sender.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(update_stmt, 3, from_addr.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(update_stmt, 4, subject.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(update_stmt, 5, date.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(update_stmt, 6, bodystructure.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(update_stmt, 7, reply_to.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(update_stmt, 8, in_reply_to.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(update_stmt, 9, flags.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(update_stmt, 10, folder.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(update_stmt, 11, servicerecvtime.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(update_stmt, 12, email_data.value("to_addr", "").c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_int64(update_stmt, 13, existing_id);
                    int update_rc = sqlite3_step(update_stmt);
                    if (update_rc != SQLITE_DONE) {
                        LOG_INFO("FetchAndStore_c: UPDATE failed, rc=%d, error=%s\n", update_rc, sqlite3_errmsg(db));
                    } else {
                        LOG_INFO("FetchAndStore_c: Updated id=%lld, uuid='%s' -> '%s', folder='%s'\n", (long long)existing_id, uuid.c_str(), uuid.c_str(), folder.c_str());
                    }
                    sqlite3_finalize(update_stmt);
                } else {
                    LOG_INFO("FetchAndStore_c: prepare UPDATE failed, error=%s\n", sqlite3_errmsg(db));
                }

                // No need to update session table — email_id stores localemail.id which doesn't change
                stored_count++;
                continue;
            }

            // No existing record found — insert a new one
            const char* sql = "INSERT INTO localemail "
                              "(uuid, account, sender, from_addr, subject, date, bodystructure, reply_to, in_reply_to, message_id, flags, folder, islocal, servicerecvtime, to_addr, file) "
                              "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 0, ?, ?, '');";
            sqlite3_stmt* stmt;
            int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
            if (rc != SQLITE_OK) continue;

            sqlite3_bind_text(stmt, 1, uuid.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, accountStr.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 3, sender.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 4, from_addr.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 5, subject.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 6, date.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 7, bodystructure.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 8, reply_to.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 9, in_reply_to.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 10, message_id.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 11, flags.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 12, folder.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 13, servicerecvtime.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 14, email_data.value("to_addr", "").c_str(), -1, SQLITE_TRANSIENT);
            
            rc = sqlite3_step(stmt);
            int changes = sqlite3_changes(db);
            sqlite3_finalize(stmt);

            // Only create session records if the insert actually happened (not ignored)
            // AND only for INBOX folder emails
            if (rc == SQLITE_DONE && changes > 0 && folder == "INBOX") {
                stored_count++;
                
                // Generate session_id for this email
                std::string session_id;
                
                // Priority 1: Use x-session-id if present
                if (!x_session_id.empty()) {
                    session_id = x_session_id;
                }
                // Priority 2: If in_reply_to points to an existing message, use its session_id
                // Only associate within the same account and INBOX folder
                else if (!in_reply_to.empty()) {
                    // Only find referenced message in the same account and INBOX folder
                    const char* find_rowid_sql = "SELECT id FROM localemail WHERE message_id = ? AND account = ? AND folder = 'INBOX' LIMIT 1;";
                    sqlite3_stmt* find_rowid_stmt;
                    int find_rowid_rc = sqlite3_prepare_v2(db, find_rowid_sql, -1, &find_rowid_stmt, NULL);
                    std::string referenced_rowid;
                    if (find_rowid_rc == SQLITE_OK) {
                        sqlite3_bind_text(find_rowid_stmt, 1, in_reply_to.c_str(), -1, SQLITE_TRANSIENT);
                        sqlite3_bind_text(find_rowid_stmt, 2, accountStr.c_str(), -1, SQLITE_TRANSIENT);
                        if (sqlite3_step(find_rowid_stmt) == SQLITE_ROW) {
                            referenced_rowid = std::to_string(sqlite3_column_int64(find_rowid_stmt, 0));
                        }
                        sqlite3_finalize(find_rowid_stmt);
                    }
                    
                    // Only search within the same account to avoid cross-account session contamination
                    if (referenced_rowid.empty()) {
                        // Do not search across accounts - this causes incorrect session associations
                        // If the referenced message is not in the same account, create a new session
                    }
                    
                    if (!referenced_rowid.empty()) {
                        // Check if the referenced email has a session_id
                        const char* check_session_sql = "SELECT session_id FROM session WHERE email_id = ? LIMIT 1;";
                        sqlite3_stmt* check_session_stmt;
                        int check_session_rc = sqlite3_prepare_v2(db, check_session_sql, -1, &check_session_stmt, NULL);
                        if (check_session_rc == SQLITE_OK) {
                            sqlite3_bind_int64(check_session_stmt, 1, std::stoll(referenced_rowid));
                            if (sqlite3_step(check_session_stmt) == SQLITE_ROW) {
                                session_id = reinterpret_cast<const char*>(sqlite3_column_text(check_session_stmt, 0));
                            }
                            sqlite3_finalize(check_session_stmt);
                        }
                        
                        // If the referenced email has no session, use its rowid as session name
                        // but do NOT insert the referenced email into session table
                        // (it might be a Sent email — only INBOX emails get session records)
                        if (session_id.empty()) {
                            session_id = "session_" + referenced_rowid;
                        }
                    } else {
                        // Referenced message not found in same account, generate new session
                        int64_t my_rowid = sqlite3_last_insert_rowid(db);
                        session_id = "session_" + std::to_string(my_rowid);
                    }
                }
                // Priority 3: Check if any existing email's in_reply_to points to this email's message_id
                // Only look within the same account and INBOX folder
                else {
                    const char* find_replier_sql = "SELECT id FROM localemail WHERE in_reply_to = ? AND account = ? AND folder = 'INBOX' LIMIT 1;";
                    sqlite3_stmt* find_replier_stmt;
                    int find_replier_rc = sqlite3_prepare_v2(db, find_replier_sql, -1, &find_replier_stmt, NULL);
                    std::string replier_rowid;
                    if (find_replier_rc == SQLITE_OK) {
                        sqlite3_bind_text(find_replier_stmt, 1, message_id.c_str(), -1, SQLITE_TRANSIENT);
                        sqlite3_bind_text(find_replier_stmt, 2, accountStr.c_str(), -1, SQLITE_TRANSIENT);
                        if (sqlite3_step(find_replier_stmt) == SQLITE_ROW) {
                            replier_rowid = std::to_string(sqlite3_column_int64(find_replier_stmt, 0));
                        }
                        sqlite3_finalize(find_replier_stmt);
                    }
                    
                    int64_t my_rowid = sqlite3_last_insert_rowid(db);
                    std::string my_rowid_str = std::to_string(my_rowid);
                    if (!replier_rowid.empty()) {
                        // Check if the replier email has a session_id
                        const char* check_replier_session_sql = "SELECT session_id FROM session WHERE email_id = ? LIMIT 1;";
                        sqlite3_stmt* check_replier_session_stmt;
                        int check_replier_session_rc = sqlite3_prepare_v2(db, check_replier_session_sql, -1, &check_replier_session_stmt, NULL);
                        if (check_replier_session_rc == SQLITE_OK) {
                            sqlite3_bind_int64(check_replier_session_stmt, 1, std::stoll(replier_rowid));
                            if (sqlite3_step(check_replier_session_stmt) == SQLITE_ROW) {
                                session_id = reinterpret_cast<const char*>(sqlite3_column_text(check_replier_session_stmt, 0));
                            }
                            sqlite3_finalize(check_replier_session_stmt);
                        }
                        
                        // If the replier email has no session, create one using this email as root
                        if (session_id.empty()) {
                            session_id = "session_" + my_rowid_str;
                        }
                    } else {
                        // No relationships found, generate new session
                        session_id = "session_" + my_rowid_str;
                    }
                }
                
                // Get rowid of the just-inserted email
                int64_t my_rowid = sqlite3_last_insert_rowid(db);
                std::string my_rowid_str = std::to_string(my_rowid);
                
                // Delete existing session record for this email_id, then insert
                const char* del_session_sql = "DELETE FROM session WHERE email_id = ?;";
                sqlite3_stmt* del_session_stmt;
                if (sqlite3_prepare_v2(db, del_session_sql, -1, &del_session_stmt, NULL) == SQLITE_OK) {
                    sqlite3_bind_int64(del_session_stmt, 1, my_rowid);
                    sqlite3_step(del_session_stmt);
                    sqlite3_finalize(del_session_stmt);
                }
                const char* session_sql = "INSERT OR REPLACE INTO session (session_id, email_id, visible, auto, isread) VALUES (?, ?, 1, 1, 0);";
                sqlite3_stmt* session_stmt;
                int session_rc = sqlite3_prepare_v2(db, session_sql, -1, &session_stmt, NULL);
                if (session_rc == SQLITE_OK) {
                    sqlite3_bind_text(session_stmt, 1, session_id.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_int64(session_stmt, 2, my_rowid);
                    sqlite3_step(session_stmt);
                    sqlite3_finalize(session_stmt);
                }
                
                // After inserting, check if any existing emails should join this session
                // (emails whose in_reply_to points to this email's message_id but are not yet in this session)
                // Only look within the same account and INBOX folder
                if (!message_id.empty()) {
                    const char* find_orphans_sql = "SELECT id FROM localemail WHERE in_reply_to = ? AND account = ? AND folder = 'INBOX' AND id NOT IN (SELECT email_id FROM session WHERE session_id = ?);";
                    sqlite3_stmt* find_orphans_stmt;
                    int find_orphans_rc = sqlite3_prepare_v2(db, find_orphans_sql, -1, &find_orphans_stmt, NULL);
                    if (find_orphans_rc == SQLITE_OK) {
                        sqlite3_bind_text(find_orphans_stmt, 1, message_id.c_str(), -1, SQLITE_TRANSIENT);
                        sqlite3_bind_text(find_orphans_stmt, 2, accountStr.c_str(), -1, SQLITE_TRANSIENT);
                        sqlite3_bind_text(find_orphans_stmt, 3, session_id.c_str(), -1, SQLITE_TRANSIENT);
                        
                        while (sqlite3_step(find_orphans_stmt) == SQLITE_ROW) {
                            // Delete existing session record for orphan, then add to current session
                            const char* del_orphan_sql = "DELETE FROM session WHERE email_id = ?;";
                            sqlite3_stmt* del_orphan_stmt;
                            if (sqlite3_prepare_v2(db, del_orphan_sql, -1, &del_orphan_stmt, NULL) == SQLITE_OK) {
                                sqlite3_bind_int64(del_orphan_stmt, 1, sqlite3_column_int64(find_orphans_stmt, 0));
                                sqlite3_step(del_orphan_stmt);
                                sqlite3_finalize(del_orphan_stmt);
                            }
                            const char* add_orphan_sql = "INSERT OR REPLACE INTO session (session_id, email_id, visible, auto, isread) VALUES (?, ?, 1, 1, 0);";
                            sqlite3_stmt* add_orphan_stmt;
                            int add_orphan_rc = sqlite3_prepare_v2(db, add_orphan_sql, -1, &add_orphan_stmt, NULL);
                            if (add_orphan_rc == SQLITE_OK) {
                                sqlite3_bind_text(add_orphan_stmt, 1, session_id.c_str(), -1, SQLITE_TRANSIENT);
                                sqlite3_bind_int64(add_orphan_stmt, 2, sqlite3_column_int64(find_orphans_stmt, 0));
                                sqlite3_step(add_orphan_stmt);
                                sqlite3_finalize(add_orphan_stmt);
                            }
                        }
                        sqlite3_finalize(find_orphans_stmt);
                    }
                }
                
                LOG_INFO("FetchAndStore_c: uuid=%s, session_id=%s, in_reply_to=%s, x_session_id=%s\n", 
                         uuid.c_str(), session_id.c_str(), in_reply_to.c_str(), x_session_id.c_str());
            }
        }

        // Build response with stored count
        response["stored"] = stored_count;
        std::string jsonStr = response.dump();
        if (outJson && outSize > 0) {
            snprintf(outJson, outSize, "%s", jsonStr.c_str());
        }

        // Notify via callback if set
        if (oemailim::EmailHandler::g_callback) {
            nlohmann::json notify_json;
            notify_json["event"] = "emails_fetched";
            notify_json["account"] = accountStr;
            notify_json["folder"] = folderStr;
            notify_json["count"] = stored_count;
            oemailim::EmailHandler::g_callback(configIndex, 5, notify_json.dump()); // 5 = NOTIFICATION_MESSAGE_EMAILS_FETCHED
        }

        return 0;
    } catch (const std::exception& e) {
        if (outJson && outSize > 0) {
            snprintf(outJson, outSize, R"({"status":"failed", "error":"%s"})", e.what());
        }
        return -7;
    } catch (...) {
        if (outJson && outSize > 0) {
            snprintf(outJson, outSize, R"({"status":"failed", "error":"exception"})");
        }
        return -8;
    }
}

// Static mutex removed - now using shared mutex from email_core.cpp via email_core_get_db_mutex()

// Get max UID from localemail table for a specific account
extern "C" int EmailGetMaxUid_c(const char* account, const char* folder, char* outUid, int outSize) {
    if (!account || !folder) return -2;

    // Use shared mutex from email_core.cpp to protect SQLite operations
    std::lock_guard<std::mutex> lock(email_core_get_db_mutex());
    
    sqlite3* db = email_core_get_db();
    if (!db) return -1;
    
    const char* sql = "SELECT MAX(uuid) FROM localemail WHERE account = ? AND folder = ?;";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        return -3;
    }

    sqlite3_bind_text(stmt, 1, account, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, folder, -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        const char* maxUid = (const char*)sqlite3_column_text(stmt, 0);
        if (maxUid && outUid && outSize > 0) {
            snprintf(outUid, outSize, "%s", maxUid);
        }
    }

    sqlite3_finalize(stmt);
    return 0;
}

// Query thread root emails (first email of each conversation thread)
extern "C" int EmailQueryThreadRoots_c(const char* account, char* outJson, int outSize) {
    return email_query_thread_roots(account, outJson, outSize);
}

// C wrapper for EmailGetMaxUid_c
extern "C" int EmailGetMaxUid_c_wrapper(const char* account, const char* folder, char* outUid, int outSize) {
    return EmailGetMaxUid_c(account, folder, outUid, outSize);
}

// C wrapper for email_get_max_uid
extern "C" int email_get_max_uid(const char* account, const char* folder, char* outUid, int outSize) {
    return EmailGetMaxUid_c(account, folder, outUid, outSize);
}

int IdleWait_c(int configIndex, const char* folder, int timeoutSeconds) {
    try {
        if (configIndex < 0 || configIndex >= static_cast<int>(oemailim::EmailHandler::g_EmailConfigIndices.size())) {
            return -1;
        }

        auto emailObj = oemailim::EmailHandler::g_EmailConfigIndices[configIndex];
        if (!emailObj) return -2;

        auto delegate = emailObj->get_delegate();
        if (!delegate) return -3;

        std::string folderStr = folder ? folder : "INBOX";
        bool result = delegate->idle_wait(folderStr, timeoutSeconds);

        // Log the result and error for debugging (std::cerr is redirected to log file)
        if (!result) {
            auto err = delegate->get_last_error();
            LOG_INFO("[IDLE] idle_wait returned false for folder %s, last_error: %s", folderStr.c_str(), err.c_str());
        }

        return result ? 0 : -5;
    } catch (const std::exception& e) {
        LOG_INFO("[IDLE] IdleWait_c exception: %s", e.what());
        return -5;
    } catch (...) {
        return -6;
    }
}

int FindSentFolder_c(int configIndex, char* outFolder, int outSize) {
    try {
        if (configIndex < 0 || configIndex >= static_cast<int>(oemailim::EmailHandler::g_EmailConfigIndices.size())) {
            return -1;
        }

        auto emailObj = oemailim::EmailHandler::g_EmailConfigIndices[configIndex];
        if (!emailObj) return -2;

        auto delegate = emailObj->get_delegate();
        if (!delegate) return -3;

        std::string sentFolder = delegate->find_sent_folder();
        if (outFolder && outSize > 0) {
            snprintf(outFolder, outSize, "%s", sentFolder.c_str());
        }
        return 0;
    } catch (const std::exception& e) {
        LOG_INFO("FindSentFolder_c exception: %s\n", e.what());
        return -4;
    } catch (...) {
        return -5;
    }
}

int SendEmail_c(int configIndex, const char* content) {
    try {
        if (configIndex < 0 || configIndex >= static_cast<int>(oemailim::EmailHandler::g_EmailConfigIndices.size())) {
            return -1;
        }

        auto emailObj = oemailim::EmailHandler::g_EmailConfigIndices[configIndex];
        if (!emailObj) return -2;

        auto delegate = emailObj->get_delegate();
        if (!delegate) return -3;

        // Set SMTP server from the Email object's SMTP settings
        auto outlookDelegate = std::dynamic_pointer_cast<EmailComm::EmailOptOutlookImpl>(delegate);
        if (outlookDelegate) {
            outlookDelegate->set_smtp_server(emailObj->get_smtp_address(), emailObj->get_smtp_port());
        }

        bool ok = delegate->send_email("", content ? content : "");
        return ok ? 0 : -4;
    } catch (const std::exception& e) {
        return -5;
    } catch (...) {
        return -6;
    }
}

int GetLastError_c(int configIndex, char* outBuf, int outSize) {
    try {
        if (configIndex < 0 || configIndex >= static_cast<int>(oemailim::EmailHandler::g_EmailConfigIndices.size())) {
            return -1;
        }
        auto emailObj = oemailim::EmailHandler::g_EmailConfigIndices[configIndex];
        if (!emailObj) return -2;
        auto delegate = emailObj->get_delegate();
        if (!delegate) return -3;
        std::string err = delegate->get_last_error();
        if (outBuf && outSize > 0) {
            snprintf(outBuf, outSize, "%s", err.c_str());
        }
        return 0;
    } catch (...) {
        return -4;
    }
}

} // extern "C"
