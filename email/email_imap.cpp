#include "email_core_common.h"
#include "email_core.h"
#include "logger.h"
#include "email_handler_c.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

struct IMAPConnection {
    int config_index;
    char* email;
    int connected;
};

static int libemail_initialized = 0;

IMAPConnection* imap_create(const char* server, int port, const char* email, const char* auth_code) {
    LOG_INFO("[IMAP] Creating connection to %s:%d for %s using libemail\n", server, port, email);

    if (!libemail_initialized) {
        const char* dataDir = "/tmp/oim_data";
        const char* configDir = "/tmp/oim_config";
        const char* logDir = "/tmp/oim_log";

        int result = systemOpen_c(dataDir, configDir, logDir);
        if (result != 1) {
            LOG_INFO("[IMAP] Failed to initialize libemail system\n");
            return NULL;
        }
        libemail_initialized = 1;
        LOG_INFO("[IMAP] libemail system initialized\n");
    }

    LOG_INFO("[IMAP] imap_create is deprecated, use email_id-based OpenNewEmail_c instead\n");
    return NULL;
}

void imap_destroy(IMAPConnection* conn) {
    if (!conn) return;
    if (conn->connected) {
        systemClose_c(conn->config_index);
    }
    free(conn->email);
    free(conn);
}

int imap_fetch_inbox(IMAPConnection* conn) {
    if (!conn) {
        LOG_INFO("[IMAP] Connection invalid\n");
        return -1;
    }

    LOG_INFO("[IMAP] Fetching inbox for %s using libemail\n", conn->email);

    int auth_result = Authority_c(conn->config_index);
    if (auth_result != 0) {
        LOG_INFO("[IMAP] Authority failed: %d\n", auth_result);
        return -1;
    }

    LOG_INFO("[IMAP] Authority successful, connection established\n");
    conn->connected = 1;

    char json_buffer[8192];
    int list_result = Email_List_c(conn->config_index, "INBOX", json_buffer, sizeof(json_buffer));
    if (list_result != 0) {
        LOG_INFO("[IMAP] Email list failed: %d\n", list_result);
        return -1;
    }

    LOG_INFO("[IMAP] Email list result: %s\n", json_buffer);

    try {
        json email_list = json::parse(json_buffer);
        if (email_list.contains("uids") && email_list["uids"].is_array()) {
            auto uids = email_list["uids"];
            LOG_INFO("[IMAP] Found %d emails, fetching content...\n", (int)uids.size());

            for (const auto& uid_val : uids) {
                std::string uid = uid_val.get<std::string>();

                char email_buffer[65536];
                int get_result = GetEmail_c(conn->config_index, "INBOX", uid.c_str(), email_buffer, sizeof(email_buffer));
                if (get_result != 0) {
                    LOG_INFO("[IMAP] Failed to fetch email uid=%s: %d\n", uid.c_str(), get_result);
                    continue;
                }

                try {
                    json email_data = json::parse(email_buffer);
                    std::string content = email_data.value("content", "");

                    std::string sender = "unknown@example.com";
                    std::string subject = "No Subject";

                    size_t from_pos = content.find("From: ");
                    if (from_pos != std::string::npos) {
                        size_t end = content.find("\r\n", from_pos);
                        if (end == std::string::npos) end = content.find("\n", from_pos);
                        if (end != std::string::npos) {
                            sender = content.substr(from_pos + 6, end - from_pos - 6);
                        }
                    }

                    size_t subj_pos = content.find("Subject: ");
                    if (subj_pos != std::string::npos) {
                        size_t end = content.find("\r\n", subj_pos);
                        if (end == std::string::npos) end = content.find("\n", subj_pos);
                        if (end != std::string::npos) {
                            subject = content.substr(subj_pos + 9, end - subj_pos - 9);
                        }
                    }

                    LOG_INFO("[IMAP] Fetched email: %s - %s\n", sender.c_str(), subject.c_str());
                } catch (const std::exception& e) {
                    LOG_INFO("[IMAP] Failed to parse email content: %s\n", e.what());
                }
            }
        } else {
            LOG_INFO("[IMAP] No uids in response\n");
        }
    } catch (const std::exception& e) {
        LOG_INFO("[IMAP] Failed to parse email list JSON: %s\n", e.what());
    }

    LOG_INFO("[IMAP] Fetch completed\n");
    return 0;
}

// High-level email connection API wrappers
int email_set_credentials(int configIndex, const char* email, const char* authCode) {
    return SetCredentials_c(configIndex, email, authCode);
}

int email_connect(int configIndex) {
    return Authority_c(configIndex);
}

int email_list(int configIndex, const char* folder, char* outJson, int outSize) {
    return Email_List_c(configIndex, folder, outJson, outSize);
}

int email_get_content(int configIndex, const char* folder, const char* uid, char* outJson, int outSize) {
    return GetEmail_c(configIndex, folder, uid, outJson, outSize);
}

int email_fetch_and_store(int configIndex, const char* folder, const char* startUid,
                          const char* account, char* outJson, int outSize) {
    return FetchAndStore_c(configIndex, folder, startUid, account, outJson, outSize);
}
