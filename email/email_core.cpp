#include "email_core.h"
#include "email_handler_c.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sqlite3.h>
#include <time.h>
#include <stdarg.h>
#include <random>
#include <sstream>
#include <iostream>
#include <fstream>
#include <mutex>
#include <chrono>
#include <nlohmann/json.hpp>
#include <vmime/vmime.hpp>
#include <vmime/platforms/posix/posixHandler.hpp>
#include <vmime/security/cert/defaultCertificateVerifier.hpp>
#include <vmime/net/smtp/SMTPTransport.hpp>
#include <vmime/contentDispositionField.hpp>
#include <vmime/contentTypeField.hpp>

using json = nlohmann::json;

static std::string generate_email_id(const char* type) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(10000000, 99999999);
    std::ostringstream oss;
    oss << dis(gen) << "." << (type ? type : "");
    return oss.str();
}

#define VERSION "1.0.0"
#define MAX_EMAILS 1000
#define DEFAULT_CAPACITY 100

static int system_initialized = 0;


// Unified log function - callable from Dart via FFI
extern "C" void email_log_write(const char* message) {
    if (message) {
        LOG_INFO("%s", message);
    }
}

// std::cout/std::cerr redirection removed - all logging goes through Logger

EmailHandler* email_handler_create(int initial_capacity) {
    LOG_INFO("email_handler_create called with capacity: %d", initial_capacity);

    if (initial_capacity <= 0) {
        initial_capacity = DEFAULT_CAPACITY;
    }

    EmailHandler* handler = (EmailHandler*)malloc(sizeof(EmailHandler));
    if (!handler) {
        LOG_INFO("Failed to allocate memory for EmailHandler");
        return NULL;
    }

    handler->emails = (Email*)calloc(initial_capacity, sizeof(Email));
    if (!handler->emails) {
        free(handler);
        return NULL;
    }

    handler->count = 0;
    handler->capacity = initial_capacity;

    return handler;
}

// Initialize logger only - call once at app startup
extern "C" int email_logger_init(const char* logDir) {
    if (!logDir) return -1;
    oemail::Logger::getInstance().init(logDir);
    LOG_INFO("Logger initialized with logDir: %s", logDir);
    return 0;
}

// Initialize libemail system with paths provided from Dart
int email_core_initialize(const char* appSupportDir) {
    if (!appSupportDir) {
        return -1;
    }

    char dataDir[512], configDir[512], logDir[512];
    snprintf(dataDir, sizeof(dataDir), "%s/data", appSupportDir);
    snprintf(configDir, sizeof(configDir), "%s/config", appSupportDir);
    snprintf(logDir, sizeof(logDir), "%s/log", appSupportDir);

    LOG_INFO("Initializing libemail system with dataDir: %s, configDir: %s, logDir: %s", dataDir, configDir, logDir);
    int initResult = systemOpen_c(dataDir, configDir, logDir);
    LOG_INFO("systemOpen_c result: %d", initResult);

    return initResult;
}

void email_handler_destroy(EmailHandler* handler) {
    if (!handler) {
        return;
    }

    for (int i = 0; i < handler->count; i++) {
        email_free(&handler->emails[i]);
    }

    free(handler->emails);
    free(handler);
}

int email_add(EmailHandler* handler, const char* sender, const char* recipient,
              const char* subject, const char* body) {
    if (!handler || !sender || !recipient || !subject || !body) {
        return -1;
    }

    if (handler->count >= handler->capacity) {
        return -2; // Capacity exceeded
    }

    Email* email = &handler->emails[handler->count];

    email->sender = strdup(sender);
    email->recipient = strdup(recipient);
    email->subject = strdup(subject);
    email->body = strdup(body);

    // Generate timestamp
    time_t now = time(NULL);
    char* timestamp = ctime(&now);
    if (timestamp) {
        timestamp[strlen(timestamp) - 1] = '\0'; // Remove newline
        email->timestamp = strdup(timestamp);
    } else {
        email->timestamp = strdup("Unknown");
    }

    if (!email->sender || !email->recipient || !email->subject || 
        !email->body || !email->timestamp) {
        email_free(email);
        return -3; // Memory allocation failed
    }

    handler->count++;
    return 0;
}

int email_remove(EmailHandler* handler, int index) {
    if (!handler || index < 0 || index >= handler->count) {
        return -1;
    }

    email_free(&handler->emails[index]);

    // Shift remaining emails
    for (int i = index; i < handler->count - 1; i++) {
        handler->emails[i] = handler->emails[i + 1];
    }

    handler->count--;
    return 0;
}

Email* email_get(EmailHandler* handler, int index) {
    if (!handler || index < 0 || index >= handler->count) {
        return NULL;
    }

    return &handler->emails[index];
}

int email_count(EmailHandler* handler) {
    if (!handler) {
        return -1;
    }

    return handler->count;
}

int email_send(const char* recipient, const char* subject, const char* body) {
    if (!recipient || !subject || !body) {
        return -1;
    }

    LOG_INFO("[EMAIL_CORE] Sending email to: %s\n", recipient);
    LOG_INFO("[EMAIL_CORE] Subject: %s\n", subject);
    LOG_INFO("[EMAIL_CORE] Body: %s\n", body);
    
    // Simulate email sending
    // In a real implementation, this would connect to an SMTP server
    return 0;
}

int email_send_with_headers(const char* recipient, const char* subject, const char* body, const char* in_reply_to) {
    if (!recipient || !subject || !body) {
        return -1;
    }

    LOG_INFO("[EMAIL_CORE] Sending email to: %s\n", recipient);
    LOG_INFO("[EMAIL_CORE] Subject: %s\n", subject);
    LOG_INFO("[EMAIL_CORE] Body: %s\n", body);
    if (in_reply_to && strlen(in_reply_to) > 0) {
        LOG_INFO("[EMAIL_CORE] In-Reply-To: %s\n", in_reply_to);
    }

    // Simulate email sending
    // In a real implementation, this would connect to an SMTP server
    // and include the In-Reply-To header in the message
    return 0;
}

// Trust-all certificate verifier for SMTP
class SMTPTrustAllVerifier : public vmime::security::cert::defaultCertificateVerifier {
public:
    void verify(
        const vmime::shared_ptr<vmime::security::cert::certificateChain>& chain,
        const vmime::string& hostname
    ) override {
        // Accept all certificates
    }
};

int email_send_via_smtp(const char* smtp_server, int smtp_port,
                        const char* sender_email, const char* auth_code,
                        const char* recipient, const char* subject,
                        const char* body, const char* in_reply_to) {
    if (!smtp_server || !sender_email || !auth_code || !recipient || !subject || !body) {
        LOG_INFO( "[SMTP] Missing required parameters\n");
        return -1;
    }

    LOG_INFO("[SMTP] Sending via %s:%d from %s to %s\n", smtp_server, smtp_port, sender_email, recipient);
    LOG_INFO("[SMTP] Subject: %s\n", subject);

    try {
        // Initialize vmime platform
        static bool vmime_initialized = false;
        if (!vmime_initialized) {
            vmime::platform::setHandler<vmime::platforms::posix::posixHandler>();
            vmime_initialized = true;
        }

        // Create session
        vmime::shared_ptr<vmime::net::session> sess = vmime::net::session::create();

        // Build SMTP URL: smtps://smtp.example.com:465
        std::string url_str = "smtps://" + std::string(smtp_server) + ":" + std::to_string(smtp_port);
        vmime::utility::url url(url_str);

        // Get transport
        vmime::shared_ptr<vmime::net::transport> tr = sess->getTransport(url);
        tr->setProperty("options.need-authentication", true);
        tr->setProperty("auth.username", sender_email);
        tr->setProperty("auth.password", auth_code);

        // Disable SSL certificate verification
        tr->setCertificateVerifier(vmime::make_shared<SMTPTrustAllVerifier>());

        // Connect
        tr->connect();
        LOG_INFO("[SMTP] Connected to %s\n", url_str.c_str());

        // Build the message
        vmime::shared_ptr<vmime::message> msg = vmime::make_shared<vmime::message>();

        // Set From
        vmime::mailbox from(sender_email);
        msg->getHeader()->From()->setValue(from);

        // Set To (support comma-separated recipients)
        std::string recipientsStr(recipient);
        vmime::addressList toList;
        // Split by comma
        size_t start = 0, end;
        while ((end = recipientsStr.find(',', start)) != std::string::npos) {
            std::string addr = recipientsStr.substr(start, end - start);
            // trim whitespace
            size_t b = addr.find_first_not_of(" \t");
            size_t e = addr.find_last_not_of(" \t");
            if (b != std::string::npos) {
                addr = addr.substr(b, e - b + 1);
                toList.appendAddress(vmime::make_shared<vmime::mailbox>(addr));
            }
            start = end + 1;
        }
        // Last recipient
        {
            std::string addr = recipientsStr.substr(start);
            size_t b = addr.find_first_not_of(" \t");
            size_t e = addr.find_last_not_of(" \t");
            if (b != std::string::npos) {
                addr = addr.substr(b, e - b + 1);
                toList.appendAddress(vmime::make_shared<vmime::mailbox>(addr));
            }
        }
        msg->getHeader()->To()->setValue(toList);

        // Set Subject
        msg->getHeader()->Subject()->setValue(vmime::text(subject));

        // Set Date
        msg->getHeader()->Date()->setValue(vmime::datetime::now());

        // Set Message-ID
        msg->getHeader()->MessageId()->setValue(vmime::messageId::generateId());

        // Set In-Reply-To if provided
        if (in_reply_to && strlen(in_reply_to) > 0) {
            msg->getHeader()->InReplyTo()->setValue(
                vmime::make_shared<vmime::messageId>(in_reply_to));
        }

        // Set body
        msg->getBody()->setContents(vmime::make_shared<vmime::stringContentHandler>(body));

        // Send
        tr->send(msg);
        LOG_INFO("[SMTP] Email sent successfully\n");

        // Disconnect
        tr->disconnect();

        return 0;
    } catch (const vmime::exception& e) {
        LOG_INFO( "[SMTP] VMime error: %s\n", e.what());
        return -2;
    } catch (const std::exception& e) {
        LOG_INFO( "[SMTP] Error: %s\n", e.what());
        return -3;
    }
}

int email_receive(EmailHandler* handler) {
    if (!handler) {
        return -1;
    }

    LOG_INFO("[EMAIL_CORE] Receiving emails...\n");
    
    // Simulate receiving emails
    // In a real implementation, this would connect to an IMAP/POP3 server
    return 0;
}

