#ifndef OIM_MLS_GROUP_H
#define OIM_MLS_GROUP_H

// C++ facade over the Rust OpenMLS FFI (mls_ffi.h) for 1:n group sessions.
// Handles per-account state loading/persistence (mls_state table) and maps
// local group_id <-> MLS GroupId. All binary payloads are exchanged as base64.
//
// This is the ONLY entry point for group cryptography. It never touches the
// 1:1 Double Ratchet code.

#include <string>
#include <vector>

namespace mls {

// Ensure the account's MLS identity is loaded into the Rust runtime (from DB or fresh).
bool ensureAccount(const std::string& account);

// Fresh KeyPackage for this account (base64 of TLS bytes).
std::string generateKeyPackage(const std::string& account);

// Create a new MLS group for `account` and bind it to local `groupId`. Returns hex MLS GroupId.
std::string createGroup(const std::string& account, const std::string& groupId);

struct AddResult {
    bool ok = false;
    std::string welcomeB64;   // for the new members
    std::string commitB64;    // for existing members
    std::string treeB64;      // ratchet tree after the commit (ship with the Welcome)
    std::string error;
};
// Add members (base64 KeyPackages) to the group bound to local `groupId`.
AddResult addMembers(const std::string& account, const std::string& groupId,
                     const std::vector<std::string>& keyPackagesB64);

// Join from Welcome + tree; binds the resulting MLS group to local `groupId`. Returns hex MLS GroupId.
std::string joinGroup(const std::string& account, const std::string& groupId,
                      const std::string& welcomeB64, const std::string& treeB64);

// Application messages. Ciphertext is base64 of the TLS MlsMessage.
bool encrypt(const std::string& account, const std::string& groupId, const std::string& plaintext, std::string& outCipherB64);
bool decrypt(const std::string& account, const std::string& groupId, const std::string& cipherB64, std::string& outPlaintext);

bool processCommit(const std::string& account, const std::string& groupId, const std::string& commitB64);

// Members currently in the MLS tree (identities = emails). Empty if group not joined yet.
std::vector<std::string> members(const std::string& account, const std::string& groupId);

// A group is "ready" when the account has a joined MLS group for it.
bool isReady(const std::string& account, const std::string& groupId);

int epoch(const std::string& account, const std::string& groupId);

} // namespace mls

#endif // OIM_MLS_GROUP_H
