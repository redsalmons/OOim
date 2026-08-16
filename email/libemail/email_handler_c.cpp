#include "email_handler_c.h"
#include "email_handler.h"
#include "email.h"
#include "email_opt_163_impl.h"
#include "email_opt_outlook_impl.h"
#include "email_opt_gmail_impl.h"
#include "logger.h"
#include "db_connection.h"
#include "email_repo.h"
#include "session_repo.h"
#include <string>
#include <nlohmann/json.hpp>
#include <cstring>
#include <mutex>


// Forward declaration for db handle and mutex from email_core_utils.cpp
extern "C" sqlite3* email_core_get_db();
std::mutex& email_core_get_db_mutex();

static EmailRepo s_emailRepo;
static SessionRepo s_sessionRepo;

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
            LOG_INFO("SetImapServer_c: Successfully set IMAP server (Outlook)\n");
            return 0;
        }
        // Cast to EmailOpt163Impl to set IMAP server (also handles QQ)
        auto impl163 = std::dynamic_pointer_cast<EmailComm::EmailOpt163Impl>(delegate);
        if (impl163) {
            impl163->set_imap_server(server, port);
            LOG_INFO("SetImapServer_c: Successfully set IMAP server (163/QQ)\n");
            return 0;
        }
        LOG_INFO("SetImapServer_c: Delegate type not recognized\n");
        return -4;
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
        
        // Use the provider-specific implementation for fetch_email_headers
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
                auto implGmail = std::dynamic_pointer_cast<EmailComm::EmailOptGmailImpl>(delegate);
                if (implGmail) {
                    LOG_INFO("FetchAndStore_c: Using Gmail implementation");
                    headersJson = implGmail->fetch_email_headers(folderStr, startUidStr);
                } else {
                    LOG_INFO("FetchAndStore_c: Unsupported provider type for configIndex %d", configIndex);
                    if (outJson && outSize > 0) {
                        snprintf(outJson, outSize, R"({"status":"failed", "error":"unsupported_provider"})");
                    }
                    return -4;
                }
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
        auto& conn = DbConnection::instance();
        sqlite3* db = conn.get();
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
            std::string x_session_chart = email_data.value("x_session_chart", "");

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
                existing_id = s_emailRepo.findIdByMessageId(message_id, accountStr);
                if (existing_id > 0) {
                    found_existing = true;
                }
            }

            if (found_existing) {
                // Update the existing record with real data from IMAP
                EmailRecord updateRec;
                updateRec.uuid = uuid;
                updateRec.sender = sender;
                updateRec.fromAddr = from_addr;
                updateRec.subject = subject;
                updateRec.date = date;
                updateRec.bodystructure = bodystructure;
                updateRec.replyTo = reply_to;
                updateRec.inReplyTo = in_reply_to;
                updateRec.flags = flags;
                updateRec.folder = folder;
                updateRec.servicerecvtime = servicerecvtime;
                updateRec.toAddr = email_data.value("to_addr", "");
                if (!s_emailRepo.updateById(existing_id, updateRec)) {
                    LOG_INFO("FetchAndStore_c: UPDATE failed for id=%lld\n", (long long)existing_id);
                } else {
                    LOG_INFO("FetchAndStore_c: Updated id=%lld, uuid='%s', folder='%s'\n", (long long)existing_id, uuid.c_str(), folder.c_str());
                }

                // No need to update session table — email_id stores localemail.id which doesn't change
                stored_count++;
                continue;
            }

            // No existing record found — insert a new one
            EmailRecord insertRec;
            insertRec.uuid = uuid;
            insertRec.account = accountStr;
            insertRec.sender = sender;
            insertRec.fromAddr = from_addr;
            insertRec.subject = subject;
            insertRec.date = date;
            insertRec.bodystructure = bodystructure;
            insertRec.replyTo = reply_to;
            insertRec.inReplyTo = in_reply_to;
            insertRec.messageId = message_id;
            insertRec.flags = flags;
            insertRec.folder = folder;
            insertRec.servicerecvtime = servicerecvtime;
            insertRec.toAddr = email_data.value("to_addr", "");
            // No X-Session-Chart header → mark as islocal=2 (no need to download body)
            insertRec.isLocal = x_session_chart.empty() ? 2 : 0;
            int64_t my_rowid = s_emailRepo.insert(insertRec);

            if (my_rowid > 0 && folder == "INBOX") {
                stored_count++;

                LOG_INFO("FetchAndStore_c: uuid=%s, message_id=%s, in_reply_to=%s, x_session_chart=%s\n", 
                         uuid.c_str(), message_id.c_str(), in_reply_to.c_str(), x_session_chart.c_str());

                // Session is only created via X-Session-Chart=new in download_pending_bodies
                // Here we only match in_reply_to to join existing sessions
                if (x_session_chart != "new" && !in_reply_to.empty()) {
                    std::string session_id = s_sessionRepo.querySessionByInReplyTo(in_reply_to, accountStr);
                    if (!session_id.empty()) {
                        s_sessionRepo.insertSessionAssoc(session_id, my_rowid);
                        LOG_INFO("FetchAndStore_c: matched in_reply_to to session=%s, email_id=%lld\n", session_id.c_str(), my_rowid);
                    }
                }
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
    auto& conn = DbConnection::instance();
    std::lock_guard<std::mutex> lock(conn.mutex());
    
    if (!conn.get()) return -1;
    
    std::string maxUid = s_emailRepo.getMaxUid(account, folder);
    if (!maxUid.empty() && outUid && outSize > 0) {
        snprintf(outUid, outSize, "%s", maxUid.c_str());
    }
    
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
        auto delegate163 = std::dynamic_pointer_cast<EmailComm::EmailOpt163Impl>(delegate);
        if (delegate163) {
            delegate163->set_smtp_server(emailObj->get_smtp_address(), emailObj->get_smtp_port());
        }
        auto delegateGmail = std::dynamic_pointer_cast<EmailComm::EmailOptGmailImpl>(delegate);
        if (delegateGmail) {
            delegateGmail->set_smtp_server(emailObj->get_smtp_address(), emailObj->get_smtp_port());
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