int email_search(EmailHandler* handler, const char* query, Email** results, int* result_count) {
    if (!handler || !query || !results || !result_count) {
        return -1;
    }

    int found = 0;
    Email* search_results = (Email*)malloc(handler->count * sizeof(Email));
    if (!search_results) {
        return -2;
    }

    for (int i = 0; i < handler->count; i++) {
        Email* email = &handler->emails[i];
        
        if (strstr(email->sender, query) || strstr(email->recipient, query) ||
            strstr(email->subject, query) || strstr(email->body, query)) {
            // Copy email to results
            search_results[found].sender = strdup(email->sender);
            search_results[found].recipient = strdup(email->recipient);
            search_results[found].subject = strdup(email->subject);
            search_results[found].body = strdup(email->body);
            search_results[found].timestamp = strdup(email->timestamp);
            found++;
        }
    }

    *results = search_results;
    *result_count = found;
    return 0;
}

void email_free(Email* email) {
    if (!email) {
        return;
    }

    if (email->sender) free(email->sender);
    if (email->recipient) free(email->recipient);
    if (email->subject) free(email->subject);
    if (email->body) free(email->body);
    if (email->timestamp) free(email->timestamp);

    email->sender = NULL;
    email->recipient = NULL;
    email->subject = NULL;
    email->body = NULL;
    email->timestamp = NULL;
}

const char* get_version(void) {
    return VERSION;
}

int initialize_email_system(void) {
    if (system_initialized) {
        return 0;
    }

    LOG_INFO("[EMAIL_CORE] Initializing email system...\n");
    system_initialized = 1;
    return 0;
}

void shutdown_email_system(void) {
    if (!system_initialized) {
        return;
    }

    LOG_INFO("[EMAIL_CORE] Shutting down email system...\n");
    system_initialized = 0;
}

static char* trim_newline(char* s) {
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r')) {
        s[--len] = '\0';
    }
    return s;
}

int email_config_save(const char* path, const char* local_data_path,
                       const EmailAccountConfig* accounts, int count) {
    if (!path) {
        return -1;
    }

    json root;
    root["local_data_path"] = local_data_path ? local_data_path : "";
    root["accounts"] = json::array();

    for (int i = 0; i < count; i++) {
        const EmailAccountConfig* acc = &accounts[i];
        json account_obj;
        account_obj["type"] = acc->type ? acc->type : "";
        account_obj["email"] = acc->email ? acc->email : "";
        account_obj["auth_code"] = acc->auth_code ? acc->auth_code : "";
        account_obj["smtp_server"] = acc->smtp_server ? acc->smtp_server : "";
        account_obj["smtp_port"] = acc->smtp_port;
        account_obj["imap_server"] = acc->imap_server ? acc->imap_server : "";
        account_obj["imap_port"] = acc->imap_port;
        account_obj["account_type"] = acc->account_type ? acc->account_type : "";
        account_obj["authorized"] = acc->authorized;

        std::string id_str = acc->id ? acc->id : "";
        if (id_str.empty()) {
            id_str = generate_email_id(acc->type);
            LOG_INFO("email_config_save: generated new id '%s' for account type '%s'",
                     id_str.c_str(), acc->type ? acc->type : "");
        }
        account_obj["id"] = id_str;

        account_obj["uid"] = acc->uid;
        account_obj["phrase"] = acc->phrase ? acc->phrase : "";
        account_obj["folder_size"] = acc->folder_size;
        root["accounts"].push_back(account_obj);
    }

    std::string json_str = root.dump(2);

    FILE* f = fopen(path, "w");
    if (!f) {
        return -2;
    }

    fprintf(f, "%s\n", json_str.c_str());
    fclose(f);

    return 0;
}

int email_config_exists(const char* path) {
    if (!path) {
        return 0;
    }
    FILE* f = fopen(path, "r");
    if (!f) {
        return 0;
    }
    fclose(f);
    return 1;
}

static void set_field(char** field, const char* value) {
    if (*field) {
        free(*field);
    }
    *field = strdup(value ? value : "");
}

int email_config_load(const char* path, char** local_data_path,
                       EmailAccountConfig** accounts, int* count) {
    if (!path || !local_data_path || !accounts || !count) {
        return -1;
    }

    FILE* f = fopen(path, "r");
    if (!f) {
        return -2;
    }

    *local_data_path = strdup("");
    *accounts = NULL;
    *count = 0;

    // Read entire file
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* json_str = (char*)malloc(file_size + 1);
    if (!json_str) {
        fclose(f);
        return -3;
    }
    fread(json_str, 1, file_size, f);
    json_str[file_size] = '\0';
    fclose(f);

    // Parse JSON using nlohmann::json
    try {
        json root = json::parse(json_str);
        free(json_str);

        // Get local_data_path
        if (root.contains("local_data_path") && root["local_data_path"].is_string()) {
            free(*local_data_path);
            *local_data_path = strdup(root["local_data_path"].get<std::string>().c_str());
        }

        // Get accounts array
        if (!root.contains("accounts") || !root["accounts"].is_array()) {
            return -4;
        }

        int capacity = 0;
        json accounts_array = root["accounts"];

        for (const auto& account_obj : accounts_array) {
            if (!account_obj.is_object()) {
                continue;
            }

            EmailAccountConfig current;
            memset(&current, 0, sizeof(current));

            if (account_obj.contains("type") && account_obj["type"].is_string()) {
                current.type = strdup(account_obj["type"].get<std::string>().c_str());
            }

            if (account_obj.contains("email") && account_obj["email"].is_string()) {
                current.email = strdup(account_obj["email"].get<std::string>().c_str());
            }

            if (account_obj.contains("auth_code") && account_obj["auth_code"].is_string()) {
                current.auth_code = strdup(account_obj["auth_code"].get<std::string>().c_str());
            }

            if (account_obj.contains("smtp_server") && account_obj["smtp_server"].is_string()) {
                current.smtp_server = strdup(account_obj["smtp_server"].get<std::string>().c_str());
            }

            if (account_obj.contains("smtp_port") && account_obj["smtp_port"].is_number()) {
                current.smtp_port = account_obj["smtp_port"].get<int>();
            }

            if (account_obj.contains("imap_server") && account_obj["imap_server"].is_string()) {
                current.imap_server = strdup(account_obj["imap_server"].get<std::string>().c_str());
            }

            if (account_obj.contains("imap_port") && account_obj["imap_port"].is_number()) {
                current.imap_port = account_obj["imap_port"].get<int>();
            }

            if (account_obj.contains("account_type") && account_obj["account_type"].is_string()) {
                current.account_type = strdup(account_obj["account_type"].get<std::string>().c_str());
            }

            if (account_obj.contains("authorized") && account_obj["authorized"].is_number()) {
                current.authorized = account_obj["authorized"].get<int>();
            }

            if (account_obj.contains("id") && account_obj["id"].is_string()) {
                std::string id_str = account_obj["id"].get<std::string>();
                if (id_str.empty() && current.type) {
                    id_str = generate_email_id(current.type);
                }
                current.id = strdup(id_str.c_str());
            }

            if (account_obj.contains("uid") && account_obj["uid"].is_number()) {
                current.uid = account_obj["uid"].get<int>();
            }

            if (account_obj.contains("phrase") && account_obj["phrase"].is_string()) {
                current.phrase = strdup(account_obj["phrase"].get<std::string>().c_str());
            }

            if (account_obj.contains("folder_size") && account_obj["folder_size"].is_number()) {
                current.folder_size = account_obj["folder_size"].get<long long>();
            }

            // Add to array
            if (*count >= capacity) {
                capacity = capacity == 0 ? 4 : capacity * 2;
                EmailAccountConfig* resized = (EmailAccountConfig*)realloc(
                    *accounts, capacity * sizeof(EmailAccountConfig));
                if (!resized) {
                    return -3;
                }
                *accounts = resized;
            }
            (*accounts)[*count] = current;
            (*count)++;
        }

        return 0;
    } catch (const json::exception& e) {
        free(json_str);
        return -4;
    }
}

void email_config_free(EmailAccountConfig* accounts, int count) {
    if (!accounts) {
        return;
    }
    for (int i = 0; i < count; i++) {
        free(accounts[i].type);
        free(accounts[i].email);
        free(accounts[i].auth_code);
        free(accounts[i].smtp_server);
        free(accounts[i].imap_server);
        free(accounts[i].account_type);
        free(accounts[i].id);
        free(accounts[i].phrase);
    }
    free(accounts);
}

int email_save(EmailHandler* handler, const char* path) {
    if (!handler || !path) {
        return -1;
    }

    FILE* f = fopen(path, "w");
    if (!f) {
        return -2;
    }

    fprintf(f, "email_count=%d\n", handler->count);

    for (int i = 0; i < handler->count; i++) {
        const Email* email = &handler->emails[i];
        fprintf(f, "[email]\n");
        fprintf(f, "sender=%s\n", email->sender ? email->sender : "");
        fprintf(f, "recipient=%s\n", email->recipient ? email->recipient : "");
        fprintf(f, "subject=%s\n", email->subject ? email->subject : "");
        fprintf(f, "body=%s\n", email->body ? email->body : "");
        fprintf(f, "timestamp=%s\n", email->timestamp ? email->timestamp : "");
        fprintf(f, "[end]\n");
    }

    fclose(f);
    return 0;
}

int email_load(EmailHandler* handler, const char* path) {
    if (!handler || !path) {
        return -1;
    }

    FILE* f = fopen(path, "r");
    if (!f) {
        return -2;
    }

    // Clear existing emails
    for (int i = 0; i < handler->count; i++) {
        email_free(&handler->emails[i]);
    }
    handler->count = 0;

    int in_email = 0;
    Email current;
    memset(&current, 0, sizeof(current));

    char line[4096];
    while (fgets(line, sizeof(line), f)) {
        trim_newline(line);
        if (line[0] == '\0') {
            continue;
        }

        if (strcmp(line, "[email]") == 0) {
            in_email = 1;
            memset(&current, 0, sizeof(current));
            continue;
        }

        if (strcmp(line, "[end]") == 0) {
            if (in_email) {
                if (handler->count < handler->capacity) {
                    handler->emails[handler->count] = current;
                    handler->count++;
                } else {
                    // Free the email if capacity exceeded
                    email_free(&current);
                }
            }
            in_email = 0;
            continue;
        }

        char* eq = strchr(line, '=');
        if (!eq) {
            continue;
        }
        *eq = '\0';
        const char* key = line;
        const char* value = eq + 1;

        if (strcmp(key, "sender") == 0) {
            current.sender = strdup(value);
        } else if (strcmp(key, "recipient") == 0) {
            current.recipient = strdup(value);
        } else if (strcmp(key, "subject") == 0) {
            current.subject = strdup(value);
        } else if (strcmp(key, "body") == 0) {
            current.body = strdup(value);
        } else if (strcmp(key, "timestamp") == 0) {
            current.timestamp = strdup(value);
        }
    }

    fclose(f);
    return 0;
}

