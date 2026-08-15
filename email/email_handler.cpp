#include "email_core_common.h"
#include "email_core.h"
#include "logger.h"
#include "email_handler_c.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <nlohmann/json.hpp>

#include <vmime/vmime.hpp>
#include <vmime/platforms/posix/posixHandler.hpp>
#include <vmime/security/cert/defaultCertificateVerifier.hpp>
#include <vmime/net/smtp/SMTPTransport.hpp>
#include <vmime/contentDispositionField.hpp>
#include <vmime/contentTypeField.hpp>

using json = nlohmann::json;

#define DEFAULT_CAPACITY 100

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
        return -2;
    }

    Email* email = &handler->emails[handler->count];

    email->sender = strdup(sender);
    email->recipient = strdup(recipient);
    email->subject = strdup(subject);
    email->body = strdup(body);

    time_t now = time(NULL);
    char* timestamp = ctime(&now);
    if (timestamp) {
        timestamp[strlen(timestamp) - 1] = '\0';
        email->timestamp = strdup(timestamp);
    } else {
        email->timestamp = strdup("Unknown");
    }

    if (!email->sender || !email->recipient || !email->subject ||
        !email->body || !email->timestamp) {
        email_free(email);
        return -3;
    }

    handler->count++;
    return 0;
}

int email_remove(EmailHandler* handler, int index) {
    if (!handler || index < 0 || index >= handler->count) {
        return -1;
    }

    email_free(&handler->emails[index]);

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
    return 0;
}

// Trust-all certificate verifier for SMTP
class SMTPTrustAllVerifier : public vmime::security::cert::defaultCertificateVerifier {
public:
    void verify(
        const vmime::shared_ptr<vmime::security::cert::certificateChain>& chain,
        const vmime::string& hostname
    ) override {
    }
};

int email_send_via_smtp(const char* smtp_server, int smtp_port,
                        const char* sender_email, const char* auth_code,
                        const char* recipient, const char* subject,
                        const char* body, const char* in_reply_to) {
    if (!smtp_server || !sender_email || !auth_code || !recipient || !subject || !body) {
        LOG_INFO("[SMTP] Missing required parameters\n");
        return -1;
    }

    LOG_INFO("[SMTP] Sending via %s:%d from %s to %s\n", smtp_server, smtp_port, sender_email, recipient);
    LOG_INFO("[SMTP] Subject: %s\n", subject);

    try {
        static bool vmime_initialized = false;
        if (!vmime_initialized) {
            vmime::platform::setHandler<vmime::platforms::posix::posixHandler>();
            vmime_initialized = true;
        }

        vmime::shared_ptr<vmime::net::session> sess = vmime::net::session::create();

        std::string url_str = "smtps://" + std::string(smtp_server) + ":" + std::to_string(smtp_port);
        vmime::utility::url url(url_str);

        vmime::shared_ptr<vmime::net::transport> tr = sess->getTransport(url);
        tr->setProperty("options.need-authentication", true);
        tr->setProperty("auth.username", sender_email);
        tr->setProperty("auth.password", auth_code);

        tr->setCertificateVerifier(vmime::make_shared<SMTPTrustAllVerifier>());
        tr->connect();
        LOG_INFO("[SMTP] Connected to %s\n", url_str.c_str());

        vmime::shared_ptr<vmime::message> msg = vmime::make_shared<vmime::message>();

        vmime::mailbox from(sender_email);
        msg->getHeader()->From()->setValue(from);

        std::string recipientsStr(recipient);
        vmime::addressList toList;
        size_t start = 0, end;
        while ((end = recipientsStr.find(',', start)) != std::string::npos) {
            std::string addr = recipientsStr.substr(start, end - start);
            size_t b = addr.find_first_not_of(" \t");
            size_t e = addr.find_last_not_of(" \t");
            if (b != std::string::npos) {
                addr = addr.substr(b, e - b + 1);
                toList.appendAddress(vmime::make_shared<vmime::mailbox>(addr));
            }
            start = end + 1;
        }
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

        msg->getHeader()->Subject()->setValue(vmime::text(subject));
        msg->getHeader()->Date()->setValue(vmime::datetime::now());
        msg->getHeader()->MessageId()->setValue(vmime::messageId::generateId());

        if (in_reply_to && strlen(in_reply_to) > 0) {
            msg->getHeader()->InReplyTo()->setValue(
                vmime::make_shared<vmime::messageId>(in_reply_to));
        }

        msg->getBody()->setContents(vmime::make_shared<vmime::stringContentHandler>(body));

        tr->send(msg);
        LOG_INFO("[SMTP] Email sent successfully\n");

        tr->disconnect();
        return 0;
    } catch (const vmime::exception& e) {
        LOG_INFO("[SMTP] VMime error: %s\n", e.what());
        return -2;
    } catch (const std::exception& e) {
        LOG_INFO("[SMTP] Error: %s\n", e.what());
        return -3;
    }
}

