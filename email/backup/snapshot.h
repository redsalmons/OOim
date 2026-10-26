#ifndef OIM_BACKUP_SNAPSHOT_H
#define OIM_BACKUP_SNAPSHOT_H

#include <sqlite3.h>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <map>
#include <string>

namespace backup {

// Internal snapshot layer; never point this at a running email_core instance.
// Caller must drain all protocol/network workers and freeze configuration,
// credentials and original files before capture. A SQLite transaction alone
// cannot make a multi-step Signal/MLS operation consistent.
//
// config: parsed oim.conf (all settings preserved).
// credentials: map containing each configured email and email+":refresh".
// Empty string means explicitly absent; a failed vault read must not be supplied
// as an empty credential map. No system Keychain or source DB key is modified.
void exportSnapshot(sqlite3* source, const nlohmann::json& config,
                    const nlohmann::json& credentials,
                    const std::filesystem::path& destination,
                    const std::string& password);

// Config/vault adapter for a stopped core. Rejects use while the global live
// database is open. source is a separately opened, keyed source connection.
void exportStoppedSnapshot(sqlite3* source, const std::filesystem::path& configPath,
                           const std::filesystem::path& destination,
                           const std::string& password);

// Owns sensitive decoded snapshot data. No copying/logging.
struct Snapshot {
    nlohmann::json data;
    Snapshot() = default;
    ~Snapshot();
    Snapshot(const Snapshot&) = delete;
    Snapshot& operator=(const Snapshot&) = delete;
    Snapshot(Snapshot&& other) noexcept;
    Snapshot& operator=(Snapshot&& other) noexcept;
};

// Authenticates/decrypts and validates structural content; does not mutate a DB.
Snapshot readSnapshot(const std::filesystem::path& source, const std::string& password);

// Populate an isolated, already-schema-initialized EMPTY database. The target
// may use its own SQLCipher key. All rows are restored in one transaction, with
// no SQL/schema from the archive executed. No live activation or protocol replay.
// External original-file paths are replaced by the supplied map (transfer row
// id -> caller-staged local path). Caller stages file bytes from data["originals"]
// and commits configuration/vault only during the later application cutover.
void restoreTables(sqlite3* emptyTarget, const Snapshot& snapshot,
                   const std::map<int64_t, std::string>& originalPaths);

} // namespace backup
#endif
