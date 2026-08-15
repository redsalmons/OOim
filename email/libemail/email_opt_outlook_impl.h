#ifndef LIB_EMAIL_OPT_OUTLOOK_IMPL_H
#define LIB_EMAIL_OPT_OUTLOOK_IMPL_H

#include "email_opt_interface.h"
#include <string>
#include <memory>
#include <atomic>
#include <functional>
#include <vmime/vmime.hpp>

namespace EmailComm {

// Outlook OAuth implementation using cpp-http async server
class EmailOptOutlookImpl : public EmailOptInterface {
public:
    explicit EmailOptOutlookImpl(std::shared_ptr<::oemailim::EmailHandler> email_handler = nullptr);

    ~EmailOptOutlookImpl() override;

    // Connect to Outlook IMAP server
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
    
    // Set email address
    void set_email(const std::string& email) { email_ = email; }

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

    // Set IMAP server and port
    void set_imap_server(const std::string& server, int port);

    // Set SMTP server and port
    void set_smtp_server(const std::string& server, int port);
    
    // Set account type: "personal" or "enterprise"
    void set_account_type(const std::string& type);
    
    // Get account type
    std::string get_account_type() const;
    
    // Set data directory
    void set_data_dir(const std::string& dir);
    
    // Get data directory
    std::string get_data_dir() const;

    // IMAP operations
    bool select_folder(const std::string& folder_name) override;
    std::vector<std::string> fetch_emails_since_uid(const std::string& folder, const std::string& start_uid) override;
    std::string get_email(const std::string& folder, const std::string& uid) override;
    bool send_email(const std::string& folder, const std::string& content) override;
    std::string fetch_email_headers(const std::string& folder, const std::string& start_uid) override;

    // Wait for new emails using IMAP IDLE
    bool idle_wait(const std::string& folder, int timeout_seconds = 300) override;

    // Discover the Sent folder name via IMAP SPECIAL-USE or name matching
    std::string find_sent_folder();

protected:
    bool launch_browser(const std::string& url) override;

private:
    std::shared_ptr<::oemailim::EmailHandler> email_handler_;
    std::string client_id_;
    static constexpr const char* DEFAULT_CLIENT_ID = "6050c77b-bfc7-47fa-8ae3-d9dcae5b29bc";
    static constexpr const char* DEFAULT_TENANT_ID = "common";
    // Consent scope for initial authorization (all requested permissions)
    static constexpr const char* DEFAULT_SCOPE = "openid email https://outlook.office.com/IMAP.AccessAsUser.All https://outlook.office.com/SMTP.Send https://graph.microsoft.com/Mail.Send offline_access";
    // Scope for Outlook IMAP/SMTP token refresh
    static constexpr const char* OUTLOOK_SCOPE = "openid email https://outlook.office.com/IMAP.AccessAsUser.All https://outlook.office.com/SMTP.Send offline_access";
    // Scope for Microsoft Graph sendMail token
    static constexpr const char* GRAPH_SCOPE = "https://graph.microsoft.com/Mail.Send";
    static constexpr const char* DEFAULT_REDIRECT_URI = "http://localhost:9871";
    std::string email_;
    std::string access_token_;
    std::string graph_access_token_;
    std::string refresh_token_;
    std::string last_error_;
    bool is_valid_;
    std::string imap_server_;
    int imap_port_;
    std::string smtp_server_;
    int smtp_port_;
    std::string data_dir_;
    
    // Account type: "personal" or "enterprise"
    std::string account_type_;

    // IMAP layer removed - no longer using ImapOptOutlook or ImapOauth
    // std::shared_ptr<oemail::ImapOauth> imap_auth_;
    // std::unique_ptr<oemail::ImapOptOutlook> imap_client_;
    // void init_imap_client();

    // PKCE
    std::string code_verifier_;
    std::string code_challenge_;
    std::string state_;

    // vmime connection objects
    vmime::shared_ptr<vmime::net::session> session_;
    vmime::shared_ptr<vmime::net::store> store_;

    // Helper methods
    std::string generate_random_string(size_t length = 32);
    std::string generate_code_verifier();
    std::string generate_code_challenge(const std::string& verifier);
    std::string base64_url_encode(const std::string& input);
    std::string base64_url_encode_bytes(const std::vector<uint8_t>& input);
    std::string base64_encode_bytes(const std::vector<uint8_t>& input);
    std::string wrap_base64_lines(const std::string& b64, size_t line_len = 76);
    std::string base64_url_decode(const std::string& input);
    std::string url_encode(const std::string& value);
    std::string get_authorization_url(const std::string& redirect_uri,
                                      const std::string& state,
                                      const std::string& code_challenge) const;
    bool exchange_code_for_token(const std::string& code,
                                 const std::string& redirect_uri,
                                 const std::string& code_verifier);
    bool refresh_graph_token();
    std::string parse_json_field(const std::string& json, const std::string& field);
    
    // Helper methods for sending email
    bool send_email_via_graph_api(const std::string& recipient, const std::string& subject, 
                                   const std::string& body, const std::string& in_reply_to, 
                                   const std::string& message_id, const std::string& session_id,
                                   const std::string& x_message_id = "", const std::string& x_start_new = "");
    bool send_email_via_vmime_smtp(const std::string& recipient, const std::string& subject, 
                                     const std::string& body, const std::string& in_reply_to, 
                                     const std::string& message_id, const std::string& session_id,
                                     const std::string& x_message_id = "", const std::string& x_start_new = "");
};

} // namespace EmailComm

#endif // LIB_EMAIL_OPT_OUTLOOK_IMPL_H