int email_receive(EmailHandler* handler) {
    if (!handler) {
        return -1;
    }
    LOG_INFO("[EMAIL_CORE] Receiving emails...\n");
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

// --- Config save/load ---

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

    try {
        json root = json::parse(json_str);
        free(json_str);

        if (root.contains("local_data_path") && root["local_data_path"].is_string()) {
            free(*local_data_path);
            *local_data_path = strdup(root["local_data_path"].get<std::string>().c_str());
        }

        if (!root.contains("accounts") || !root["accounts"].is_array()) {
            return -4;
        }

        int capacity = 0;
        json accounts_array = root["accounts"];

        for (const auto& account_obj : accounts_array) {
            if (!account_obj.is_object()) continue;

            EmailAccountConfig current;
            memset(&current, 0, sizeof(current));

            if (account_obj.contains("type") && account_obj["type"].is_string())
                current.type = strdup(account_obj["type"].get<std::string>().c_str());
            if (account_obj.contains("email") && account_obj["email"].is_string())
                current.email = strdup(account_obj["email"].get<std::string>().c_str());
            if (account_obj.contains("auth_code") && account_obj["auth_code"].is_string())
                current.auth_code = strdup(account_obj["auth_code"].get<std::string>().c_str());
            if (account_obj.contains("smtp_server") && account_obj["smtp_server"].is_string())
                current.smtp_server = strdup(account_obj["smtp_server"].get<std::string>().c_str());
            if (account_obj.contains("smtp_port") && account_obj["smtp_port"].is_number())
                current.smtp_port = account_obj["smtp_port"].get<int>();
            if (account_obj.contains("imap_server") && account_obj["imap_server"].is_string())
                current.imap_server = strdup(account_obj["imap_server"].get<std::string>().c_str());
            if (account_obj.contains("imap_port") && account_obj["imap_port"].is_number())
                current.imap_port = account_obj["imap_port"].get<int>();
            if (account_obj.contains("account_type") && account_obj["account_type"].is_string())
                current.account_type = strdup(account_obj["account_type"].get<std::string>().c_str());
            if (account_obj.contains("authorized") && account_obj["authorized"].is_number())
                current.authorized = account_obj["authorized"].get<int>();
            if (account_obj.contains("id") && account_obj["id"].is_string()) {
                std::string id_str = account_obj["id"].get<std::string>();
                if (id_str.empty() && current.type)
                    id_str = generate_email_id(current.type);
                current.id = strdup(id_str.c_str());
            }
            if (account_obj.contains("uid") && account_obj["uid"].is_number())
                current.uid = account_obj["uid"].get<int>();
            if (account_obj.contains("phrase") && account_obj["phrase"].is_string())
                current.phrase = strdup(account_obj["phrase"].get<std::string>().c_str());
            if (account_obj.contains("folder_size") && account_obj["folder_size"].is_number())
                current.folder_size = account_obj["folder_size"].get<long long>();

            if (*count >= capacity) {
                capacity = capacity == 0 ? 4 : capacity * 2;
                EmailAccountConfig* resized = (EmailAccountConfig*)realloc(
                    *accounts, capacity * sizeof(EmailAccountConfig));
                if (!resized) return -3;
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
    if (!accounts) return;
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

// --- Email file save/load ---

int email_save(EmailHandler* handler, const char* path) {
    if (!handler || !path) return -1;
    FILE* f = fopen(path, "w");
    if (!f) return -2;

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
    if (!handler || !path) return -1;
    FILE* f = fopen(path, "r");
    if (!f) return -2;

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
        if (line[0] == '\0') continue;

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
                    email_free(&current);
                }
            }
            in_email = 0;
            continue;
        }

        char* eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        const char* key = line;
        const char* value = eq + 1;

        if (strcmp(key, "sender") == 0) current.sender = strdup(value);
        else if (strcmp(key, "recipient") == 0) current.recipient = strdup(value);
        else if (strcmp(key, "subject") == 0) current.subject = strdup(value);
        else if (strcmp(key, "body") == 0) current.body = strdup(value);
        else if (strcmp(key, "timestamp") == 0) current.timestamp = strdup(value);
    }
    fclose(f);
    return 0;
}

