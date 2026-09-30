#ifndef PERSISTENCE_EMAIL_BODY_REPO_H
#define PERSISTENCE_EMAIL_BODY_REPO_H

#include <string>
#include <cstdint>

// Processed message bodies (rewritten/decrypted EML text) live here instead
// of on-disk .eml files. Keyed by localemail.id (rowid), which is stable for
// both received and sent (uuid="0" pending) rows.
class EmailBodyRepo {
public:
    bool upsert(int64_t emailId, const std::string& body);
    std::string get(int64_t emailId);
    bool remove(int64_t emailId);
    bool removeByAccount(const std::string& account);
};

#endif // PERSISTENCE_EMAIL_BODY_REPO_H
