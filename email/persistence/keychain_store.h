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
// On Android this returns the value set by inject_db_master_key_hex().
std::string db_master_key_hex();

// Android only: the Kotlin side generates the master key and wraps it with
// AndroidKeyStore; the plaintext hex is injected here once per process,
// before any database/vault access.
void inject_db_master_key_hex(const char* hex);

// Sets the path of the encrypted credential vault file (AES-256-GCM over a
// JSON map, keyed by the master key). Secrets are stored there, not as
// individual Keychain items — only the master key touches the Keychain.
void set_vault_path(const std::string& path);

bool store_secret(const std::string& account, const std::string& secret);
std::string load_secret(const std::string& account);
bool delete_secret(const std::string& account);

} // namespace keychain_store

#endif // PERSISTENCE_KEYCHAIN_STORE_H