// --- Chat contacts/messages save/load ---

int chat_contacts_save(const char* path, const ChatContact* contacts, int count) {
    if (!path) return -1;
    FILE* f = fopen(path, "w");
    if (!f) return -2;

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
    if (!path || !contacts || !count) return -1;
    FILE* f = fopen(path, "r");
    if (!f) return -2;

    int contact_count = 0;
    char line[4096];
    int capacity = 10;
    ChatContact* result = (ChatContact*)malloc(capacity * sizeof(ChatContact));
    if (!result) { fclose(f); return -3; }

    int in_contact = 0;
    ChatContact current;
    memset(&current, 0, sizeof(current));

    while (fgets(line, sizeof(line), f)) {
        trim_newline(line);
        if (line[0] == '\0') continue;

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
                    if (!new_result) { fclose(f); free(result); return -3; }
                    result = new_result;
                }
                result[contact_count] = current;
                contact_count++;
            }
            in_contact = 0;
            continue;
        }

        char* eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        const char* key = line;
        const char* value = eq + 1;

        if (strcmp(key, "name") == 0) set_field(&current.name, value);
        else if (strcmp(key, "last_message") == 0) set_field(&current.last_message, value);
        else if (strcmp(key, "time") == 0) set_field(&current.time, value);
        else if (strcmp(key, "unread") == 0) current.unread = atoi(value);
    }
    fclose(f);
    *contacts = result;
    *count = contact_count;
    return 0;
}

void chat_contacts_free(ChatContact* contacts, int count) {
    if (!contacts) return;
    for (int i = 0; i < count; i++) {
        free(contacts[i].name);
        free(contacts[i].last_message);
        free(contacts[i].time);
    }
    free(contacts);
}

int chat_messages_save(const char* path, const ChatMessage* messages, int count) {
    if (!path) return -1;
    FILE* f = fopen(path, "w");
    if (!f) return -2;

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
    if (!path || !messages || !count) return -1;
    FILE* f = fopen(path, "r");
    if (!f) return -2;

    int message_count = 0;
    char line[4096];
    int capacity = 10;
    ChatMessage* result = (ChatMessage*)malloc(capacity * sizeof(ChatMessage));
    if (!result) { fclose(f); return -3; }

    int in_message = 0;
    ChatMessage current;
    memset(&current, 0, sizeof(current));

    while (fgets(line, sizeof(line), f)) {
        trim_newline(line);
        if (line[0] == '\0') continue;

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
                    if (!new_result) { fclose(f); free(result); return -3; }
                    result = new_result;
                }
                result[message_count] = current;
                message_count++;
            }
            in_message = 0;
            continue;
        }

        char* eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        const char* key = line;
        const char* value = eq + 1;

        if (strcmp(key, "sender") == 0) set_field(&current.sender, value);
        else if (strcmp(key, "content") == 0) set_field(&current.content, value);
        else if (strcmp(key, "time") == 0) set_field(&current.time, value);
        else if (strcmp(key, "is_me") == 0) current.is_me = atoi(value);
    }
    fclose(f);
    *messages = result;
    *count = message_count;
    return 0;
}

void chat_messages_free(ChatMessage* messages, int count) {
    if (!messages) return;
    for (int i = 0; i < count; i++) {
        free(messages[i].sender);
        free(messages[i].content);
        free(messages[i].time);
    }
    free(messages);
}