int chat_contacts_save(const char* path, const ChatContact* contacts, int count) {
    if (!path) {
        return -1;
    }

    FILE* f = fopen(path, "w");
    if (!f) {
        return -2;
    }

    fprintf(f, "contact_count=%d\n", count);

    for (int i = 0; i < count; i++) {
        const ChatContact* contact = &contacts[i];
        fprintf(f, "[contact]\n");
        fprintf(f, "name=%s\n", contact->name ? contact->name : "");
        fprintf(f, "last_message=%s\n", contact->last_message ? contact->last_message : "");
        fprintf(f, "time=%s\n", contact->time ? contact->time : "");
        fprintf(f, "unread=%d\n", contact->unread);
        fprintf(f, "[end]\n");
    }

    fclose(f);
    return 0;
}

int chat_contacts_load(const char* path, ChatContact** contacts, int* count) {
    if (!path || !contacts || !count) {
        return -1;
    }

    FILE* f = fopen(path, "r");
    if (!f) {
        return -2;
    }

    int contact_count = 0;
    char line[4096];
    ChatContact* result = NULL;
    int capacity = 10;
    result = (ChatContact*)malloc(capacity * sizeof(ChatContact));
    if (!result) {
        fclose(f);
        return -3;
    }

    int in_contact = 0;
    ChatContact current;
    memset(&current, 0, sizeof(current));

    while (fgets(line, sizeof(line), f)) {
        trim_newline(line);
        if (line[0] == '\0') {
            continue;
        }

        if (strcmp(line, "[contact]") == 0) {
            in_contact = 1;
            memset(&current, 0, sizeof(current));
            continue;
        }

        if (strcmp(line, "[end]") == 0) {
            if (in_contact) {
                if (contact_count >= capacity) {
                    capacity *= 2;
                    ChatContact* new_result = (ChatContact*)realloc(result, capacity * sizeof(ChatContact));
                    if (!new_result) {
                        fclose(f);
                        free(result);
                        return -3;
                    }
                    result = new_result;
                }
                result[contact_count] = current;
                contact_count++;
            }
            in_contact = 0;
            continue;
        }

        char* eq = strchr(line, '=');
        if (!eq) {
            continue;
        }
        *eq = '\0';
        const char* key = line;
        const char* value = eq + 1;

        if (strcmp(key, "contact_count") == 0) {
            // Skip, we count actual contacts
        } else if (strcmp(key, "name") == 0) {
            set_field(&current.name, value);
        } else if (strcmp(key, "last_message") == 0) {
            set_field(&current.last_message, value);
        } else if (strcmp(key, "time") == 0) {
            set_field(&current.time, value);
        } else if (strcmp(key, "unread") == 0) {
            current.unread = atoi(value);
        }
    }

    fclose(f);
    *contacts = result;
    *count = contact_count;
    return 0;
}

void chat_contacts_free(ChatContact* contacts, int count) {
    if (!contacts) {
        return;
    }
    for (int i = 0; i < count; i++) {
        free(contacts[i].name);
        free(contacts[i].last_message);
        free(contacts[i].time);
    }
    free(contacts);
}

int chat_messages_save(const char* path, const ChatMessage* messages, int count) {
    if (!path) {
        return -1;
    }

    FILE* f = fopen(path, "w");
    if (!f) {
        return -2;
    }

    fprintf(f, "message_count=%d\n", count);

    for (int i = 0; i < count; i++) {
        const ChatMessage* msg = &messages[i];
        fprintf(f, "[message]\n");
        fprintf(f, "sender=%s\n", msg->sender ? msg->sender : "");
        fprintf(f, "content=%s\n", msg->content ? msg->content : "");
        fprintf(f, "time=%s\n", msg->time ? msg->time : "");
        fprintf(f, "is_me=%d\n", msg->is_me);
        fprintf(f, "[end]\n");
    }

    fclose(f);
    return 0;
}

int chat_messages_load(const char* path, ChatMessage** messages, int* count) {
    if (!path || !messages || !count) {
        return -1;
    }

    FILE* f = fopen(path, "r");
    if (!f) {
        return -2;
    }

    int message_count = 0;
    char line[4096];
    ChatMessage* result = NULL;
    int capacity = 10;
    result = (ChatMessage*)malloc(capacity * sizeof(ChatMessage));
    if (!result) {
        fclose(f);
        return -3;
    }

    int in_message = 0;
    ChatMessage current;
    memset(&current, 0, sizeof(current));

    while (fgets(line, sizeof(line), f)) {
        trim_newline(line);
        if (line[0] == '\0') {
            continue;
        }

        if (strcmp(line, "[message]") == 0) {
            in_message = 1;
            memset(&current, 0, sizeof(current));
            continue;
        }

        if (strcmp(line, "[end]") == 0) {
            if (in_message) {
                if (message_count >= capacity) {
                    capacity *= 2;
                    ChatMessage* new_result = (ChatMessage*)realloc(result, capacity * sizeof(ChatMessage));
                    if (!new_result) {
                        fclose(f);
                        free(result);
                        return -3;
                    }
                    result = new_result;
                }
                result[message_count] = current;
                message_count++;
            }
            in_message = 0;
            continue;
        }

        char* eq = strchr(line, '=');
        if (!eq) {
            continue;
        }
        *eq = '\0';
        const char* key = line;
        const char* value = eq + 1;

        if (strcmp(key, "message_count") == 0) {
            // Skip, we count actual messages
        } else if (strcmp(key, "sender") == 0) {
            set_field(&current.sender, value);
        } else if (strcmp(key, "content") == 0) {
            set_field(&current.content, value);
        } else if (strcmp(key, "time") == 0) {
            set_field(&current.time, value);
        } else if (strcmp(key, "is_me") == 0) {
            current.is_me = atoi(value);
        }
    }

    fclose(f);
    *messages = result;
    *count = message_count;
    return 0;
}

void chat_messages_free(ChatMessage* messages, int count) {
    if (!messages) {
        return;
    }
    for (int i = 0; i < count; i++) {
        free(messages[i].sender);
        free(messages[i].content);
        free(messages[i].time);
    }
    free(messages);
}

// ---------------------------------------------------------------------------
// SQLite Database Implementation
// ---------------------------------------------------------------------------

static sqlite3* g_db = NULL;
static std::mutex g_db_mutex;

int email_db_init(const char* path) {
    if (g_db != NULL) {
        return 0; // Already initialized
    }

    int rc = sqlite3_open_v2(path, &g_db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, NULL);
    if (rc != SQLITE_OK) {
        LOG_INFO( "Cannot open database: %s\n", sqlite3_errmsg(g_db));
        return -1;
    }

    // Enable WAL mode for concurrent read/write access (background isolate writes, UI reads)
    sqlite3_exec(g_db, "PRAGMA journal_mode=WAL;", NULL, NULL, NULL);
    // Set busy timeout to 5 seconds — if DB is locked, wait instead of returning SQLITE_BUSY immediately
    sqlite3_exec(g_db, "PRAGMA busy_timeout=5000;", NULL, NULL, NULL);

    char* err_msg = NULL;

    // Create localemail table for fetched email metadata
    const char* sql_localemail = "CREATE TABLE IF NOT EXISTS localemail ("
                                 "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                                 "uuid TEXT,"
                                 "account TEXT NOT NULL,"
                                 "sender TEXT,"
                                 "from_addr TEXT,"
                                 "to_addr TEXT,"
                                 "subject TEXT,"
                                 "date TEXT,"
                                 "bodystructure TEXT,"
                                 "reply_to TEXT,"
                                 "in_reply_to TEXT,"
                                 "message_id TEXT,"
                                 "flags TEXT,"
                                 "folder TEXT,"
                                 "islocal INTEGER DEFAULT 0,"
                                 "servicerecvtime TEXT"
                                 ");";
    rc = sqlite3_exec(g_db, sql_localemail, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        LOG_INFO( "SQL error (localemail): %s\n", err_msg);
        sqlite3_free(err_msg);
        sqlite3_close(g_db);
        g_db = NULL;
        return -3;
    }

    // Add reply_to column if it doesn't exist (for existing databases)
    const char* sql_add_reply_to = "ALTER TABLE localemail ADD COLUMN reply_to TEXT;";
    sqlite3_exec(g_db, sql_add_reply_to, NULL, NULL, &err_msg);
    // Ignore error if column already exists
    
    // Add to_addr column if it doesn't exist
    const char* sql_add_to_addr = "ALTER TABLE localemail ADD COLUMN to_addr TEXT;";
    sqlite3_exec(g_db, sql_add_to_addr, NULL, NULL, &err_msg);
    // Ignore error if column already exists

    // Add in_reply_to column if it doesn't exist (for existing databases)
    const char* sql_add_in_reply_to = "ALTER TABLE localemail ADD COLUMN in_reply_to TEXT;";
    sqlite3_exec(g_db, sql_add_in_reply_to, NULL, NULL, &err_msg);
    // Ignore error if column already exists

    // Add message_id column if it doesn't exist (for existing databases)
    const char* sql_add_message_id = "ALTER TABLE localemail ADD COLUMN message_id TEXT;";
    sqlite3_exec(g_db, sql_add_message_id, NULL, NULL, &err_msg);
    // Ignore error if column already exists

    // Add folder column if it doesn't exist (for existing databases)
    const char* sql_add_folder = "ALTER TABLE localemail ADD COLUMN folder TEXT;";
    sqlite3_exec(g_db, sql_add_folder, NULL, NULL, &err_msg);
    // Ignore error if column already exists

    // Add flags column if it doesn't exist (for existing databases)
    const char* sql_add_flags = "ALTER TABLE localemail ADD COLUMN flags TEXT;";
    sqlite3_exec(g_db, sql_add_flags, NULL, NULL, &err_msg);
    // Ignore error if column already exists

    // Add file column if it doesn't exist (for existing databases)
    const char* sql_add_file = "ALTER TABLE localemail ADD COLUMN file TEXT;";
    sqlite3_exec(g_db, sql_add_file, NULL, NULL, &err_msg);
    // Ignore error if column already exists

    // Create session table for conversation threading
    const char* sql_session = "CREATE TABLE IF NOT EXISTS session ("
                              "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                              "session_id TEXT NOT NULL,"
                              "email_id INTEGER NOT NULL,"
                              "visible INTEGER DEFAULT 1,"
                              "auto INTEGER DEFAULT 1"
                              ");";
    rc = sqlite3_exec(g_db, sql_session, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        LOG_INFO("SQL error (session): %s\n", err_msg);
        sqlite3_free(err_msg);
        sqlite3_close(g_db);
        g_db = NULL;
        return -3;
    }

    // Add auto column if it doesn't exist (for existing databases)
    sqlite3_exec(g_db, "ALTER TABLE session ADD COLUMN auto INTEGER DEFAULT 1;", NULL, NULL, &err_msg);
    if (err_msg) sqlite3_free(err_msg);

    // Add isread column if it doesn't exist (for existing databases)
    sqlite3_exec(g_db, "ALTER TABLE session ADD COLUMN isread INTEGER DEFAULT 0;", NULL, NULL, &err_msg);
    if (err_msg) sqlite3_free(err_msg);

    // Create index on session_id for faster queries
    const char* sql_session_index = "CREATE INDEX IF NOT EXISTS idx_session_id ON session(session_id);";
    sqlite3_exec(g_db, sql_session_index, NULL, NULL, &err_msg);
    // Ignore error if index already exists

    // Clean up duplicate session records before adding unique index
    sqlite3_exec(g_db, "DELETE FROM session WHERE id NOT IN (SELECT MIN(id) FROM session GROUP BY email_id);", NULL, NULL, &err_msg);
    if (err_msg) sqlite3_free(err_msg);

    // Create unique index on email_id to prevent duplicate session entries
    sqlite3_exec(g_db, "CREATE UNIQUE INDEX IF NOT EXISTS idx_session_email_id ON session(email_id);", NULL, NULL, &err_msg);
    if (err_msg) {
        LOG_INFO("SQL warning (session unique index): %s\n", err_msg);
        sqlite3_free(err_msg);
    }

    // Add islocal column if it doesn't exist (for existing databases)
    const char* sql_add_islocal = "ALTER TABLE localemail ADD COLUMN islocal INTEGER DEFAULT 0;";
    sqlite3_exec(g_db, sql_add_islocal, NULL, NULL, &err_msg);
    // Ignore error if column already exists

    // Add retry_count column if it doesn't exist (for existing databases)
    const char* sql_add_retry_count = "ALTER TABLE localemail ADD COLUMN retry_count INTEGER DEFAULT 0;";
    sqlite3_exec(g_db, sql_add_retry_count, NULL, NULL, &err_msg);
    // Ignore error if column already exists

    // Add servicerecvtime column if it doesn't exist (for existing databases)
    const char* sql_add_servicerecvtime = "ALTER TABLE localemail ADD COLUMN servicerecvtime TEXT;";
    sqlite3_exec(g_db, sql_add_servicerecvtime, NULL, NULL, &err_msg);
    // Ignore error if column already exists

    return 0;
}

