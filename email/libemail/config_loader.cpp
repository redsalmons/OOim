#include "config_loader.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <random>
#include <sstream>
#include <iomanip>
#include <array>

namespace oemail {

// Generate 8-digit random number + type format (e.g., "12345678.163.com")
static std::string generate_email_id(const std::string& type) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(10000000, 99999999);
    
    int random_num = dis(gen);
    std::ostringstream oss;
    oss << random_num << "." << type;
    
    return oss.str();
}

ConfigLoader::ConfigLoader(const std::string& config_path)
    : config_path_(config_path) {
}

bool ConfigLoader::load() {
    std::ifstream file(config_path_);
    if (!file.is_open()) {
        last_error_ = "Failed to open config file: " + config_path_;
        return false;
    }
    
    try {
        nlohmann::json config_json;
        file >> config_json;
        
        email_configs_.clear();
        
        // Support both "email_list" (C++ format) and "accounts" (Dart format)
        if (config_json.contains("email_list") && config_json["email_list"].is_array()) {
            for (const auto& email_json : config_json["email_list"]) {
                EmailConfig config = json_to_email_config(email_json);
                email_configs_.push_back(config);
            }
        } else if (config_json.contains("accounts") && config_json["accounts"].is_array()) {
            for (const auto& email_json : config_json["accounts"]) {
                EmailConfig config = json_to_email_config(email_json);
                email_configs_.push_back(config);
            }
        }
        
        // Load top-level phrase_test
        if (config_json.contains("phrase_test")) {
            phrase_test_ = config_json["phrase_test"];
        }
        
        // Ensure id and uid fields exist
        ensure_id_uid_fields();
        
        last_error_.clear();
        return true;
    } catch (const std::exception& e) {
        last_error_ = std::string("Failed to parse config: ") + e.what();
        return false;
    }
}

bool ConfigLoader::save() {
    try {
        nlohmann::json config_json;
        config_json["email_list"] = nlohmann::json::array();
        
        for (const auto& config : email_configs_) {
            config_json["email_list"].push_back(email_config_to_json(config));
        }
        
        if (!phrase_test_.empty()) {
            config_json["phrase_test"] = phrase_test_;
        }
        
        std::ofstream file(config_path_);
        if (!file.is_open()) {
            last_error_ = "Failed to open config file for writing: " + config_path_;
            return false;
        }
        
        file << config_json.dump(4);
        file.close();
        
        last_error_.clear();
        return true;
    } catch (const std::exception& e) {
        last_error_ = std::string("Failed to save config: ") + e.what();
        return false;
    }
}

std::vector<EmailConfig> ConfigLoader::get_email_configs() const {
    return email_configs_;
}

void ConfigLoader::add_email_config(const EmailConfig& config) {
    email_configs_.push_back(config);
    ensure_id_uid_fields();
}

void ConfigLoader::update_email_config(const EmailConfig& config) {
    for (auto& ec : email_configs_) {
        if (ec.id == config.id) {
            ec = config;
            ensure_id_uid_fields();
            return;
        }
    }
    // If not found, add it
    email_configs_.push_back(config);
    ensure_id_uid_fields();
}

bool ConfigLoader::delete_email_config(const std::string& email_id) {
    for (auto it = email_configs_.begin(); it != email_configs_.end(); ++it) {
        if (it->id == email_id) {
            email_configs_.erase(it);
            last_error_.clear();
            return true;
        }
    }
    last_error_ = "Email config with id " + email_id + " not found";
    return false;
}

bool ConfigLoader::update_email_uid(const std::string& email_id, int new_uid) {
    for (auto& config : email_configs_) {
        if (config.id == email_id) {
            config.uid = new_uid;
            return true;
        }
    }
    last_error_ = "Email config with id " + email_id + " not found";
    return false;
}

std::string ConfigLoader::get_last_error() const {
    return last_error_;
}

std::string ConfigLoader::get_phrase_test() const {
    return phrase_test_;
}

void ConfigLoader::set_phrase_test(const std::string& phrase_test) {
    phrase_test_ = phrase_test;
}

bool ConfigLoader::clear_refresh_token(const std::string& email_id) {
    for (auto& config : email_configs_) {
        if (config.id == email_id) {
            config.refresh_token.clear();
            last_error_.clear();
            return true;
        }
    }
    last_error_ = "Email config with id " + email_id + " not found";
    return false;
}

void ConfigLoader::clear_all_outlook_refresh_tokens() {
    for (auto& config : email_configs_) {
        if (config.type == "outlook" || config.type == "outlook.com" || config.type == "hotmail.com") {
            config.refresh_token.clear();
        }
    }
    last_error_.clear();
}

bool ConfigLoader::clear_all_outlook_refresh_tokens_and_save() {
    clear_all_outlook_refresh_tokens();
    return save();
}

bool ConfigLoader::update_outlook_tenant_id_to_common_and_save() {
    for (auto& config : email_configs_) {
        if (config.type == "outlook" || config.type == "outlook.com" || config.type == "hotmail.com") {
            config.tenant_id = "common";
        }
    }
    last_error_.clear();
    return save();
}

void ConfigLoader::ensure_id_uid_fields() {
    for (auto& config : email_configs_) {
        // If id is missing, generate an 8-digit random number + type
        if (config.id.empty()) {
            config.id = generate_email_id(config.type);
        }
        
        // If uid is missing or invalid, set it to 0
        if (config.uid < 0) {
            config.uid = 0;
        }
    }
}

nlohmann::json ConfigLoader::email_config_to_json(const EmailConfig& config) const {
    nlohmann::json json;
    json["auth_code"] = config.auth_code;
    json["email"] = config.email;
    json["imap_port"] = config.imap_port;
    json["imap_server"] = config.imap_server;
    json["smtp_port"] = config.smtp_port;
    json["smtp_server"] = config.smtp_server;
    json["type"] = config.type;
    json["id"] = config.id;
    json["uid"] = config.uid;
    if (!config.phrase.empty()) json["phrase"] = config.phrase;
    
    // OAuth fields for Outlook
    if (!config.client_id.empty()) json["client_id"] = config.client_id;
    if (!config.tenant_id.empty()) json["tenant_id"] = config.tenant_id;
    if (!config.refresh_token.empty()) json["refresh_token"] = config.refresh_token;
    if (!config.account_type.empty()) json["account_type"] = config.account_type;
    
    return json;
}

EmailConfig ConfigLoader::json_to_email_config(const nlohmann::json& json) const {
    EmailConfig config;
    
    if (json.contains("auth_code")) config.auth_code = json["auth_code"];
    if (json.contains("email")) config.email = json["email"];
    if (json.contains("imap_port")) config.imap_port = json["imap_port"];
    if (json.contains("imap_server")) config.imap_server = json["imap_server"];
    if (json.contains("smtp_port")) config.smtp_port = json["smtp_port"];
    if (json.contains("smtp_server")) config.smtp_server = json["smtp_server"];
    if (json.contains("type")) config.type = json["type"];
    if (json.contains("id")) config.id = json["id"];
    if (json.contains("uid")) config.uid = json["uid"];
    else config.uid = 0;  // Default to 0 if missing
    
    if (json.contains("phrase")) config.phrase = json["phrase"];
    
    // OAuth fields for Outlook
    if (json.contains("client_id")) config.client_id = json["client_id"];
    if (json.contains("tenant_id")) config.tenant_id = json["tenant_id"];
    if (json.contains("refresh_token")) config.refresh_token = json["refresh_token"];
    if (json.contains("account_type")) config.account_type = json["account_type"];
    
    return config;
}

} // namespace oemail
