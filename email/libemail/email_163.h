#ifndef LIB_EMAIL_163_H
#define LIB_EMAIL_163_H

#include "email.h"
#include <string>

namespace EmailComm {

// 163 email account
class Email163 : public Email {
public:
    Email163(const std::string& smtp_address = "smtp.163.com",
             int smtp_port = 465,
             const std::string& imap_address = "imap.163.com",
             int imap_port = 993)
        : Email(smtp_address, smtp_port, imap_address, imap_port) {}

    // Getter for authorization code
    std::string get_auth_code() const { return auth_code_; }

    // Setter for authorization code
    void set_auth_code(const std::string& code) { auth_code_ = code; }

private:
    std::string auth_code_;
};

} // namespace EmailComm

#endif // LIB_EMAIL_163_H