void email_db_close(void) {
    if (g_db != NULL) {
        sqlite3_close(g_db);
        g_db = NULL;
    }
}

// Expose the global db handle for use by email_handler_c.cpp
extern "C" sqlite3* email_core_get_db() {
    return g_db;
}

// Expose the global db mutex for use by email_handler_c.cpp to prevent concurrent SQLite access
extern "C" std::mutex& email_core_get_db_mutex() {
    return g_db_mutex;
}

// ---------------------------------------------------------------------------
// IMAP Connection Implementation using libemail
// ---------------------------------------------------------------------------

struct IMAPConnection {
    int config_index;
    char* email;
    int connected;
};

static int libemail_initialized = 0;

IMAPConnection* imap_create(const char* server, int port, const char* email, const char* auth_code) {
    LOG_INFO("[IMAP] Creating connection to %s:%d for %s using libemail\n", server, port, email);

    // Initialize libemail system if not already done
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

    // Create new email object - this function is deprecated, use email_id-based approach instead
    // For now, return NULL as this legacy function is not compatible with email_id system
    LOG_INFO("[IMAP] imap_create is deprecated, use email_id-based OpenNewEmail_c instead\n");
    return NULL;
}

void imap_destroy(IMAPConnection* conn) {
    if (!conn) {
        return;
    }

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

    // Perform authority (for 163, this just checks auth code and connects)
    int auth_result = Authority_c(conn->config_index);
    if (auth_result != 0) {
        LOG_INFO("[IMAP] Authority failed: %d\n", auth_result);
        return -1;
    }

    LOG_INFO("[IMAP] Authority successful, connection established\n");
    conn->connected = 1;

    // Get email list from INBOX
    char json_buffer[8192];
    int list_result = Email_List_c(conn->config_index, "INBOX", json_buffer, sizeof(json_buffer));
    if (list_result != 0) {
        LOG_INFO("[IMAP] Email list failed: %d\n", list_result);
        return -1;
    }

    LOG_INFO("[IMAP] Email list result: %s\n", json_buffer);

    // Parse JSON UIDs and fetch each email's content
    try {
        json email_list = json::parse(json_buffer);
        if (email_list.contains("uids") && email_list["uids"].is_array()) {
            auto uids = email_list["uids"];
            LOG_INFO("[IMAP] Found %d emails, fetching content...\n", (int)uids.size());

            for (const auto& uid_val : uids) {
                std::string uid = uid_val.get<std::string>();

                // Fetch email content by UID
                char email_buffer[65536];
                int get_result = GetEmail_c(conn->config_index, "INBOX", uid.c_str(), email_buffer, sizeof(email_buffer));
                if (get_result != 0) {
                    LOG_INFO("[IMAP] Failed to fetch email uid=%s: %d\n", uid.c_str(), get_result);
                    continue;
                }

                // Parse email content JSON
                try {
                    json email_data = json::parse(email_buffer);
                    std::string content = email_data.value("content", "");

                    // Extract sender, subject from raw email content (basic parsing)
                    std::string sender = "unknown@example.com";
                    std::string subject = "No Subject";

                    // Simple header parsing from raw email
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

                    // Get timestamp
                    time_t now = time(NULL);
                    struct tm* tm_info = localtime(&now);
                    char timestamp[64];
                    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);

                    LOG_INFO("[IMAP] Fetched email: %s - %s\n", sender.c_str(), subject.c_str());
                    // Legacy email_db_insert removed - emails are now stored via localemail table
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

// ---------------------------------------------------------------------------
// High-level email connection API wrappers
// ---------------------------------------------------------------------------

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

int email_query_localemail(const char* account, char* outJson, int outSize) {
    if (!g_db) {
        if (outJson && outSize > 0) {
            snprintf(outJson, outSize, R"({"status":"failed","error":"database_not_initialized"})");
        }
        return -1;
    }

    LOG_INFO( "[DB] email_query_localemail called with account: '%s'\n", account ? account : "null");

    const char* sql = "SELECT l.uuid, l.account, l.sender, l.from_addr, l.subject, l.date, l.bodystructure, l.reply_to, l.in_reply_to, l.message_id, l.flags, l.folder, l.islocal, s.session_id, l.servicerecvtime, l.id, l.to_addr, l.file "
                      "FROM localemail l LEFT JOIN session s ON l.id = s.email_id WHERE l.account = ? ORDER BY l.id DESC;";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        LOG_INFO( "[DB] prepare failed: %s\n", sqlite3_errmsg(g_db));
        if (outJson && outSize > 0) {
            snprintf(outJson, outSize, R"({"status":"failed","error":"prepare_failed"})");
        }
        return -2;
    }

    sqlite3_bind_text(stmt, 1, account ? account : "", -1, SQLITE_STATIC);

    LOG_INFO("[DB] email_query_localemail: executing query for account='%s'\n", account ? account : "null");

    nlohmann::json emails_array = nlohmann::json::array();
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        nlohmann::json email_obj;
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
        email_obj["rowid"] = sqlite3_column_int64(stmt, 15);
        email_obj["to_addr"] = sqlite3_column_text(stmt, 16) ? (const char*)sqlite3_column_text(stmt, 16) : "";
        email_obj["file"] = sqlite3_column_text(stmt, 17) ? (const char*)sqlite3_column_text(stmt, 17) : "";
        emails_array.push_back(email_obj);
    }
    
    LOG_INFO( "[DB] email_query_localemail step result: %d, found %zu emails\n", rc, emails_array.size());
    if (rc != SQLITE_DONE) {
        LOG_INFO( "[DB] step error: %s\n", sqlite3_errmsg(g_db));
    }
    sqlite3_finalize(stmt);

    nlohmann::json response;
    response["status"] = "success";
    response["count"] = emails_array.size();
    response["emails"] = emails_array;
    response["debug_account"] = account ? account : "null";
    response["debug_account_len"] = account ? strlen(account) : 0;

    std::string jsonStr = response.dump();
    if (outJson && outSize > 0) {
        snprintf(outJson, outSize, "%s", jsonStr.c_str());
    }
    return 0;
}

// Query thread root emails (first email of each conversation thread)
int email_query_thread_roots(const char* account, char* outJson, int outSize) {
    if (!g_db) {
        if (outJson && outSize > 0) {
            snprintf(outJson, outSize, R"({"status":"failed","error":"database_not_initialized"})");
        }
        return -1;
    }

    LOG_INFO( "[DB] email_query_thread_roots called with account: '%s'\n", account ? account : "null");

    // Query the first email of each session (conversation root)
    const char* sql =
        "SELECT l.uuid, l.account, l.sender, l.from_addr, l.subject, l.date, l.bodystructure, l.reply_to, l.in_reply_to, l.message_id, l.flags, l.folder, l.islocal, s.session_id, l.servicerecvtime, l.to_addr, l.id, l.file "
        "FROM localemail l "
        "INNER JOIN session s ON l.id = s.email_id "
        "WHERE l.account = ? AND s.visible = 1 "
        "AND l.id = (SELECT MIN(email_id) FROM session WHERE session_id = s.session_id AND email_id > 0) "
        "ORDER BY l.id DESC;";

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        LOG_INFO( "[DB] prepare failed: %s\n", sqlite3_errmsg(g_db));
        if (outJson && outSize > 0) {
            snprintf(outJson, outSize, R"({"status":"failed","error":"prepare_failed"})");
        }
        return -2;
    }

    sqlite3_bind_text(stmt, 1, account ? account : "", -1, SQLITE_STATIC);

    nlohmann::json emails_array = nlohmann::json::array();
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        nlohmann::json email_obj;
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

    LOG_INFO( "[DB] email_query_thread_roots step result: %d, found %zu thread roots\n", rc, emails_array.size());
    if (rc != SQLITE_DONE) {
        LOG_INFO( "[DB] step error: %s\n", sqlite3_errmsg(g_db));
    }
    sqlite3_finalize(stmt);

    nlohmann::json response;
    response["status"] = "success";
    response["count"] = emails_array.size();
    response["emails"] = emails_array;

    std::string jsonStr = response.dump();
    if (outJson && outSize > 0) {
        snprintf(outJson, outSize, "%s", jsonStr.c_str());
    }
    return 0;
}

// Generate session records for existing emails
extern "C" int email_generate_sessions(const char* account, char* outJson, int outSize) {
    if (!g_db) {
        if (outJson && outSize > 0) {
            snprintf(outJson, outSize, R"({"status":"failed","error":"database_not_initialized"})");
        }
        return -1;
    }

    LOG_INFO( "[DB] email_generate_sessions called with account: '%s'\n", account ? account : "null");

    // Get all INBOX emails for this account that don't have session records
    const char* sql = 
        "SELECT id, uuid, message_id, in_reply_to FROM localemail WHERE account = ? AND folder = 'INBOX' ORDER BY id ASC;";

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        LOG_INFO( "[DB] prepare failed: %s\n", sqlite3_errmsg(g_db));
        if (outJson && outSize > 0) {
            snprintf(outJson, outSize, R"({"status":"failed","error":"prepare_failed"})");
        }
        return -2;
    }

    sqlite3_bind_text(stmt, 1, account ? account : "", -1, SQLITE_STATIC);

    int generated_count = 0;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        int64_t emailId = sqlite3_column_int64(stmt, 0);
        std::string emailIdStr = std::to_string(emailId);
        std::string uuid = (const char*)sqlite3_column_text(stmt, 1);
        std::string message_id = sqlite3_column_text(stmt, 2) ? (const char*)sqlite3_column_text(stmt, 2) : "";
        std::string in_reply_to = sqlite3_column_text(stmt, 3) ? (const char*)sqlite3_column_text(stmt, 3) : "";

        // Check if this email already has a session record (by email_id)
        const char* check_sql = "SELECT session_id FROM session WHERE email_id = ? LIMIT 1;";
        sqlite3_stmt* check_stmt;
        int check_rc = sqlite3_prepare_v2(g_db, check_sql, -1, &check_stmt, NULL);
        bool has_session = false;
        if (check_rc == SQLITE_OK) {
            sqlite3_bind_int64(check_stmt, 1, emailId);
            if (sqlite3_step(check_stmt) == SQLITE_ROW) {
                has_session = true;
            }
            sqlite3_finalize(check_stmt);
        }

        if (has_session) continue;

        // Generate session_id
        std::string session_id;
        if (!in_reply_to.empty() && in_reply_to != "<<> <>" && in_reply_to != "<>") {
            // Find the id of the referenced message (same account, INBOX only)
            const char* find_ref_sql = "SELECT id FROM localemail WHERE message_id = ? AND account = ? AND folder = 'INBOX' LIMIT 1;";
            sqlite3_stmt* find_ref_stmt;
            int find_ref_rc = sqlite3_prepare_v2(g_db, find_ref_sql, -1, &find_ref_stmt, NULL);
            int64_t referencedId = 0;
            bool found_ref = false;
            if (find_ref_rc == SQLITE_OK) {
                sqlite3_bind_text(find_ref_stmt, 1, in_reply_to.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(find_ref_stmt, 2, account ? account : "", -1, SQLITE_TRANSIENT);
                if (sqlite3_step(find_ref_stmt) == SQLITE_ROW) {
                    referencedId = sqlite3_column_int64(find_ref_stmt, 0);
                    found_ref = true;
                }
                sqlite3_finalize(find_ref_stmt);
            }

            if (found_ref) {
                // Check if the referenced email has a session_id
                const char* check_session_sql = "SELECT session_id FROM session WHERE email_id = ? LIMIT 1;";
                sqlite3_stmt* check_session_stmt;
                int check_session_rc = sqlite3_prepare_v2(g_db, check_session_sql, -1, &check_session_stmt, NULL);
                if (check_session_rc == SQLITE_OK) {
                    sqlite3_bind_int64(check_session_stmt, 1, referencedId);
                    if (sqlite3_step(check_session_stmt) == SQLITE_ROW) {
                        session_id = reinterpret_cast<const char*>(sqlite3_column_text(check_session_stmt, 0));
                    }
                    sqlite3_finalize(check_session_stmt);
                }

                // If the referenced email has no session, use its id as session name
                if (session_id.empty()) {
                    session_id = "session_" + std::to_string(referencedId);
                }
            } else {
                session_id = "session_" + emailIdStr;
            }
        } else {
            session_id = "session_" + emailIdStr;
        }

        // Delete any existing session record for this email_id, then insert
        const char* delete_sql = "DELETE FROM session WHERE email_id = ?;";
        sqlite3_stmt* delete_stmt;
        if (sqlite3_prepare_v2(g_db, delete_sql, -1, &delete_stmt, NULL) == SQLITE_OK) {
            sqlite3_bind_int64(delete_stmt, 1, emailId);
            sqlite3_step(delete_stmt);
            sqlite3_finalize(delete_stmt);
        }
        const char* insert_sql = "INSERT OR REPLACE INTO session (session_id, email_id, visible, auto) VALUES (?, ?, 1, 1);";
        sqlite3_stmt* insert_stmt;
        int insert_rc = sqlite3_prepare_v2(g_db, insert_sql, -1, &insert_stmt, NULL);
        if (insert_rc == SQLITE_OK) {
            sqlite3_bind_text(insert_stmt, 1, session_id.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(insert_stmt, 2, emailId);
            sqlite3_step(insert_stmt);
            sqlite3_finalize(insert_stmt);
            generated_count++;
        }
    }

    sqlite3_finalize(stmt);

    // Second pass: merge sessions by following in_reply_to chains
    // For each email with in_reply_to, if the referenced email is in a different session, merge
    const char* merge_sql = 
        "SELECT l.id, l.in_reply_to, s.session_id FROM localemail l "
        "JOIN session s ON l.id = s.email_id "
        "WHERE l.account = ? AND l.folder = 'INBOX' AND l.in_reply_to != '' AND l.in_reply_to != '<<> <>' AND l.in_reply_to != '<>' "
        "ORDER BY l.id ASC;";
    sqlite3_stmt* merge_stmt;
    rc = sqlite3_prepare_v2(g_db, merge_sql, -1, &merge_stmt, NULL);
    if (rc == SQLITE_OK) {
        sqlite3_bind_text(merge_stmt, 1, account ? account : "", -1, SQLITE_STATIC);
        while (sqlite3_step(merge_stmt) == SQLITE_ROW) {
            std::string rowidStr = std::to_string(sqlite3_column_int64(merge_stmt, 0));
            std::string in_reply_to = (const char*)sqlite3_column_text(merge_stmt, 1);
            std::string my_session = (const char*)sqlite3_column_text(merge_stmt, 2);

            // Find the referenced email's session (same account, INBOX only)
            const char* ref_sql = 
                "SELECT s2.session_id FROM localemail l2 "
                "JOIN session s2 ON l2.id = s2.email_id "
                "WHERE l2.message_id = ? AND l2.account = ? AND l2.folder = 'INBOX' LIMIT 1;";
            sqlite3_stmt* ref_stmt;
            std::string ref_session;
            if (sqlite3_prepare_v2(g_db, ref_sql, -1, &ref_stmt, NULL) == SQLITE_OK) {
                sqlite3_bind_text(ref_stmt, 1, in_reply_to.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(ref_stmt, 2, account ? account : "", -1, SQLITE_TRANSIENT);
                if (sqlite3_step(ref_stmt) == SQLITE_ROW) {
                    ref_session = (const char*)sqlite3_column_text(ref_stmt, 0);
                }
                sqlite3_finalize(ref_stmt);
            }
            // Do not search across accounts — sessions are per-account only
            if (ref_session.empty()) {
                // No cross-account search to avoid session contamination
            }
            if (!ref_session.empty() && ref_session != my_session) {
                        // Merge: update all emails in my_session to ref_session
                        const char* update_sql = "UPDATE session SET session_id = ? WHERE session_id = ?;";
                        sqlite3_stmt* update_stmt;
                        if (sqlite3_prepare_v2(g_db, update_sql, -1, &update_stmt, NULL) == SQLITE_OK) {
                            sqlite3_bind_text(update_stmt, 1, ref_session.c_str(), -1, SQLITE_TRANSIENT);
                            sqlite3_bind_text(update_stmt, 2, my_session.c_str(), -1, SQLITE_TRANSIENT);
                            sqlite3_step(update_stmt);
                            sqlite3_finalize(update_stmt);
                        }
                    }
        }
        sqlite3_finalize(merge_stmt);
    }

    // Delete auto-inserted sessions with only one email (keep user-created sessions with auto=0)
    // But preserve sessions for emails that have in_reply_to set — those are replies waiting for their parent
    const char* del_single_sql = 
        "DELETE FROM session WHERE auto = 1 AND session_id IN "
        "(SELECT s.session_id FROM session s GROUP BY s.session_id HAVING COUNT(*) = 1) "
        "AND email_id NOT IN ("
        "  SELECT l.id FROM localemail l "
        "  WHERE l.in_reply_to IS NOT NULL AND l.in_reply_to != '' "
        "  AND l.in_reply_to != '<<> <>' AND l.in_reply_to != '<>'"
        ");";
    sqlite3_exec(g_db, del_single_sql, NULL, NULL, NULL);

    nlohmann::json response;
    response["status"] = "success";
    response["count"] = generated_count;

    std::string jsonStr = response.dump();
    if (outJson && outSize > 0) {
        snprintf(outJson, outSize, "%s", jsonStr.c_str());
    }
    return 0;
}

// Create a user-defined session (auto=0) with message_id as index_uuid
extern "C" int email_create_session(const char* account, const char* subject, const char* members, const char* message_id, char* outJson, int outSize) {
    if (!g_db) {
        if (outJson && outSize > 0) {
            snprintf(outJson, outSize, R"({"status":"failed","error":"database_not_initialized"})");
        }
        return -1;
    }

    LOG_INFO("[DB] email_create_session called with account: '%s', subject: '%s', members: '%s', message_id: '%s'\n", 
             account ? account : "null", subject ? subject : "null", members ? members : "null", message_id ? message_id : "null");

    // Insert a placeholder row to get an auto-increment id, then use it as session_id
    const char* placeholder_sql = "INSERT INTO session (session_id, email_id, visible, auto) VALUES ('', 0, 1, 0);";
    sqlite3_exec(g_db, placeholder_sql, NULL, NULL, NULL);
    int64_t newId = sqlite3_last_insert_rowid(g_db);
    std::string session_id = "session_" + std::to_string(newId);

    // Update the placeholder row with the real session_id
    const char* update_sid_sql = "UPDATE session SET session_id = ? WHERE id = ?;";
    sqlite3_stmt* update_sid_stmt;
    if (sqlite3_prepare_v2(g_db, update_sid_sql, -1, &update_sid_stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(update_sid_stmt, 1, session_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(update_sid_stmt, 2, newId);
        sqlite3_step(update_sid_stmt);
        sqlite3_finalize(update_sid_stmt);
    }

    // If message_id is a numeric string (id from insertSentEmail), use it directly as email_id
    // Otherwise, look up localemail id by message_id
    int64_t emailId = 0;
    if (message_id && *message_id) {
        // Check if it's a pure number (id)
        bool isNumeric = true;
        for (const char* p = message_id; *p; ++p) {
            if (!isdigit(*p)) { isNumeric = false; break; }
        }
        if (isNumeric) {
            emailId = std::stoll(std::string(message_id));
        } else {
            const char* find_id_sql = "SELECT id FROM localemail WHERE message_id = ? AND account = ? ORDER BY id DESC LIMIT 1;";
            sqlite3_stmt* find_stmt;
            if (sqlite3_prepare_v2(g_db, find_id_sql, -1, &find_stmt, NULL) == SQLITE_OK) {
                sqlite3_bind_text(find_stmt, 1, message_id, -1, SQLITE_STATIC);
                sqlite3_bind_text(find_stmt, 2, account ? account : "", -1, SQLITE_STATIC);
                if (sqlite3_step(find_stmt) == SQLITE_ROW) {
                    emailId = sqlite3_column_int64(find_stmt, 0);
                }
                sqlite3_finalize(find_stmt);
            }
        }
    }
    LOG_INFO("[DB] email_create_session: email_id='%lld' from message_id='%s'\n", 
             (long long)emailId, message_id ? message_id : "null");

    // Update the placeholder row with the real email_id if we have one
    if (emailId > 0) {
        const char* update_sql = "UPDATE session SET email_id = ? WHERE id = ?;";
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(g_db, update_sql, -1, &stmt, NULL) == SQLITE_OK) {
            sqlite3_bind_int64(stmt, 1, emailId);
            sqlite3_bind_int64(stmt, 2, newId);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    } else {
        LOG_INFO("[DB] email_create_session: emailId=0, will be updated by addEmailToSession\n");
    }

    nlohmann::json response;
    response["status"] = "success";
    response["session_id"] = session_id;

    std::string jsonStr = response.dump();
    if (outJson && outSize > 0) {
        snprintf(outJson, outSize, "%s", jsonStr.c_str());
    }
    return 0;
}

// Insert a sent email record into localemail with a pending uuid
// When IMAP syncs Sent folder, the real uuid will update this record (matched by message_id)
extern "C" int email_insert_sent_email(const char* account, const char* sender, const char* from_addr, const char* to_addr, const char* subject, const char* date, const char* message_id, const char* in_reply_to, const char* body, const char* storageDir, char* outJson, int outSize) {
    if (!g_db) {
        if (outJson && outSize > 0) {
            snprintf(outJson, outSize, R"({"status":"failed","error":"database_not_initialized"})");
        }
        return -1;
    }

    LOG_INFO("[DB] email_insert_sent_email: account='%s', message_id='%s', subject='%s'\n",
             account ? account : "null", message_id ? message_id : "null", subject ? subject : "null");

    // Check if a record with this message_id already exists
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
        // Already exists, return existing rowid
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
            nlohmann::json response;
            response["status"] = "success";
            response["uuid"] = rowidStr;
            response["exists"] = true;
            std::string jsonStr = response.dump();
            snprintf(outJson, outSize, "%s", jsonStr.c_str());
        }
        return 0;
    }

    // Use "0" as the pending uuid — when IMAP sync fetches the real email,
    // it will match by message_id and update uuid to the real IMAP UID.
    // Using "0" ensures correct sort order by rowid (insertion order = chronological).
    std::string pending_uuid = "0";

    // Construct a simple bodystructure JSON representing plain text
    nlohmann::json bs;
    bs["type"] = "text";
    bs["subtype"] = "plain";
    bs["size"] = body ? strlen(body) : 0;
    bs["encoding"] = "7bit";
    std::string bodystructureStr = bs.dump();

    // Extract filename from message_id (remove < and >) for file field
    std::string filename = message_id ? message_id : "";
    size_t start = filename.find('<');
    size_t end = filename.find('>');
    if (start != std::string::npos && end != std::string::npos && end > start) {
        filename = filename.substr(start + 1, end - start - 1);
    }

    // Insert into localemail, setting islocal=0 so download thread will fetch body
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
        sqlite3_bind_text(insert_stmt, 9, in_reply_to ? in_reply_to : "", -1, SQLITE_STATIC);  // reply_to field (reuse in_reply_to)
        sqlite3_bind_text(insert_stmt, 10, in_reply_to ? in_reply_to : "", -1, SQLITE_STATIC); // in_reply_to field
        sqlite3_bind_text(insert_stmt, 11, message_id ? message_id : "", -1, SQLITE_STATIC);   // message_id field
        // Positions 12-14 are hardcoded in SQL: '[]', 'INBOX', 0
        sqlite3_bind_text(insert_stmt, 12, date ? date : "", -1, SQLITE_STATIC);                // servicerecvtime
        sqlite3_bind_text(insert_stmt, 13, filename.c_str(), -1, SQLITE_STATIC);                // file
        sqlite3_step(insert_stmt);
        sqlite3_finalize(insert_stmt);
        int64_t rowid = sqlite3_last_insert_rowid(g_db);
        std::string rowidStr = std::to_string(rowid);
        LOG_INFO("[DB] email_insert_sent_email: inserted with pending uuid='%s', id='%s', file='%s'\n", pending_uuid.c_str(), rowidStr.c_str(), filename.c_str());

        // Save email to {storageDir}/{account}/{message-id}.eml
        if (storageDir && account && message_id && body) {
            std::string accountStr(account);
            std::string messageIdStr(message_id);
            std::string storageDirStr(storageDir);

            // Create directory: <storageDir>/<account>/
            std::string accountDir = storageDirStr + "/" + accountStr;
            std::filesystem::create_directories(accountDir);

            // Extract filename from message_id (remove < and >)
            std::string filename = messageIdStr;
            size_t start = filename.find('<');
            size_t end = filename.find('>');
            if (start != std::string::npos && end != std::string::npos && end > start) {
                filename = filename.substr(start + 1, end - start - 1);
            }

            // Save to file: <accountDir>/<message-id>.eml
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
            } else {
                LOG_INFO("[DB] email_insert_sent_email: failed to open file %s\n", filePath.c_str());
            }
        }

        if (outJson && outSize > 0) {
            nlohmann::json response;
            response["status"] = "success";
            response["uuid"] = rowidStr;
            std::string jsonStr = response.dump();
            snprintf(outJson, outSize, "%s", jsonStr.c_str());
        }
        return 0;
    }

    return 0;
}

