#include "email_handler.h"
#include "email_opt_163_impl.h"
#include "email_opt_gmail_impl.h"
#include "email_opt_outlook_impl.h"
#include "email_outlook.h"
#include "email_google.h"
#include "config_loader.h"
#include "email_config_provider.h"
#include "logger.h"
#include <fstream>

namespace oemailim {

// Static member variable definitions
std::shared_ptr<EmailHandler> EmailHandler::g_instance;
std::vector<std::shared_ptr<EmailComm::Email>> EmailHandler::g_EmailConfigIndices;
std::string EmailHandler::g_workspace;
std::string EmailHandler::g_data;
std::string EmailHandler::g_log;
NotificationCallback EmailHandler::g_callback;

EmailHandler::EmailHandler() {
    nextConfigIndex = 0;
}

EmailHandler::~EmailHandler() {
}

std::shared_ptr<EmailHandler> EmailHandler::getInstance() {
    if (!g_instance) {
        g_instance = std::make_shared<EmailHandler>();
    }
    return g_instance;
}

bool EmailHandler::systemOpen(const std::string& dataDir,
                              const std::string& configDir,
                              const std::string& logDir,
                              NotificationCallback callback) {
    // Set static member variables
    g_workspace = configDir;
    g_data = dataDir;
    g_log = logDir;
    g_callback = callback;
    
    // Set instance member variables
    this->dataDir = dataDir;
    this->configDir = configDir;
    this->logDir = logDir;
    this->callback = callback;
    
    // Clear previously loaded configs to prevent duplicates on multiple systemOpen calls
    g_EmailConfigIndices.clear();
    
    // Load email configurations from config file
    std::string configFilePath = configDir + "/oim.conf";
    oemail::ConfigLoader loader(configFilePath);
    if (loader.load()) {
        auto configs = loader.get_email_configs();
        LOG_INFO("Loaded %zu email configurations from %s\n", configs.size(), configDir.c_str());
        for (const auto& config : configs) {
            LOG_INFO("Loading email: %s type: %s\n", config.email.c_str(), config.type.c_str());
            // Load each email configuration
            loadEmailConfig(config);
        }
        LOG_INFO("Total loaded emails: %zu\n", g_EmailConfigIndices.size());
    } else {
        LOG_INFO("Failed to load config from %s: %s\n", configDir.c_str(), loader.get_last_error().c_str());
    }
    
    return true;
}

int EmailHandler::OpenNewEmail(const std::string& email_id) {
    LOG_INFO("OpenNewEmail called with email_id: %s\n", email_id.c_str());
    LOG_INFO("OpenNewEmail: g_workspace = %s\n", g_workspace.c_str());
    LOG_INFO("OpenNewEmail: g_workspace empty = %d\n", g_workspace.empty());
    
    if (g_workspace.empty()) {
        LOG_INFO("OpenNewEmail: ERROR - g_workspace is empty!\n");
        return -1;
    }
    
    // Load config to find the email configuration by ID
    std::string configFilePath = g_workspace + "/oim.conf";
    LOG_INFO("OpenNewEmail: Loading config from %s\n", configFilePath.c_str());
    
    // Check if file exists
    std::ifstream testFile(configFilePath);
    if (!testFile.good()) {
        LOG_INFO("OpenNewEmail: ERROR - Config file does not exist or is not readable: %s\n", configFilePath.c_str());
        return -1;
    }
    testFile.close();
    
    oemail::ConfigLoader loader(configFilePath);
    if (!loader.load()) {
        LOG_INFO("Failed to load config for OpenNewEmail: %s\n", loader.get_last_error().c_str());
        return -1;
    }

    auto configs = loader.get_email_configs();
    LOG_INFO("OpenNewEmail: Loaded %zu email configs\n", configs.size());
    
    oemail::EmailConfig target_config;
    bool found = false;
    
    for (const auto& config : configs) {
        LOG_INFO("OpenNewEmail: Checking config id=%s against requested id=%s\n", config.id.c_str(), email_id.c_str());
        if (config.id == email_id) {
            target_config = config;
            found = true;
            break;
        }
    }
    
    if (!found) {
        LOG_INFO("Email config with id %s not found\n", email_id.c_str());
        return -1;
    }

    LOG_INFO("OpenNewEmail: Found config for %s type=%s\n", target_config.email.c_str(), target_config.type.c_str());

    // Create a new email instance based on the found config
    std::shared_ptr<EmailComm::Email> email;
    std::shared_ptr<EmailComm::EmailOptInterface> delegate;
    oemailim::EmailProvider provider;

    // Determine provider type from config.type
    if (target_config.type == "163.com" || target_config.type == "emailType163" ||
        target_config.type == "qq.com" || target_config.type == "emailTypeQQ") {
        
        bool isQQ = (target_config.type == "qq.com" || target_config.type == "emailTypeQQ");
        std::string defaultSmtp = isQQ ? "smtp.qq.com" : "smtp.163.com";
        std::string defaultImap = isQQ ? "imap.qq.com" : "imap.163.com";
        
        provider = oemailim::EmailProvider::EMAIL_PROVIDER_163;
        email = std::make_shared<EmailComm::Email163>(
            target_config.smtp_server.empty() ? defaultSmtp : target_config.smtp_server,
            target_config.smtp_port > 0 ? target_config.smtp_port : 465,
            target_config.imap_server.empty() ? defaultImap : target_config.imap_server,
            target_config.imap_port > 0 ? target_config.imap_port : 993
        );
        delegate = std::make_shared<EmailComm::EmailOpt163Impl>(g_instance);
    } else if (target_config.type == "outlook.com" || target_config.type == "emailTypeOutlook") {
        provider = oemailim::EmailProvider::EMAIL_PROVIDER_OUTLOOK;
        email = std::make_shared<EmailComm::EmailOutlook>(
            target_config.smtp_server.empty() ? "smtp-mail.outlook.com" : target_config.smtp_server,
            target_config.smtp_port > 0 ? target_config.smtp_port : 587,
            target_config.imap_server.empty() ? "outlook.office365.com" : target_config.imap_server,
            target_config.imap_port > 0 ? target_config.imap_port : 993
        );
        delegate = std::make_shared<EmailComm::EmailOptOutlookImpl>(g_instance);
    } else if (target_config.type == "gmail.com" || target_config.type == "emailTypeGmail") {
        provider = oemailim::EmailProvider::EMAIL_PROVIDER_GMAIL;
        email = std::make_shared<EmailComm::EmailGoogle>(
            target_config.smtp_server.empty() ? "smtp.gmail.com" : target_config.smtp_server,
            target_config.smtp_port > 0 ? target_config.smtp_port : 587,
            target_config.imap_server.empty() ? "imap.gmail.com" : target_config.imap_server,
            target_config.imap_port > 0 ? target_config.imap_port : 993
        );
        delegate = std::make_shared<EmailComm::EmailOptGmailImpl>(g_instance);
    } else {
        return -1; // Unknown provider type
    }

    // Set the delegate
    if (delegate) {
        email->set_delegate(delegate);
    }

    // Set EmailHandler reference for notifications
    email->set_email_handler(g_instance);

    // Set email address
    if (!target_config.email.empty()) {
        email->set_address(target_config.email);
    }

    // Set provider-specific configuration
    if (provider == oemailim::EmailProvider::EMAIL_PROVIDER_163) {
        auto impl163 = std::dynamic_pointer_cast<EmailComm::EmailOpt163Impl>(delegate);
        if (impl163) {
            if (!target_config.email.empty()) {
                impl163->set_email(target_config.email);
            }
            if (!target_config.auth_code.empty()) {
                impl163->set_auth_code(target_config.auth_code);
            }
            impl163->set_data_dir(dataDir);
            // Set IMAP/SMTP server from config if provided
            if (!target_config.imap_server.empty()) {
                impl163->set_imap_server(target_config.imap_server, target_config.imap_port);
            }
            if (!target_config.smtp_server.empty()) {
                impl163->set_smtp_server(target_config.smtp_server, target_config.smtp_port);
            }
        }
    } else if (provider == oemailim::EmailProvider::EMAIL_PROVIDER_OUTLOOK) {
        auto implOutlook = std::dynamic_pointer_cast<EmailComm::EmailOptOutlookImpl>(delegate);
        if (implOutlook) {
            if (!target_config.email.empty()) {
                implOutlook->set_email(target_config.email);
            }
            if (!target_config.refresh_token.empty()) {
                implOutlook->set_refresh_token(target_config.refresh_token);
            } else if (!target_config.auth_code.empty()) {
                implOutlook->set_refresh_token(target_config.auth_code);
            }
            // Set data directory
            implOutlook->set_data_dir(dataDir);
            // Set IMAP server if provided
            if (!target_config.imap_server.empty()) {
                implOutlook->set_imap_server(target_config.imap_server, target_config.imap_port);
            }
        }
    }

    // Add the email to the global vector
    g_EmailConfigIndices.push_back(email);

    // Return the index in the vector
    int configIndex = static_cast<int>(g_EmailConfigIndices.size()) - 1;
    LOG_INFO("OpenNewEmail: Created email instance with configIndex=%d, provider=%d, delegate type=%s\n",
            configIndex, static_cast<int>(provider), typeid(*delegate).name());
    return configIndex;
}

int EmailHandler::AddOutlookEmail() {
    LOG_INFO("AddOutlookEmail called\n");

    auto email = std::make_shared<EmailComm::EmailOutlook>(
        "smtp.office365.com", 587,
        "outlook.office365.com", 993
    );
    auto delegate = std::make_shared<EmailComm::EmailOptOutlookImpl>(g_instance);

    email->set_delegate(delegate);
    email->set_email_handler(g_instance);

    // Set data directory
    delegate->set_data_dir(dataDir);

    g_EmailConfigIndices.push_back(email);

    int configIndex = static_cast<int>(g_EmailConfigIndices.size()) - 1;
    LOG_INFO("AddOutlookEmail: configIndex=%d\n", configIndex);
    return configIndex;
}

void EmailHandler::loadEmailConfig(const oemail::EmailConfig& config) {
    std::shared_ptr<EmailComm::Email> email;
    std::shared_ptr<EmailComm::EmailOptInterface> delegate;
    oemailim::EmailProvider provider;

    // Determine provider type from config.type
    if (config.type == "163.com" || config.type == "emailType163" ||
        config.type == "qq.com" || config.type == "emailTypeQQ") {
        
        bool isQQ = (config.type == "qq.com" || config.type == "emailTypeQQ");
        std::string defaultSmtp = isQQ ? "smtp.qq.com" : "smtp.163.com";
        std::string defaultImap = isQQ ? "imap.qq.com" : "imap.163.com";
        
        provider = oemailim::EmailProvider::EMAIL_PROVIDER_163;
        email = std::make_shared<EmailComm::Email163>(
            config.smtp_server.empty() ? defaultSmtp : config.smtp_server,
            config.smtp_port > 0 ? config.smtp_port : 465,
            config.imap_server.empty() ? defaultImap : config.imap_server,
            config.imap_port > 0 ? config.imap_port : 993
        );
        delegate = std::make_shared<EmailComm::EmailOpt163Impl>(g_instance);
    } else if (config.type == "outlook.com" || config.type == "emailTypeOutlook") {
        provider = oemailim::EmailProvider::EMAIL_PROVIDER_OUTLOOK;
        email = std::make_shared<EmailComm::EmailOutlook>(
            config.smtp_server.empty() ? "smtp-mail.outlook.com" : config.smtp_server,
            config.smtp_port > 0 ? config.smtp_port : 587,
            config.imap_server.empty() ? "outlook.office365.com" : config.imap_server,
            config.imap_port > 0 ? config.imap_port : 993
        );
        delegate = std::make_shared<EmailComm::EmailOptOutlookImpl>(g_instance);
    } else if (config.type == "gmail.com" || config.type == "emailTypeGmail") {
        provider = oemailim::EmailProvider::EMAIL_PROVIDER_GMAIL;
        email = std::make_shared<EmailComm::EmailGoogle>(
            config.smtp_server.empty() ? "smtp.gmail.com" : config.smtp_server,
            config.smtp_port > 0 ? config.smtp_port : 587,
            config.imap_server.empty() ? "imap.gmail.com" : config.imap_server,
            config.imap_port > 0 ? config.imap_port : 993
        );
        delegate = std::make_shared<EmailComm::EmailOptGmailImpl>(g_instance);
    } else {
        return; // Unknown provider type
    }

    // Set the delegate
    if (delegate) {
        email->set_delegate(delegate);
    }

    // Set EmailHandler reference for notifications
    email->set_email_handler(g_instance);

    // Set email address
    if (!config.email.empty()) {
        email->set_address(config.email);
    }

    // Set provider-specific configuration
    if (provider == oemailim::EmailProvider::EMAIL_PROVIDER_163) {
        auto impl163 = std::dynamic_pointer_cast<EmailComm::EmailOpt163Impl>(delegate);
        if (impl163) {
            if (!config.email.empty()) {
                impl163->set_email(config.email);
            }
            if (!config.auth_code.empty()) {
                impl163->set_auth_code(config.auth_code);
            }
            impl163->set_data_dir(dataDir);
            // Set IMAP/SMTP server from config if provided
            if (!config.imap_server.empty()) {
                impl163->set_imap_server(config.imap_server, config.imap_port);
            }
            if (!config.smtp_server.empty()) {
                impl163->set_smtp_server(config.smtp_server, config.smtp_port);
            }
        }
    } else if (provider == oemailim::EmailProvider::EMAIL_PROVIDER_OUTLOOK) {
        auto implOutlook = std::dynamic_pointer_cast<EmailComm::EmailOptOutlookImpl>(delegate);
        if (implOutlook) {
            if (!config.email.empty()) {
                implOutlook->set_email(config.email);
            }
            // Dart UI saves the refresh token in the authCode field for Outlook accounts
            if (!config.refresh_token.empty()) {
                implOutlook->set_refresh_token(config.refresh_token);
            } else if (!config.auth_code.empty()) {
                implOutlook->set_refresh_token(config.auth_code);
            }
            implOutlook->set_data_dir(dataDir);
        }
    } else if (provider == oemailim::EmailProvider::EMAIL_PROVIDER_GMAIL) {
        auto implGmail = std::dynamic_pointer_cast<EmailComm::EmailOptGmailImpl>(delegate);
        if (implGmail) {
            if (!config.email.empty()) {
                implGmail->set_email(config.email);
            }
            if (!config.refresh_token.empty()) {
                implGmail->set_refresh_token(config.refresh_token);
            } else if (!config.auth_code.empty()) {
                implGmail->set_refresh_token(config.auth_code);
            }
            implGmail->set_data_dir(dataDir);
            if (!config.imap_server.empty()) {
                implGmail->set_imap_server(config.imap_server, config.imap_port);
            }
            if (!config.smtp_server.empty()) {
                implGmail->set_smtp_server(config.smtp_server, config.smtp_port);
            }
        }
    }

    // Add the email to the global vector
    g_EmailConfigIndices.push_back(email);
}

void EmailHandler::notify(EmailComm::Email* email, int message, const std::string& json) {
    // Find the config index for this email
    int configIndex = -1;
    if (email) {
        for (size_t i = 0; i < g_EmailConfigIndices.size(); i++) {
            if (g_EmailConfigIndices[i].get() == email) {
                configIndex = static_cast<int>(i);
                break;
            }
        }
    }

    // Call the global callback if callback is set
    // Note: configIndex may be -1 if email is nullptr (e.g., for browser launch notifications)
    if (g_callback) {
        g_callback(configIndex, message, json);
    }
}

} // namespace oemailim
