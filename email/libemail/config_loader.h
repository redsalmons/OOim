#ifndef CONFIG_LOADER_H
#define CONFIG_LOADER_H

#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace oemail {

struct EmailConfig {
    std::string auth_code;
    std::string email;
    int imap_port;
    std::string imap_server;
    int smtp_port;
    std::string smtp_server;
    std::string type;
    std::string id;  // Unique identifier (same as uid)
    int uid;         // Default 0
    long long folder_size;  // Machine folder size in bytes
    std::string phrase; // Random phrase encrypted with master password (base64)
    std::string decrypted_phrase; // IN MEMORY ONLY: Decrypted phrase used as the actual encryption key
    
    // OAuth fields for Outlook
    std::string client_id;
    std::string tenant_id;
    std::string refresh_token;
    std::string account_type; // "personal" or "enterprise" for Outlook accounts
};

class ConfigLoader {
public:
    ConfigLoader(const std::string& config_path);
    ~ConfigLoader() = default;
    
    // Load config from file
    bool load();
    
    // Save config to file
    bool save();
    
    // Get email configurations
    std::vector<EmailConfig> get_email_configs() const;
    
    // Add email configuration
    void add_email_config(const EmailConfig& config);
    
    // Update existing email configuration
    void update_email_config(const EmailConfig& config);
    
    // Delete email configuration by id
    bool delete_email_config(const std::string& email_id);
    
    // Update uid for email configuration by id
    bool update_email_uid(const std::string& email_id, int new_uid);
    
    // Get last error
    std::string get_last_error() const;
    
    // Get top-level phrase_test for master password verification
    std::string get_phrase_test() const;
    
    // Set top-level phrase_test
    void set_phrase_test(const std::string& phrase_test);
    
    // Clear refresh token for a specific email config
    bool clear_refresh_token(const std::string& email_id);
    
    // Clear all refresh tokens for Outlook accounts
    void clear_all_outlook_refresh_tokens();
    
    // Clear all refresh tokens for Outlook accounts and save
    bool clear_all_outlook_refresh_tokens_and_save();
    
    // Update tenant_id for all Outlook accounts to "common"
    bool update_outlook_tenant_id_to_common_and_save();
    
private:
    std::string config_path_;
    std::vector<EmailConfig> email_configs_;
    std::string last_error_;
    std::string phrase_test_;
    
    // Ensure id and uid fields exist in email configs
    void ensure_id_uid_fields();
    
    // Convert EmailConfig to JSON
    nlohmann::json email_config_to_json(const EmailConfig& config) const;
    
    // Convert JSON to EmailConfig
    EmailConfig json_to_email_config(const nlohmann::json& json) const;
};

} // namespace oemail

#endif // CONFIG_LOADER_H
