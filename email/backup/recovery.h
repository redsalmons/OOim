#ifndef OIM_BACKUP_RECOVERY_H
#define OIM_BACKUP_RECOVERY_H
#include "snapshot.h"
#include <functional>

namespace backup {
using VaultWriter = std::function<void(const std::filesystem::path&, const nlohmann::json&)>;
// All application writers must be drained before staging. Schema comes only
// from the installed application's database, never from archive SQL.
void stageRestore(sqlite3* schemaSource, const Snapshot& snapshot,
                  const std::filesystem::path& root, const std::string& keyHex,
                  const VaultWriter& writeVault);
// Startup only, before any DB/delegate/cache initialization.
std::string applyPendingRestore(const std::filesystem::path& root);
void finishRestore(const std::filesystem::path& root);
}

extern "C" int oim_backup_action(const char* operation, const char* appRoot,
    const char* file, const char* password, char* outJson, int outSize);
#endif
