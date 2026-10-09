#ifndef PERSISTENCE_KEYCHAIN_STORE_H
#define PERSISTENCE_KEYCHAIN_STORE_H

#include <string>
#include <vector>

// OS-backed secret storage. Two item families:
//   service "com.redsalmon.oim.db"         — SQLCipher master key (one item)
//   service "com.redsalmon.oim.credentials"— per-account secrets (auth codes,
//                                            OAuth tokens), keyed by email
// macOS:   Keychain generic-password items, ThisDeviceOnly (no iCloud sync).
// Windows: Credential Manager generic credentials ("OceanTalk:<service>:<account>"),
//          per-user vault bound to the current logon session; legacy DPAPI blob
//          files are migrated into the vault on first access.

namespace keychain_store {

// Returns the 32-byte SQLCipher master key as a 64-char hex string,
// generating and persisting it on first use. Empty on failure.
std::string db_master_key_hex();

bool store_secret(const std::string& account, const std::string& secret);
std::string load_secret(const std::string& account);
bool delete_secret(const std::string& account);

} // namespace keychain_store

#endif // PERSISTENCE_KEYCHAIN_STORE_H
