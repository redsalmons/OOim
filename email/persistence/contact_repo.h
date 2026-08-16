#ifndef PERSISTENCE_CONTACT_REPO_H
#define PERSISTENCE_CONTACT_REPO_H

#include <string>
#include <vector>

struct ContactRecord {
    int64_t id = 0;
    std::string email;
    std::string name;
    std::string categories;
    std::string notes;
    std::string key;
};

class ContactRepo {
public:
    bool add(const std::string& email, const std::string& name,
        const std::string& categories, const std::string& notes,
        const std::string& key);

    std::vector<ContactRecord> queryAll();

    bool remove(int64_t id);
};

#endif // PERSISTENCE_CONTACT_REPO_H