// Query session_id by message_id (looks up localemail.message_id -> session.session_id)
extern "C" int email_query_session_by_message_id(const char* messageId, char* outSessionId, int outSize) {
    if (!g_db || !messageId || !*messageId) {
        if (outSessionId && outSize > 0) outSessionId[0] = '\0';
        return -1;
    }

    const char* sql =
        "SELECT s.session_id FROM session s "
        "INNER JOIN localemail l ON l.id = s.email_id "
        "WHERE l.message_id = ? LIMIT 1;";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        if (outSessionId && outSize > 0) outSessionId[0] = '\0';
        return -2;
    }
    sqlite3_bind_text(stmt, 1, messageId, -1, SQLITE_STATIC);

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

// Add an email to an existing session by uuid and session_id
extern "C" int email_add_email_to_session(const char* sessionId, const char* uuid, const char* account, char* outJson, int outSize) {
    if (!g_db) {
        if (outJson && outSize > 0) {
            snprintf(outJson, outSize, R"({"status":"failed","error":"database_not_initialized"})");
        }
        return -1;
    }

    LOG_INFO("[DB] email_add_email_to_session: session_id='%s', uuid='%s', account='%s'\n",
             sessionId ? sessionId : "null", uuid ? uuid : "null", account ? account : "null");

    if (!uuid || !*uuid) {
        if (outJson && outSize > 0) {
            snprintf(outJson, outSize, R"({"status":"failed","error":"empty_uuid"})");
        }
        return -2;
    }

    // uuid parameter is actually the email id (numeric string from insertSentEmail)
    int64_t emailId = 0;
    if (uuid && *uuid) {
        emailId = std::stoll(std::string(uuid));
    }

    // Check if email_id already exists in session table
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
            LOG_INFO("[DB] email_add_email_to_session: email_id='%lld' already exists with id=%d, auto=%d\n",
                     (long long)emailId, existingId, existingAuto);
        }
        sqlite3_finalize(check_stmt);
    }

    if (exists) {
        // Update session_id and auto=1 if it exists
        const char* update_sql = "UPDATE session SET session_id = ?, auto = 1 WHERE email_id = ?;";
        sqlite3_stmt* update_stmt;
        if (sqlite3_prepare_v2(g_db, update_sql, -1, &update_stmt, NULL) == SQLITE_OK) {
            sqlite3_bind_text(update_stmt, 1, sessionId ? sessionId : "", -1, SQLITE_STATIC);
            sqlite3_bind_int64(update_stmt, 2, emailId);
            sqlite3_step(update_stmt);
            sqlite3_finalize(update_stmt);
            LOG_INFO("[DB] email_add_email_to_session: updated session_id='%s', auto=1 for email_id='%lld'\n",
                     sessionId ? sessionId : "", (long long)emailId);
        }
    } else {
        // Insert new session record with auto=0 (manually sent)
        const char* insert_sql = "INSERT INTO session (session_id, email_id, visible, auto, isread) VALUES (?, ?, 1, 0, 0);";
        sqlite3_stmt* insert_stmt;
        if (sqlite3_prepare_v2(g_db, insert_sql, -1, &insert_stmt, NULL) == SQLITE_OK) {
            sqlite3_bind_text(insert_stmt, 1, sessionId ? sessionId : "", -1, SQLITE_STATIC);
            sqlite3_bind_int64(insert_stmt, 2, emailId);
            sqlite3_step(insert_stmt);
            sqlite3_finalize(insert_stmt);
            LOG_INFO("[DB] email_add_email_to_session: inserted session_id='%s', email_id='%lld', auto=0\n",
                     sessionId ? sessionId : "", (long long)emailId);
        }
    }

    if (outJson && outSize > 0) {
        nlohmann::json response;
        response["status"] = "success";
        response["uuid"] = uuid;
        std::string jsonStr = response.dump();
        snprintf(outJson, outSize, "%s", jsonStr.c_str());
    }
    return 0;
}

