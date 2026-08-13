#ifndef EMAIL_CONFIG_PROVIDER_H
#define EMAIL_CONFIG_PROVIDER_H

#include <string>
#include <vector>
#include <map>
#include "config_loader.h"

namespace oemail {

struct ConfigField {
    std::string label;
    std::string key;
    bool required;
    std::string default_value;
    int tag;
};

class EmailConfigProvider {
public:
    virtual ~EmailConfigProvider() = default;
    
    // Get email type identifier
    virtual std::string get_email_type() const = 0;
    
    // Get display name
    virtual std::string get_display_name() const = 0;
    
    // Get configuration fields
    virtual std::vector<ConfigField> get_config_fields() const = 0;
    
    // Build EmailConfig from field values
    virtual EmailConfig build_config(const std::map<std::string, std::string>& field_values, 
                                     const std::string& master_password) const = 0;
    
    // Validate field values
    virtual bool validate_fields(const std::map<std::string, std::string>& field_values, 
                                 std::string& error_message) const = 0;
};

class Email163ConfigProvider : public EmailConfigProvider {
public:
    std::string get_email_type() const override {
        return "163.com";
    }
    
    std::string get_display_name() const override {
        return "emailType163";
    }
    
    std::vector<ConfigField> get_config_fields() const override;
    
    EmailConfig build_config(const std::map<std::string, std::string>& field_values,
                             const std::string& master_password) const override;
    
    bool validate_fields(const std::map<std::string, std::string>& field_values,
                         std::string& error_message) const override;
};

class EmailOutlookConfigProvider : public EmailConfigProvider {
public:
    std::string get_email_type() const override {
        return "outlook.com";
    }
    
    std::string get_display_name() const override {
        return "emailTypeOutlook";
    }
    
    std::vector<ConfigField> get_config_fields() const override;
    
    EmailConfig build_config(const std::map<std::string, std::string>& field_values,
                             const std::string& master_password) const override;
    
    bool validate_fields(const std::map<std::string, std::string>& field_values,
                         std::string& error_message) const override;
};

class EmailGmailConfigProvider : public EmailConfigProvider {
public:
    std::string get_email_type() const override {
        return "gmail.com";
    }
    
    std::string get_display_name() const override {
        return "emailTypeGmail";
    }
    
    std::vector<ConfigField> get_config_fields() const override;
    
    EmailConfig build_config(const std::map<std::string, std::string>& field_values,
                             const std::string& master_password) const override;
    
    bool validate_fields(const std::map<std::string, std::string>& field_values,
                         std::string& error_message) const override;
};

// Factory to get provider for email type
class EmailConfigProviderFactory {
public:
    static std::unique_ptr<EmailConfigProvider> get_provider(const std::string& email_type);
    static std::vector<std::string> get_supported_types();
};

} // namespace oemail

#endif // EMAIL_CONFIG_PROVIDER_H
