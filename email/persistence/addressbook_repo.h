#ifndef PERSISTENCE_ADDRESSBOOK_REPO_H
#define PERSISTENCE_ADDRESSBOOK_REPO_H

#include <string>
#include <vector>

struct AddressBookRecord {
    int64_t id = 0;
    std::string email;
    std::string name;
    std::string groupName;
    std::string notes;
    std::string createdAt;
    std::string updatedAt;
};

class AddressBookRepo {
public:
    // Insert email if not already present. Returns true if inserted, false if already exists.
    bool insertIfNotExists(const std::string& email, const std::string& name = "");

    // Query all addressbook entries ordered by name.
    std::vector<AddressBookRecord> queryAll();

    // Query by id. Returns true if found.
    bool queryById(int64_t id, AddressBookRecord& out);

    // Update name, group, notes by id.
    bool update(int64_t id, const std::string& name, const std::string& groupName, const std::string& notes);

    // Delete by id.
    bool remove(int64_t id);

    // Query all distinct group names.
    std::vector<std::string> queryGroups();
};

#endif // PERSISTENCE_ADDRESSBOOK_REPO_H
