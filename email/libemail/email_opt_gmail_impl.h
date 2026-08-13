#ifndef LIB_EMAIL_OPT_GMAIL_IMPL_H
#define LIB_EMAIL_OPT_GMAIL_IMPL_H

#include "email_opt_interface.h"
#include <string>
#include <memory>
#include <atomic>
#include <functional>
#include <vmime/vmime.hpp>

namespace EmailComm {

// Gmail OAuth implementation using cpp-http async server
class EmailOptGmailImpl : public EmailOptInterface {
public:
    explicit EmailOptGmailImpl(std::shared_ptr<::oemailim::EmailHandler> email_handler = nullptr);

    ~EmailOptGmailImpl() override;

    // Connect to Gmail IMAP server
    bool connect() override;

    // Ensure connection is established (idempotent)
    bool connect_();

    // Perform OAuth authorization with async HTTP server
    bool authority(int timeout_seconds = 120) override;

    // Get access token
    std::string get_access_token() const override { return access_token_; }

    // Get refresh token
    std::string get_refresh_token() const override { return refresh_token_; }

    // Get email address
    std::string get_email() const override { return email_; }

    // Refresh access token using async HTTP client
    bool refresh_token() override;

    // Check if credentials are valid
    bool is_valid() const override { return is_valid_; }

    // Get last error message
    std::string get_last_error() const override { return last_error_; }

    // Set access token directly (for testing or cached tokens)
    void set_access_token(const std::string& token, const std::string& email = "");

    // Set refresh token directly
    void set_refresh_token(const std::string& token);

    // IMAP operations (stubs - IMAP layer removed)
    bool select_folder(const std::string& folder_name) override;
    std::vector<std::string> fetch_emails_since_uid(const std::string& folder, const std::string& start_uid) override;
    std::string get_email(const std::string& folder, const std::string& uid) override;
    bool send_email(const std::string& folder, const std::string& content) override;

protected:
    bool launch_browser(const std::string& url) override;

private:
    std::string client_id_;
    std::string client_secret_;
    static constexpr const char* GMAIL_OAUTH_CLIENT_ID = "640364233808-4b8j3r9l5k8m1n2o3p4q5r6s7t8u9v0w.apps.googleusercontent.com";
    static constexpr const char* GMAIL_CLIENT_SECRET = "GOCSPX-xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx";
    static constexpr const char* DEFAULT_SCOPE = "https://mail.google.com https://www.googleapis.com/auth/gmail.send";
    static constexpr const char* DEFAULT_REDIRECT_URI = "http://127.0.0.1:9872";
    std::string email_;
    std::string access_token_;
    std::string refresh_token_;
    std::string last_error_;
    bool is_valid_;

    // IMAP layer removed - no longer using ImapOptGmail or ImapOauth
    // std::shared_ptr<oemail::ImapOauth> imap_auth_;
    // std::unique_ptr<oemail::ImapOptGmail> imap_client_;
    // void init_imap_client();

    // PKCE
    std::string code_verifier_;
    std::string code_challenge_;
    std::string state_;
    std::mutex callback_mutex_;

    // vmime connection objects
    vmime::shared_ptr<vmime::net::session> session_;
    vmime::shared_ptr<vmime::net::store> store_;

    // Helper methods
    std::string generate_random_string(size_t length = 32);
    std::string generate_code_verifier();
    std::string generate_code_challenge(const std::string& verifier);
    std::string base64_url_encode(const std::string& input);
    std::string base64_url_encode_bytes(const std::vector<uint8_t>& input);
    std::string base64_url_decode(const std::string& input);
    std::string get_authorization_url(const std::string& redirect_uri,
                                      const std::string& state,
                                      const std::string& code_challenge) const;
    bool exchange_code_for_token(const std::string& code,
                                 const std::string& code_verifier);
    std::string parse_json_field(const std::string& json, const std::string& field);
};

} // namespace EmailComm

#endif // LIB_EMAIL_OPT_GMAIL_IMPL_H
