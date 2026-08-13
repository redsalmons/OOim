#include "email_config_provider.h"
#include "crypto_utils.h"
#include "crypto_aes.h"
#include "email_factory.h"
#include "oauth_constants.h"
#include <random>
#include <sstream>

namespace oemail {

std::vector<ConfigField> Email163ConfigProvider::get_config_fields() const {
    return {
        {"emailLabel", "email", true, "", 1},
        {"authCodeLabel", "auth_code", true, "", 2},
        {"smtpServerLabel", "smtp_server", false, "smtp.163.com", 3},
        {"smtpPortLabel", "smtp_port", false, "465", 4},
        {"imapServerLabel", "imap_server", false, "imap.163.com", 5},
        {"imapPortLabel", "imap_port", false, "993", 6}
    };
}

EmailConfig Email163ConfigProvider::build_config(
    const std::map<std::string, std::string>& field_values,
    const std::string& master_password) const {
    
    EmailConfig config;
    config.type = get_email_type();
    config.uid = 0;
    
    // Set field values
    auto it = field_values.find("email");
    if (it != field_values.end()) {
        config.email = it->second;
    }
    
    it = field_values.find("auth_code");
    if (it != field_values.end()) {
        config.auth_code = it->second;
    }
    
    it = field_values.find("smtp_server");
    if (it != field_values.end()) {
        config.smtp_server = it->second;
    }
    
    it = field_values.find("smtp_port");
    if (it != field_values.end()) {
        try {
            config.smtp_port = std::stoi(it->second);
        } catch (...) {
            config.smtp_port = 465;
        }
    }
    
    it = field_values.find("imap_server");
    if (it != field_values.end()) {
        config.imap_server = it->second;
    }
    
    it = field_values.find("imap_port");
    if (it != field_values.end()) {
        try {
            config.imap_port = std::stoi(it->second);
        } catch (...) {
            config.imap_port = 993;
        }
    }
    
    // Generate random phrase
    std::string random_phrase = crypto_utils::generate_random_phrase();
    
    // Derive master key
    std::string master_key = crypto_utils::derive_master_key(master_password);
    
    // Encrypt phrase with master key using AES
    CryptoAES aes;
    aes.set_key(master_key);
    std::vector<uint8_t> encrypted_phrase = aes.encrypt(
        std::vector<uint8_t>(random_phrase.begin(), random_phrase.end()));
    
    // Base64 encode encrypted phrase
    std::string phrase_base64 = crypto_utils::base64_encode(encrypted_phrase);
    config.phrase = phrase_base64;
    
    // Generate id (8 char alphanumeric + . + email type)
    const char charset[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    std::string id;
    id.reserve(8);
    
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, sizeof(charset) - 2);
    
    for (int i = 0; i < 8; ++i) {
        id += charset[dis(gen)];
    }
    
    config.id = id + "." + config.type;
    
    return config;
}

bool Email163ConfigProvider::validate_fields(
    const std::map<std::string, std::string>& field_values,
    std::string& error_message) const {
    
    auto it = field_values.find("email");
    if (it == field_values.end() || it->second.empty()) {
        error_message = "emailCannotBeEmpty";
        return false;
    }
    
    it = field_values.find("auth_code");
    if (it == field_values.end() || it->second.empty()) {
        error_message = "authCodeCannotBeEmpty";
        return false;
    }
    
    return true;
}

std::vector<ConfigField> EmailOutlookConfigProvider::get_config_fields() const {
    return {
        {"emailLabel", "email", false, "", 1},
        {"accountTypeLabel", "account_type", false, "personal", 8},
        {"smtpServerLabel", "smtp_server", false, "smtp-mail.outlook.com", 3},
        {"smtpPortLabel", "smtp_port", false, "587", 4},
        {"imapServerLabel", "imap_server", false, "outlook.office365.com", 5},
        {"imapPortLabel", "imap_port", false, "993", 6},
        {"authStatusLabel", "auth_status", false, "unauthorized", 7}
    };
}

EmailConfig EmailOutlookConfigProvider::build_config(
    const std::map<std::string, std::string>& field_values,
    const std::string& master_password) const {
    
    EmailConfig config;
    config.type = get_email_type();
    config.uid = 0;
    
    // Set email from field values if provided
    auto email_it = field_values.find("email");
    if (email_it != field_values.end()) {
        config.email = email_it->second;
    }
    
    // Determine account type (personal or enterprise)
    auto account_type_it = field_values.find("account_type");
    std::string account_type = (account_type_it != field_values.end() && !account_type_it->second.empty())
                               ? account_type_it->second : "personal";
    if (account_type != "personal" && account_type != "enterprise") {
        account_type = "personal";
    }
    config.account_type = account_type;
    
    // Set default Outlook server and OAuth values based on account type
    std::string default_smtp_server = (account_type == "enterprise") ? "smtp.office365.com" : "smtp-mail.outlook.com";
    std::string default_imap_server = "outlook.office365.com";
    std::string default_client_id = (account_type == "enterprise")
        ? "08162f7c-0fd2-4200-a84a-f25a4db0b584"
        : "6050c77b-bfc7-47fa-8ae3-d9dcae5b29bc";
    
    auto smtp_server_it = field_values.find("smtp_server");
    config.smtp_server = (smtp_server_it != field_values.end() && !smtp_server_it->second.empty())
                         ? smtp_server_it->second : default_smtp_server;
    
    auto smtp_port_it = field_values.find("smtp_port");
    config.smtp_port = 587;
    if (smtp_port_it != field_values.end() && !smtp_port_it->second.empty()) {
        try { config.smtp_port = std::stoi(smtp_port_it->second); } catch (...) {}
    }
    
    auto imap_server_it = field_values.find("imap_server");
    config.imap_server = (imap_server_it != field_values.end() && !imap_server_it->second.empty())
                         ? imap_server_it->second : default_imap_server;
    
    auto imap_port_it = field_values.find("imap_port");
    config.imap_port = 993;
    if (imap_port_it != field_values.end() && !imap_port_it->second.empty()) {
        try { config.imap_port = std::stoi(imap_port_it->second); } catch (...) {}
    }
    
    // Set OAuth fields based on account type
    config.client_id = default_client_id;
    config.tenant_id = "common";
    
    // Generate random phrase
    std::string random_phrase = crypto_utils::generate_random_phrase();
    
    // Derive master key
    std::string master_key = crypto_utils::derive_master_key(master_password);
    
    // Encrypt phrase with master key using AES
    CryptoAES aes;
    aes.set_key(master_key);
    std::vector<uint8_t> encrypted_phrase = aes.encrypt(
        std::vector<uint8_t>(random_phrase.begin(), random_phrase.end()));
    
    // Base64 encode encrypted phrase
    std::string phrase_base64 = crypto_utils::base64_encode(encrypted_phrase);
    config.phrase = phrase_base64;
    
    // Generate id (8 char alphanumeric + . + email type)
    const char charset[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    std::string id;
    id.reserve(8);
    
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, sizeof(charset) - 2);
    
    for (int i = 0; i < 8; ++i) {
        id += charset[dis(gen)];
    }
    
    config.id = id + "." + config.type;
    
    return config;
}

bool EmailOutlookConfigProvider::validate_fields(
    const std::map<std::string, std::string>& field_values,
    std::string& error_message) const {
    
    // No required fields for Outlook
    return true;
}

std::unique_ptr<EmailConfigProvider> EmailConfigProviderFactory::get_provider(
    const std::string& email_type) {
    
    if (email_type == "163.com" || email_type == "emailType163") {
        return std::make_unique<Email163ConfigProvider>();
    }
    
    if (email_type == "outlook.com" || email_type == "emailTypeOutlook") {
        return std::make_unique<EmailOutlookConfigProvider>();
    }
    
    if (email_type == "gmail.com" || email_type == "emailTypeGmail") {
        return std::make_unique<EmailGmailConfigProvider>();
    }
    
    // Add more providers here for other email types
    // For now, default to 163.com
    return std::make_unique<Email163ConfigProvider>();
}

std::vector<std::string> EmailConfigProviderFactory::get_supported_types() {
    return {"163.com", "outlook.com", "gmail.com"};
}

std::vector<ConfigField> EmailGmailConfigProvider::get_config_fields() const {
    return {
        {"emailLabel", "email", false, "", 1},
        {"smtpServerLabel", "smtp_server", false, "smtp.gmail.com", 3},
        {"smtpPortLabel", "smtp_port", false, "587", 4},
        {"imapServerLabel", "imap_server", false, "imap.gmail.com", 5},
        {"imapPortLabel", "imap_port", false, "993", 6},
        {"authStatusLabel", "auth_status", false, "unauthorized", 7}
    };
}

EmailConfig EmailGmailConfigProvider::build_config(
    const std::map<std::string, std::string>& field_values,
    const std::string& master_password) const {
    
    EmailConfig config;
    config.type = get_email_type();
    config.uid = 0;
    
    // Set email from field values if provided
    auto email_it = field_values.find("email");
    if (email_it != field_values.end()) {
        config.email = email_it->second;
    }
    
    // Set Gmail server values from fields, with defaults
    auto smtp_server_it = field_values.find("smtp_server");
    config.smtp_server = (smtp_server_it != field_values.end() && !smtp_server_it->second.empty())
                         ? smtp_server_it->second : "smtp.gmail.com";
    
    auto smtp_port_it = field_values.find("smtp_port");
    config.smtp_port = 587;
    if (smtp_port_it != field_values.end() && !smtp_port_it->second.empty()) {
        try { config.smtp_port = std::stoi(smtp_port_it->second); } catch (...) {}
    }
    
    auto imap_server_it = field_values.find("imap_server");
    config.imap_server = (imap_server_it != field_values.end() && !imap_server_it->second.empty())
                         ? imap_server_it->second : "imap.gmail.com";
    
    auto imap_port_it = field_values.find("imap_port");
    config.imap_port = 993;
    if (imap_port_it != field_values.end() && !imap_port_it->second.empty()) {
        try { config.imap_port = std::stoi(imap_port_it->second); } catch (...) {}
    }
    
    // Set default OAuth fields for Gmail
    config.client_id = oemail::GMAIL_OAUTH_CLIENT_ID;
    config.tenant_id = "";
    
    // Generate random phrase
    std::string random_phrase = crypto_utils::generate_random_phrase();
    
    // Derive master key
    std::string master_key = crypto_utils::derive_master_key(master_password);
    
    // Encrypt phrase with master key using AES
    CryptoAES aes;
    aes.set_key(master_key);
    std::vector<uint8_t> encrypted_phrase = aes.encrypt(
        std::vector<uint8_t>(random_phrase.begin(), random_phrase.end()));
    
    // Base64 encode encrypted phrase
    std::string phrase_base64 = crypto_utils::base64_encode(encrypted_phrase);
    config.phrase = phrase_base64;
    
    // Generate id (8 char alphanumeric + . + email type)
    const char charset[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    std::string id;
    id.reserve(8);
    
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, sizeof(charset) - 2);
    
    for (int i = 0; i < 8; ++i) {
        id += charset[dis(gen)];
    }
    
    config.id = id + "." + config.type;
    
    return config;
}

bool EmailGmailConfigProvider::validate_fields(
    const std::map<std::string, std::string>& field_values,
    std::string& error_message) const {
    
    // No required fields for Gmail (OAuth2)
    return true;
}

} // namespace oemail
