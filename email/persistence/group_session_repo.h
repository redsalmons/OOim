#ifndef PERSISTENCE_GROUP_SESSION_REPO_H
#define PERSISTENCE_GROUP_SESSION_REPO_H

#include <string>
#include <cstdint>
#include <vector>

// Group session record
struct GroupSessionRecord {
    std::string groupId;
    std::string groupEmail;
    std::string xReplyId;
    std::string subject;
    std::vector<std::string> members;  // member email list
    std::string owner;                // creator account
    std::string account;              // local account that owns this session row
    int epoch = 0;
    int status = 0;   // 0=active, 1=closed
    std::string createdAt;
    std::string updatedAt;
};

class GroupSessionRepo {
public:
    // Create a new group session; updates rec.groupId and rec.groupEmail
    bool createGroup(GroupSessionRecord& rec);

    // Load group session by groupId
    bool loadGroup(const std::string& groupId, GroupSessionRecord& out);

    // Load group session by xReplyId and local account
    bool loadByXReplyId(const std::string& xReplyId, const std::string& account, GroupSessionRecord& out);

    // Set xReplyId for an existing group
    bool setXReplyId(const std::string& groupId, const std::string& xReplyId);

    // Update members list
    bool updateMembers(const std::string& groupId, const std::vector<std::string>& members);

    // Add a single member
    bool addMember(const std::string& groupId, const std::string& memberEmail);

    // Remove a single member
    bool removeMember(const std::string& groupId, const std::string& memberEmail);

    // Increment epoch
    bool incrementEpoch(const std::string& groupId);

    // Close group
    bool closeGroup(const std::string& groupId);

    // List all active groups for an account (member must be in members list)
    std::vector<GroupSessionRecord> listGroups(const std::string& accountEmail);

    // Check if a member is in the group
    bool isMember(const std::string& groupId, const std::string& memberEmail);
};

#endif // PERSISTENCE_GROUP_SESSION_REPO_H
