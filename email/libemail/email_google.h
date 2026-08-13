#ifndef LIB_EMAIL_GOOGLE_H
#define LIB_EMAIL_GOOGLE_H

#include "email.h"
#include <string>

namespace EmailComm {

// Google Gmail email account
class EmailGoogle : public Email {
public:
    EmailGoogle(const std::string& smtp_address = "smtp.gmail.com",
                int smtp_port = 587,
                const std::string& imap_address = "imap.gmail.com",
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

#endif // LIB_EMAIL_GOOGLE_H
