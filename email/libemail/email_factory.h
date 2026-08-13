#ifndef EMAIL_FACTORY_H
#define EMAIL_FACTORY_H

#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include <map>
#include <nlohmann/json.hpp>
// #include "imap_opt_interface.h"  // Deleted - IMAP layer removed
#include "sync_session.h"

namespace oemail {

// class ImapOauth;  // Deleted - IMAP layer removed

// Email content structure containing metadata and data
struct EmailContent {
    std::vector<uint8_t> metadata;  // metadata.json content (raw bytes)
    nlohmann::json metadata_json;   // metadata.json parsed as JSON object
    std::vector<uint8_t> data;      // data.bin content
    std::string message_id;
    std::string subject;
    std::string from;
    std::string date;
};

class EmailFactory {
public:
    EmailFactory();
    ~EmailFactory();
    
    // Set email configuration with email type (e.g., "163", "163.com", "gmail", etc.)
    void set_config(const std::string& email, const std::string& auth_credential, const std::string& email_type);
    
    // Set master password for decrypting refresh tokens
    void set_master_password(const std::string& master_password);
    
    // Set sync session for state management
    void set_session(session_t* session);
    
    // Create IMAP client with proper OAuth handling
    // Returns tuple of (client, extracted_email, auth_object)
    // std::tuple<std::unique_ptr<ImapOptInterface>, std::string, std::shared_ptr<ImapOauth>> create_imap_client(
    //     const std::string& email,
    //     const std::string& email_type,
    //     const std::string& encrypted_refresh_token,
    //     const std::string& tenant_id = "common",
    //     const std::string& auth_code = "");

    
    // Trigger OAuth flow for Outlook (returns refresh_token on success)
    std::string trigger_oauth_outlook(const std::string& email, const std::string& redirect_uri = "http://localhost:9871");
    
    // Trigger OAuth flow for Gmail (returns refresh_token on success)
    std::string trigger_oauth_gmail(const std::string& email, const std::string& redirect_uri = "http://localhost:9871");
    
    // Set encryption key
    void set_encryption_key(const std::string& key);
    
    // Fetch email by UID and parse content
    // Returns true if successful, false otherwise
    bool fetch_email(const std::string& folder, const std::string& uid, EmailContent& content);
    
    // Fetch only message_id by UID without decryption
    // Returns message_id if successful, empty string otherwise
    std::string fetch_message_id(const std::string& folder, const std::string& uid);
    
    // Send email with metadata and data
    // Returns true if successful, false otherwise
    bool send_email(const std::string& folder, 
                    const std::string& message_id,
                    const std::string& subject,
                    const std::vector<uint8_t>& metadata,
                    const std::vector<uint8_t>& data,
                    const std::string& reply_to = "");
    
    // Set metadata content (for sending)
    void set_metadata(const std::vector<uint8_t>& metadata);
    
    // Set data content (for sending)
    void set_data(const std::vector<uint8_t>& data);
    
    // Get last error message
    std::string get_last_error() const;
    
private:
    class Impl;
    std::unique_ptr<Impl> p_impl;
};

} // namespace oemail

#endif // EMAIL_FACTORY_H
