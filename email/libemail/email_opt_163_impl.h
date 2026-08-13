#ifndef LIB_EMAIL_OPT_163_IMPL_H
#define LIB_EMAIL_OPT_163_IMPL_H

#include "email_opt_interface.h"
#include <string>
#include <memory>
#include <vmime/vmime.hpp>

namespace EmailComm {

// 163 email implementation (using password/authorization code)
class EmailOpt163Impl : public EmailOptInterface {
public:
    explicit EmailOpt163Impl(std::shared_ptr<::oemailim::EmailHandler> email_handler = nullptr);

    ~EmailOpt163Impl() override;

    // Connect to 163 IMAP server
    bool connect() override;

    // Ensure connection is established (idempotent)
    bool connect_();

    // Perform authorization (163 uses password or auth code)
    bool authority(int timeout_seconds = 120) override;

    // Get access token (163 may not use OAuth tokens)
    std::string get_access_token() const override { return auth_code_; }

    // Get refresh token (163 may not use refresh tokens)
    std::string get_refresh_token() const override { return ""; }

    // Get email address
    std::string get_email() const override { return email_; }

    // Refresh token (not applicable for 163)
    bool refresh_token() override { return false; }

    // Check if credentials are valid
    bool is_valid() const override { return is_valid_; }

    // Get last error message
    std::string get_last_error() const override { return last_error_; }

    // Set authorization code
    void set_auth_code(const std::string& code);

    // Set email address
    void set_email(const std::string& email);
    
    // Set SMTP server and port
    void set_smtp_server(const std::string& server, int port);

    // Set data directory for email storage
    void set_data_dir(const std::string& dir) { data_dir_ = dir; }
    std::string get_data_dir() const { return data_dir_; }

    // IMAP operations (stubs - require gmime dependency for full implementation)
    bool select_folder(const std::string& folder_name) override;
    std::vector<std::string> fetch_emails_since_uid(const std::string& folder, const std::string& start_uid) override;
    std::string get_email(const std::string& folder, const std::string& uid) override;
    bool send_email(const std::string& folder, const std::string& content) override;

    // Fetch email headers (uuid, from, sender, subject, date, bodystructure) as JSON array
    std::string fetch_email_headers(const std::string& folder, const std::string& start_uid);

    // Enter IMAP IDLE mode on the selected folder, block until server sends
    // an untagged response (e.g. EXISTS). Returns true if new mail may have
    // arrived, false on error/timeout.
    bool idle_wait(const std::string& folder, int timeout_seconds = 300) override;

    // Discover the Sent folder name
    std::string find_sent_folder() override;

protected:
    bool launch_browser(const std::string& url) override;

private:
    std::string email_;
    std::string auth_code_;
    std::string last_error_;
    bool is_valid_;
    
    // SMTP server configuration
    std::string smtp_server_;
    int smtp_port_;

    // Data directory for email storage
    std::string data_dir_;

    // vmime connection objects (each instance has its own connection)
    vmime::shared_ptr<vmime::net::session> session_;
    vmime::shared_ptr<vmime::net::store> store_;
    vmime::shared_ptr<vmime::net::folder> folder_;

    // Instance-specific tracking of currently selected folder
    std::string current_selected_folder_;
};

} // namespace EmailComm

#endif // LIB_EMAIL_OPT_163_IMPL_H