// Query session index_uuid by session_id
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

    nlohmann::json response;
    response["status"] = "success";
    response["index_uuid"] = indexUuid;

    std::string jsonStr = response.dump();
    if (outJson && outSize > 0) {
        snprintf(outJson, outSize, "%s", jsonStr.c_str());
    }
    return 0;
}

// Query conversation thread by session_id
extern "C" int email_query_thread(const char* sessionId, char* outJson, int outSize) {
    if (!g_db) {
        if (outJson && outSize > 0) {
            snprintf(outJson, outSize, R"({"status":"failed","error":"database_not_initialized"})");
        }
        return -1;
    }

    LOG_INFO( "[DB] email_query_thread called with session_id: '%s'\n", sessionId ? sessionId : "null");

    // Query all emails in the session (email_id references localemail.id)
    const char* sql =
        "SELECT l.uuid, l.account, l.sender, l.from_addr, l.subject, l.date, l.bodystructure, l.reply_to, l.in_reply_to, l.message_id, l.flags, l.folder, l.islocal, s.session_id, l.servicerecvtime, l.to_addr, l.id, l.file "
        "FROM localemail l "
        "INNER JOIN session s ON l.id = s.email_id "
        "WHERE s.session_id = ? AND s.visible = 1 "
        "ORDER BY l.id ASC;";

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        LOG_INFO( "[DB] prepare failed: %s\n", sqlite3_errmsg(g_db));
        if (outJson && outSize > 0) {
            snprintf(outJson, outSize, R"({"status":"failed","error":"prepare_failed"})");
        }
        return -2;
    }

    sqlite3_bind_text(stmt, 1, sessionId ? sessionId : "", -1, SQLITE_STATIC);

    nlohmann::json emails_array = nlohmann::json::array();
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        nlohmann::json email_obj;
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

    LOG_INFO( "[DB] email_query_thread step result: %d, found %zu emails in thread\n", rc, emails_array.size());
    if (rc != SQLITE_DONE) {
        LOG_INFO( "[DB] step error: %s\n", sqlite3_errmsg(g_db));
    }
    sqlite3_finalize(stmt);

    nlohmann::json response;
    response["status"] = "success";
    response["count"] = emails_array.size();
    response["emails"] = emails_array;

    std::string jsonStr = response.dump();
    if (outJson && outSize > 0) {
        snprintf(outJson, outSize, "%s", jsonStr.c_str());
    }
    return 0;
}

