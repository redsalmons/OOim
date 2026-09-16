#ifndef PERSISTENCE_MLS_REPO_H
#define PERSISTENCE_MLS_REPO_H

#include <string>
#include <vector>
#include <cstdint>

// Persistence for the MLS (1:n group) protocol state.
//   mls_state      : opaque OpenMLS state blob per local account (identity + all groups)
//   group_session  : mls_group_id column maps the local group_id to the MLS GroupId (hex)
class MlsRepo {
public:
    bool saveState(const std::string& account, const std::vector<uint8_t>& blob);
    bool loadState(const std::string& account, std::vector<uint8_t>& out);

    bool setMlsGroupId(const std::string& groupId, const std::string& mlsGroupIdHex);
    std::string getMlsGroupId(const std::string& groupId);
    // Reverse lookup: local group_id for (account, mls_group_id). Empty if none.
    std::string findGroupIdByMlsGroupId(const std::string& account, const std::string& mlsGroupIdHex);
};

#endif // PERSISTENCE_MLS_REPO_H
