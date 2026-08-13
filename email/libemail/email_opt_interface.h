#ifndef LIB_EMAIL_OPT_INTERFACE_H
#define LIB_EMAIL_OPT_INTERFACE_H

#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <nlohmann/json.hpp>
#include <vmime/vmime.hpp>
#include <vmime/platforms/posix/posixHandler.hpp>
#include <vmime/security/sasl/XOAuth2SASLMechanism.hpp>
#include <cstdio>
#ifdef _WIN32
    #include <windows.h>
    #include <shellapi.h>
#endif

// Forward declaration
namespace oemailim {
class EmailHandler;
}

namespace EmailComm {

// Interface for email operations (OAuth authentication and connection)
class EmailOptInterface {
public:
    // Constructor with email_handler reference
    explicit EmailOptInterface(std::shared_ptr<oemailim::EmailHandler> email_handler = nullptr)
        : email_handler_(email_handler) {}

    virtual ~EmailOptInterface() = default;

    // Connect to the email service
    virtual bool connect() = 0;

    // Perform OAuth authorization
    virtual bool authority(int timeout_seconds = 120) = 0;

    // Get access token
    virtual std::string get_access_token() const = 0;

    // Get refresh token
    virtual std::string get_refresh_token() const = 0;

    // Get email address
    virtual std::string get_email() const = 0;

    // Refresh access token using refresh token
    virtual bool refresh_token() = 0;

    // Check if credentials are valid
    virtual bool is_valid() const = 0;

    // Get last error message
    virtual std::string get_last_error() const = 0;

    // IMAP operations
    // Select a mail folder
    virtual bool select_folder(const std::string& folder_name) = 0;

    // Fetch UIDs of emails since a given UID (e.g., get all emails with UID > 8)
    virtual std::vector<std::string> fetch_emails_since_uid(const std::string& folder, const std::string& start_uid) = 0;

    // Get email content by UID
    virtual std::string get_email(const std::string& folder, const std::string& uid) = 0;

    // Mark email as read or unread
    virtual bool mark_email(const std::string& folder, const std::string& uid, bool read) { return false; }

    // Delete an email
    virtual bool delete_email(const std::string& folder, const std::string& uid) { return false; }

    // Send email with content to a folder
    virtual bool send_email(const std::string& folder, const std::string& content) = 0;

    // Fetch email headers as JSON
    virtual std::string fetch_email_headers(const std::string& folder, const std::string& start_uid) = 0;

    // Wait for new emails using IMAP IDLE
    // Returns true if new emails arrived, false on timeout or error
    virtual bool idle_wait(const std::string& folder, int timeout_seconds = 300) { return false; }

    // Discover the Sent folder name via IMAP SPECIAL-USE or name matching
    virtual std::string find_sent_folder() { return "Sent"; }

protected:
    // Email handler reference for notifications
    std::shared_ptr<oemailim::EmailHandler> email_handler_;

    // Get system CA path for SSL certificate verification
    static std::string getSystemCAPath() {
#if defined(__linux__)
        // Priority: Ubuntu/Debian path, fallback to CentOS path
        if (FILE *f = fopen("/etc/ssl/certs/ca-certificates.crt", "r")) {
            fclose(f);
            return "/etc/ssl/certs/ca-certificates.crt";
        }
        return "/etc/pki/tls/certs/ca-bundle.crt";

#elif defined(__APPLE__) && defined(__MACH__)
        // Mac M1/M2/M3 Homebrew path
        if (FILE *f = fopen("/opt/homebrew/etc/openssl@3/cert.pem", "r")) {
            fclose(f);
            return "/opt/homebrew/etc/openssl@3/cert.pem";
        }
        // Mac Intel Homebrew path
        return "/usr/local/etc/openssl@3/cert.pem";

#elif defined(_WIN32) || defined(_WIN64)
        // Windows: place cacert.pem in exe directory
        return "ca-bundle.crt";
#else
        return "";
#endif
    }

    // Initialize vmime platform handler (call once)
    static void init_vmime_platform() {
        static bool platform_initialized = false;
        if (!platform_initialized) {
            vmime::platform::setHandler<vmime::platforms::posix::posixHandler>();
            
            // Register XOAUTH2 mechanism for OAuth2 authentication
            vmime::security::sasl::SASLMechanismFactory::getInstance()->
                registerMechanism<vmime::security::sasl::XOAuth2SASLMechanism>("XOAUTH2");
            
            platform_initialized = true;
        }
    }

    // Browser launcher - implemented in derived classes to avoid circular dependency
    virtual bool launch_browser(const std::string& url) = 0;
};

// Simple stub implementation for 163 (no OAuth needed)
// Used when ImapOpt163 dependency is not available (gmime dependency)
class EmailOpt163Stub : public EmailOptInterface {
public:
    EmailOpt163Stub() : is_valid_(false) {}
    ~EmailOpt163Stub() override = default;

    bool connect() override { return true; }
    bool authority(int timeout_seconds = 120) override { return true; }
    std::string get_access_token() const override { return ""; }
    std::string get_refresh_token() const override { return ""; }
    std::string get_email() const override { return ""; }
    bool refresh_token() override { return false; }
    bool is_valid() const override { return is_valid_; }
    std::string get_last_error() const override { return ""; }

    bool select_folder(const std::string& folder_name) override { return true; }
    std::vector<std::string> fetch_emails_since_uid(const std::string& folder, const std::string& start_uid) override { return {}; }
    std::string get_email(const std::string& folder, const std::string& uid) override { return ""; }
    bool send_email(const std::string& folder, const std::string& content) override { return true; }
    std::string fetch_email_headers(const std::string& folder, const std::string& start_uid) override { return ""; }

private:
    bool is_valid_;
};

} // namespace EmailComm

#endif // LIB_EMAIL_OPT_INTERFACE_H