// ---------------------------------------------------------------------------
// oemailim C wrappers (for Dart FFI)
// ---------------------------------------------------------------------------

int oemailim_system_open(const char* dataDir, const char* configDir, const char* logDir) {
    return systemOpen_c(dataDir, configDir, logDir);
}

void oemailim_set_callback(void* callback) {
    // Not needed - browser launch uses system command directly
}

extern "C" int oemailim_open_new_email(const char* email_id) {
    return OpenNewEmail_c(email_id);
}

int oemailim_go(int configIndex) {
    return Go_c(configIndex);
}

extern "C" int oemailim_authority(int configIndex) {
    LOG_INFO("oemailim_authority called with configIndex: %d", configIndex);
    int result = Authority_c(configIndex);
    LOG_INFO("oemailim_authority result: %d", result);
    return result;
}

extern "C" int oemailim_add_outlook_email() {
    LOG_INFO("oemailim_add_outlook_email called");
    return AddOutlookEmail_c();
}

extern "C" int oemailim_set_imap_server(int configIndex, const char* server, int port) {
    return SetImapServer_c(configIndex, server, port);
}

extern "C" int oemailim_set_smtp_server(int configIndex, const char* server, int port) {
    return SetSmtpServer_c(configIndex, server, port);
}

extern "C" int oemailim_set_refresh_token(int configIndex, const char* token) {
    return SetRefreshToken_c(configIndex, token);
}

extern "C" int oemailim_refresh_token(int configIndex) {
    return RefreshToken_c(configIndex);
}

extern "C" int oemailim_get_email(int configIndex, char* outEmail, int outSize) {
    return GetEmailAddress_c(configIndex, outEmail, outSize);
}

extern "C" int oemailim_get_refresh_token(int configIndex, char* outToken, int outSize) {
    return GetRefreshToken_c(configIndex, outToken, outSize);
}

extern "C" void oemailim_system_close(int configIndex) {
    systemClose_c(configIndex);
}

extern "C" int oemailim_email_list(int configIndex, const char* path, char* outJson, int outSize) {
    return Email_List_c(configIndex, path, outJson, outSize);
}

extern "C" int oemailim_email_select(int configIndex, const char* path, char* outJson, int outSize) {
    return Email_Select_c(configIndex, path, outJson, outSize);
}

extern "C" int email_idle_wait(int configIndex, const char* folder, int timeoutSeconds) {
    return IdleWait_c(configIndex, folder, timeoutSeconds);
}

extern "C" int email_find_sent_folder(int configIndex, char* outFolder, int outSize) {
    return FindSentFolder_c(configIndex, outFolder, outSize);
}

extern "C" int email_send_via_config(int configIndex, const char* content) {
    return SendEmail_c(configIndex, content);
}

extern "C" int email_count_pending_bodies(const char* account) {
    if (!g_db || !account) {
        LOG_INFO("[DB] count_pending_bodies: db or account is null\n");
        return -1;
    }

    LOG_INFO("[DB] count_pending_bodies: checking for account=%s\n", account);

    const char* sql = "SELECT COUNT(*) FROM localemail WHERE account = ? AND islocal = 0 AND uuid != '0' AND (retry_count IS NULL OR retry_count < 3);";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        LOG_INFO("[DB] count_pending_bodies: prepare failed: %s\n", sqlite3_errmsg(g_db));
        return -2;
    }

    sqlite3_bind_text(stmt, 1, account, -1, SQLITE_TRANSIENT);

    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    
    LOG_INFO("[DB] count_pending_bodies: found %d pending emails for account=%s\n", count, account);
    return count;
}

extern "C" int email_get_last_error(int configIndex, char* outBuf, int outSize) {
    return GetLastError_c(configIndex, outBuf, outSize);
}

