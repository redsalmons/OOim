#ifndef LIB_EMAIL_OUTLOOK_H
#define LIB_EMAIL_OUTLOOK_H

#include "email.h"
#include <string>

namespace EmailComm {

// Outlook email account
class EmailOutlook : public Email {
public:
    EmailOutlook(const std::string& smtp_address = "smtp-mail.outlook.com",
                 int smtp_port = 587,
                 const std::string& imap_address = "outlook.office365.com",
                 int imap_port = 993)
        : Email(smtp_address, smtp_port, imap_address, imap_port) {}

    // Getters for OAuth tokens
    std::string get_refresh_token() const { return refresh_token_; }
    std::string get_token() const { return token_; }

    // Setters for OAuth tokens
    void set_refresh_token(const std::string& token) { refresh_token_ = token; }
    void set_token(const std::string& token) { token_ = token; }

private:
    std::string refresh_token_;
    std::string token_;
};

} // namespace EmailComm

#endif // LIB_EMAIL_OUTLOOK_H