// Download pending email bodies (islocal=0) for a given account.
// For each email with islocal=0, fetches the full body via IMAP using configIndex,
// saves it to <storageDir>/<account>/<uuid>.eml, and updates islocal=1.
// Returns the number of emails downloaded, or negative on error.
extern "C" int email_download_pending_bodies(int configIndex, const char* account,
                                              const char* storageDir, char* outJson, int outSize) {
    if (!g_db) {
        if (outJson && outSize > 0) {
            snprintf(outJson, outSize, R"({"status":"failed","error":"database_not_initialized"})");
        }
        return -1;
    }

    if (!account || !storageDir) {
        if (outJson && outSize > 0) {
            snprintf(outJson, outSize, R"({"status":"failed","error":"null_parameter"})");
        }
        return -2;
    }

    std::string accountStr(account);
    std::string storageDirStr(storageDir);

    // Query emails with islocal=0 and retry_count < 3 for this account
    // Simplified query to avoid CAST which may cause memory allocation issues
    const char* sql = "SELECT uuid, folder FROM localemail WHERE account = ? AND islocal = 0 AND uuid != '0' AND (retry_count IS NULL OR retry_count < 3) ORDER BY uuid ASC LIMIT 10;";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        LOG_INFO("[DB] download_pending: prepare failed: %s\n", sqlite3_errmsg(g_db));
        if (outJson && outSize > 0) {
            snprintf(outJson, outSize, R"({"status":"failed","error":"prepare_failed"})");
        }
        return -3;
    }

    sqlite3_bind_text(stmt, 1, accountStr.c_str(), -1, SQLITE_STATIC);

    // Collect pending emails
    struct PendingEmail {
        std::string uuid;
        std::string folder;
    };
    std::vector<PendingEmail> pending;

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        PendingEmail pe;
        pe.uuid = (const char*)sqlite3_column_text(stmt, 0);
        pe.folder = sqlite3_column_text(stmt, 1) ? (const char*)sqlite3_column_text(stmt, 1) : "INBOX";
        pending.push_back(pe);
    }
    sqlite3_finalize(stmt);

    if (pending.empty()) {
        if (outJson && outSize > 0) {
            snprintf(outJson, outSize, R"({"status":"success","downloaded":0})");
        }
        return 0;
    }

    LOG_INFO("[DB] download_pending: found %zu emails to download for %s\n", pending.size(), accountStr.c_str());

    // Create storage directory: <storageDir>/<account>/
    std::string accountDir = storageDirStr + "/" + accountStr;
    std::filesystem::create_directories(accountDir);

    int downloaded = 0;
    nlohmann::json results = nlohmann::json::array();

    for (const auto& pe : pending) {
        // Save to file: <accountDir>/<uuid>.eml
        std::string filePath = accountDir + "/" + pe.uuid + ".eml";

        // Fetch email content and write directly to file (avoids buffer size issues)
        // Error codes: -10 = network error (don't increment retry_count), other = non-network error
        int getResult = GetEmailToFile_c(configIndex, pe.folder.c_str(), pe.uuid.c_str(), filePath.c_str());
        if (getResult != 0) {
            LOG_INFO("[DB] download_pending: failed to fetch uid=%s folder=%s: %d\n", pe.uuid.c_str(), pe.folder.c_str(), getResult);
            if (getResult != -10) {
                // Non-network error: increment retry_count
                const char* retrySql = "UPDATE localemail SET retry_count = COALESCE(retry_count, 0) + 1 WHERE uuid = ? AND account = ?;";
                sqlite3_stmt* retryStmt;
                rc = sqlite3_prepare_v2(g_db, retrySql, -1, &retryStmt, NULL);
                if (rc == SQLITE_OK) {
                    sqlite3_bind_text(retryStmt, 1, pe.uuid.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(retryStmt, 2, accountStr.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_step(retryStmt);
                    sqlite3_finalize(retryStmt);
                }
                LOG_INFO("[DB] download_pending: incremented retry_count for uid=%s\n", pe.uuid.c_str());
            } else {
                LOG_INFO("[DB] download_pending: network error for uid=%s, will retry next cycle\n", pe.uuid.c_str());
            }
            continue;
        }

        // Parse .eml file to extract message_id and in_reply_to using vmime
        std::string message_id = "";
        std::string in_reply_to = "";
        try {
            // Read entire .eml file
            std::ifstream emlFile(filePath);
            std::string emlContent((std::istreambuf_iterator<char>(emlFile)),
                                   std::istreambuf_iterator<char>());
            emlFile.close();
            
            // Parse using vmime
            vmime::parsingContext parseCtx;
            size_t pos = 0;
            const size_t end = emlContent.size();
            
            auto caseInsensitiveLess = [](const std::string& a, const std::string& b) {
                return std::lexicographical_compare(
                    a.begin(), a.end(), b.begin(), b.end(),
                    [](char c1, char c2) { return ::tolower(c1) < ::tolower(c2); });
            };
            std::map<std::string, std::string, decltype(caseInsensitiveLess)> headerMap(caseInsensitiveLess);
            
            while (pos < end) {
                auto field = vmime::headerField::parseNext(parseCtx, emlContent, pos, end, &pos);
                if (!field) break;
                std::string fName = field->getName();
                std::string fValue;
                auto val = field->getValue();
                if (val) {
                    std::string generated;
                    vmime::utility::outputStreamStringAdapter os(generated);
                    val->generate(vmime::generationContext::getDefaultContext(), os, 0);
                    os.flush();
                    fValue = generated;
                }
                headerMap[fName] = fValue;
            }
            
            // Extract message_id and in_reply_to
            auto decodeHeader = [](const std::string& raw) -> std::string {
                if (raw.empty()) return raw;
                try {
                    auto decoded = vmime::text::decodeAndUnfold(raw);
                    if (decoded) {
                        return decoded->getConvertedText(vmime::charset("utf-8"));
                    }
                    return raw;
                } catch (...) {
                    return raw;
                }
            };
            
            auto getHeader = [&](const std::string& name) -> std::string {
                auto it = headerMap.find(name);
                if (it != headerMap.end()) {
                    return it->second;
                }
                return "";
            };
            
            message_id = decodeHeader(getHeader("Message-ID"));
            in_reply_to = decodeHeader(getHeader("In-Reply-To"));
            
            LOG_INFO("[DB] download_pending: parsed message_id='%s', in_reply_to='%s' from %s\n", 
                     message_id.c_str(), in_reply_to.c_str(), filePath.c_str());
        } catch (const std::exception& e) {
            LOG_INFO("[DB] download_pending: failed to parse .eml file: %s\n", e.what());
        }

        // Update islocal=1, message_id, in_reply_to, and file in database
        const char* updateSql = "UPDATE localemail SET islocal = 1, message_id = ?, in_reply_to = ?, file = ? WHERE uuid = ? AND account = ?;";
        sqlite3_stmt* updateStmt;
        rc = sqlite3_prepare_v2(g_db, updateSql, -1, &updateStmt, NULL);
        if (rc == SQLITE_OK) {
            sqlite3_bind_text(updateStmt, 1, message_id.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(updateStmt, 2, in_reply_to.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(updateStmt, 3, pe.uuid.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(updateStmt, 4, pe.uuid.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(updateStmt, 5, accountStr.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(updateStmt);
            sqlite3_finalize(updateStmt);
            LOG_INFO("[DB] download_pending: updated uid=%s with message_id='%s', in_reply_to='%s', file='%s'\n",
                     pe.uuid.c_str(), message_id.c_str(), in_reply_to.c_str(), pe.uuid.c_str());
        }

        downloaded++;
        results.push_back({{"uuid", pe.uuid}, {"folder", pe.folder}, {"file", filePath}});
        LOG_INFO("[DB] download_pending: saved uid=%s to %s\n", pe.uuid.c_str(), filePath.c_str());
    }

    nlohmann::json response;
    response["status"] = "success";
    response["downloaded"] = downloaded;
    response["results"] = results;

    std::string jsonStr = response.dump();
    if (outJson && outSize > 0) {
        snprintf(outJson, outSize, "%s", jsonStr.c_str());
    }

    return downloaded;
}

// Helper: recursively extract text/html body and attachments from vmime message parts
static void extractParts(const vmime::shared_ptr<vmime::bodyPart>& part,
                         std::string& textBody, std::string& htmlBody,
                         nlohmann::json& attachments, bool& hasAttachment) {
    auto body = part->getBody();
    vmime::mediaType ct = body->getContentType();

    if (ct.getType() == vmime::mediaTypes::MULTIPART) {
        auto parts = body->getPartList();
        for (size_t i = 0; i < parts.size(); i++) {
            extractParts(parts[i], textBody, htmlBody, attachments, hasAttachment);
        }
        return;
    }

    // Check content-disposition for attachment
    bool isAttachment = false;
    std::string filename;

    if (part->getHeader()->hasField(vmime::fields::CONTENT_DISPOSITION)) {
        auto cdf = part->getHeader()->findField<vmime::contentDispositionField>(vmime::fields::CONTENT_DISPOSITION);
        if (cdf) {
            auto disp = cdf->getValue<vmime::contentDisposition>();
            if (disp && disp->getName() != vmime::contentDispositionTypes::INLINE) {
                isAttachment = true;
                auto cdfField = vmime::dynamicCast<vmime::contentDispositionField>(cdf);
                if (cdfField && cdfField->hasFilename()) {
                    filename = cdfField->getFilename().getBuffer();
                }
            }
        }
    }

    auto mainType = ct.getType();
    auto subType = ct.getSubType();

    // Also treat non-text types as attachments
    if (mainType != vmime::mediaTypes::TEXT && !isAttachment) {
        isAttachment = true;
        if (filename.empty()) filename = "unknown";
    }

    if (isAttachment) {
        if (filename.empty()) {
            if (part->getHeader()->hasField(vmime::fields::CONTENT_TYPE)) {
                auto ctf = part->getHeader()->findField<vmime::contentTypeField>(vmime::fields::CONTENT_TYPE);
                if (ctf && ctf->hasParameter("name")) {
                    auto nameParam = ctf->getParameter("name");
                    if (nameParam) filename = nameParam->getValue().getBuffer();
                }
            }
            if (filename.empty()) filename = "unknown";
        }

        // Extract content
        std::string data;
        vmime::utility::outputStreamStringAdapter osa(data);
        body->getContents()->extract(osa);
        osa.flush();

        nlohmann::json att;
        att["filename"] = filename;
        att["content_type"] = ct.generate();
        att["size"] = (int)data.size();
        attachments.push_back(att);
        hasAttachment = true;
        return;
    }

    if (mainType == vmime::mediaTypes::TEXT) {
        // Extract content
        std::string content;
        vmime::utility::outputStreamStringAdapter osa(content);
        body->getContents()->extract(osa);
        osa.flush();

        // Try to convert to UTF-8 using charset
        vmime::charset charset = body->getCharset();
        if (charset.getName() != vmime::charsets::UTF_8) {
            try {
                vmime::shared_ptr<vmime::charsetConverter> conv =
                    vmime::charsetConverter::create(charset, vmime::charset(vmime::charsets::UTF_8));
                std::string converted;
                conv->convert(content, converted);
                content = converted;
            } catch (...) {
                // Keep original if conversion fails
            }
        }

        if (subType == vmime::mediaTypes::TEXT_PLAIN && textBody.empty()) {
            textBody = content;
        } else if (subType == vmime::mediaTypes::TEXT_HTML && htmlBody.empty()) {
            htmlBody = content;
        }
    }
}

extern "C" int email_parse_eml(const char* filePath, char* outJson, int outSize) {
    if (!filePath || !outJson || outSize <= 0) {
        if (outJson && outSize > 0) {
            snprintf(outJson, outSize, R"({"status":"failed","error":"null_parameter"})");
        }
        return -1;
    }

    try {
        // Read file
        std::ifstream file(filePath, std::ios::binary);
        if (!file.is_open()) {
            snprintf(outJson, outSize, R"({"status":"failed","error":"file_not_found"})");
            return -2;
        }

        std::ostringstream oss;
        oss << file.rdbuf();
        std::string content = oss.str();
        file.close();

        // Parse with vmime
        vmime::shared_ptr<vmime::message> msg = vmime::make_shared<vmime::message>();
        msg->parse(content);

        // Extract subject (decode RFC 2047 encoded words)
        std::string subject;
        auto header = msg->getHeader();
        if (header->hasField("Subject")) {
            auto subjField = header->findField(vmime::fields::SUBJECT);
            if (subjField) {
                auto val = subjField->getValue<vmime::text>();
                if (val) {
                    subject = val->getConvertedText(vmime::charset(vmime::charsets::UTF_8));
                }
            }
        }

        // Extract body parts
        std::string textBody;
        std::string htmlBody;
        nlohmann::json attachments = nlohmann::json::array();
        bool hasAttachment = false;

        extractParts(msg, textBody, htmlBody, attachments, hasAttachment);

        nlohmann::json response;
        response["status"] = "success";
        response["subject"] = subject;
        response["text_body"] = textBody;
        response["html_body"] = htmlBody;
        response["has_attachments"] = hasAttachment;
        response["attachments"] = attachments;

        std::string jsonStr = response.dump();
        if ((int)jsonStr.size() >= outSize) {
            // Truncate - but better to return error
            snprintf(outJson, outSize, R"({"status":"failed","error":"output_too_small","needed":%d})", (int)jsonStr.size());
            return -3;
        }
        snprintf(outJson, outSize, "%s", jsonStr.c_str());

        LOG_INFO("email_parse_eml: parsed %s, text=%zu bytes, html=%zu bytes, attachments=%zu\n",
                 filePath, textBody.size(), htmlBody.size(), attachments.size());
        return 0;
    } catch (const vmime::exception& e) {
        snprintf(outJson, outSize, R"({"status":"failed","error":"vmime_exception"})");
        LOG_INFO("email_parse_eml: vmime exception: %s\n", e.what());
        return -4;
    } catch (const std::exception& e) {
        snprintf(outJson, outSize, R"({"status":"failed","error":"std_exception"})");
        LOG_INFO("email_parse_eml: std exception: %s\n", e.what());
        return -5;
    } catch (...) {
        snprintf(outJson, outSize, R"({"status":"failed","error":"unknown_exception"})");
        return -6;
    }
}

extern "C" int email_update_session_read(const char* sessionId) {
    if (!sessionId) {
        LOG_INFO("email_update_session_read: sessionId is null\n");
        return -1;
    }

    sqlite3* db = email_core_get_db();
    if (!db) {
        LOG_INFO("email_update_session_read: database not initialized\n");
        return -2;
    }

    std::lock_guard<std::mutex> lock(email_core_get_db_mutex());

    const char* sql = "UPDATE session SET isread = 1 WHERE session_id = ?;";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        LOG_INFO("email_update_session_read: prepare failed: %s\n", sqlite3_errmsg(db));
        return -3;
    }

    sqlite3_bind_text(stmt, 1, sessionId, -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        LOG_INFO("email_update_session_read: update failed: %s\n", sqlite3_errmsg(db));
        return -4;
    }

    LOG_INFO("email_update_session_read: updated session %s to isread=1\n", sessionId);
    return 0;
}

extern "C" int email_query_session_unread(const char* sessionId) {
    if (!sessionId) {
        LOG_INFO("email_query_session_unread: sessionId is null\n");
        return -1;
    }

    sqlite3* db = email_core_get_db();
    if (!db) {
        LOG_INFO("email_query_session_unread: database not initialized\n");
        return -2;
    }

    std::lock_guard<std::mutex> lock(email_core_get_db_mutex());

    const char* sql = "SELECT COUNT(*) FROM session WHERE session_id = ? AND isread = 0;";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        LOG_INFO("email_query_session_unread: prepare failed: %s\n", sqlite3_errmsg(db));
        return -3;
    }

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
