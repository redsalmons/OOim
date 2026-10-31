#include "recovery.h"
#include "email_core.h"
#include "email_core_common.h"
#include "keychain_store.h"
#include <openssl/crypto.h>
#include <fstream>
#include <memory>
#include <mutex>
#include <stdexcept>
#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#endif

namespace backup {
namespace {
namespace fs = std::filesystem;
using json = nlohmann::json;
constexpr const char* kStage = ".oim-restore-stage";
constexpr const char* kOld = ".oim-restore-old";
constexpr const char* kJournal = ".oim-restore.json";
void need(bool ok, const char* error) { if (!ok) throw std::runtime_error(error); }
void sql(sqlite3* db, const std::string& q) {
    need(sqlite3_exec(db, q.c_str(), nullptr, nullptr, nullptr) == SQLITE_OK, "recovery_database_error");
}
using Stmt = std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)>;
Stmt statement(sqlite3* db, const char* q) {
    sqlite3_stmt* p = nullptr;
    int rc = sqlite3_prepare_v2(db, q, -1, &p, nullptr);
    Stmt s(p, sqlite3_finalize);
    need(rc == SQLITE_OK, "recovery_database_error");
    return s;
}
struct Database {
    sqlite3* db = nullptr;
    ~Database() { if (db) sqlite3_close(db); }
    void open(const fs::path& path, const std::string& key, bool readOnly) {
        need(key.size() == 64 && key.find_first_not_of("0123456789abcdefABCDEF") == std::string::npos,
             "recovery_master_key_unavailable");
        need(sqlite3_open_v2(path.u8string().c_str(), &db,
            (readOnly ? SQLITE_OPEN_READONLY : SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE) |
            SQLITE_OPEN_FULLMUTEX, nullptr) == SQLITE_OK, "recovery_database_open_failed");
        sql(db, "PRAGMA key=\"x'" + key + "'\"");
        sql(db, "SELECT count(*) FROM sqlite_master");
    }
};
void syncPath(const fs::path& p) {
#ifndef _WIN32
    int fd = ::open(p.c_str(), O_RDONLY);
    need(fd >= 0, "recovery_sync_failed");
    int rc = ::fsync(fd);
    ::close(fd);
    need(rc == 0, "recovery_sync_failed");
#endif
}
void privateDirectory(const fs::path& p) {
    fs::create_directory(p);
    fs::permissions(p, fs::perms::owner_all, fs::perm_options::replace);
}
void writeFile(const fs::path& p, const uint8_t* data, size_t size) {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    need(out.is_open(), "recovery_file_write_failed");
    out.write(reinterpret_cast<const char*>(data), size);
    out.close();
    need(!out.fail(), "recovery_file_write_failed");
    fs::permissions(p, fs::perms::owner_read | fs::perms::owner_write, fs::perm_options::replace);
    syncPath(p);
}
void writeJson(const fs::path& p, const json& value) {
    auto bytes = value.dump();
    writeFile(p, reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size());
}
json readJournal(const fs::path& root) {
    need(fs::file_size(root / kJournal) < 4096, "recovery_invalid_journal");
    std::ifstream in(root / kJournal);
    json j; in >> j;
    need(j.is_object() && j.value("version", 0) == 1 &&
         j.contains("had_data") && j["had_data"].is_boolean() &&
         j.contains("had_config") && j["had_config"].is_boolean(),
         "recovery_invalid_journal");
    return j;
}
void journal(const fs::path& root, const json& j) {
    auto tmp = root / (std::string(kJournal) + ".tmp");
    writeJson(tmp, j);
    fs::rename(tmp, root / kJournal);
    syncPath(root);
}
void move(const fs::path& a, const fs::path& b) {
    fs::rename(a, b);
    syncPath(a.parent_path());
    if (a.parent_path() != b.parent_path()) syncPath(b.parent_path());
}
void rollback(const fs::path& root, const json& j) {
    for (const auto* part : {"data", "config"}) {
        if (fs::exists(root / kOld / part)) {
            fs::remove_all(root / part);
            move(root / kOld / part, root / part);
        } else if (!j.at(std::string("had_") + part).get<bool>() &&
                   !fs::exists(root / kStage / part)) {
            fs::remove_all(root / part);
        }
    }
    fs::remove_all(root / kStage);
    fs::remove_all(root / kOld);
    fs::remove(root / kJournal);
    syncPath(root);
}
} // namespace

void stageRestore(sqlite3* source, const Snapshot& snapshot, const fs::path& root,
                  const std::string& key, const VaultWriter& writeVault) {
    need(source && sqlite3_get_autocommit(source), "recovery_database_busy");
    need(fs::is_directory(root) && !fs::exists(root / kOld) &&
         !fs::exists(root / kJournal), "recovery_already_pending");
    // Without a journal the stage can only be a leftover from a prepare that
    // failed before publication — no committed restore ever references it.
    if (fs::exists(root / kStage)) fs::remove_all(root / kStage);
    privateDirectory(root / kStage);
    try {
        privateDirectory(root / kStage / "data");
        privateDirectory(root / kStage / "config");
        privateDirectory(root / kStage / "data" / "backup-originals");
        {
            Database target;
            target.open(root / kStage / "data" / "emails.db", key, false);
            // Use the schema of the running application, not DDL from the backup.
            auto schemas = statement(source,
                "SELECT sql FROM sqlite_master WHERE type IN ('table','index') "
                "AND sql IS NOT NULL AND name NOT LIKE 'sqlite_%' "
                "ORDER BY CASE type WHEN 'table' THEN 0 ELSE 1 END, name");
            int rc;
            while ((rc = sqlite3_step(schemas.get())) == SQLITE_ROW)
                sql(target.db, reinterpret_cast<const char*>(sqlite3_column_text(schemas.get(), 0)));
            need(rc == SQLITE_DONE, "recovery_schema_read_failed");
            std::map<int64_t, std::string> paths;
            for (const auto& file : snapshot.data.at("originals")) {
                auto id = file.at("transfer_id").get<int64_t>();
                need(id > 0, "recovery_invalid_original");
                auto name = std::to_string(id) + ".bin";
                const auto& bytes = file.at("data").get_binary();
                writeFile(root / kStage / "data" / "backup-originals" / name, bytes.data(), bytes.size());
                paths[id] = (root / "data" / "backup-originals" / name).u8string();
            }
            restoreTables(target.db, snapshot, paths);
            auto check = statement(target.db, "PRAGMA quick_check");
            need(sqlite3_step(check.get()) == SQLITE_ROW &&
                 std::string(reinterpret_cast<const char*>(sqlite3_column_text(check.get(), 0))) == "ok",
                 "recovery_database_check_failed");
        }
        auto config = snapshot.data.at("config");
        config["local_data_path"] = (root / "data").u8string();
        for (auto& account : config["accounts"]) {
            account["auth_code"] = "";
            account["refresh_token"] = "";
        }
        writeJson(root / kStage / "config" / "oim.conf", config);
        writeVault(root / kStage / "config" / "secrets.vault", snapshot.data.at("credentials"));
        syncPath(root / kStage / "config" / "secrets.vault");
        syncPath(root / kStage / "data" / "emails.db");
        syncPath(root / kStage / "data" / "backup-originals");
        syncPath(root / kStage / "data");
        syncPath(root / kStage / "config");
        syncPath(root / kStage);
        journal(root, {{"version", 1}, {"phase", "prepared"},
            {"had_data", fs::exists(root / "data")}, {"had_config", fs::exists(root / "config")}});
    } catch (...) {
        // Preparation never alters active data. Cancel even if the journal
        // rename succeeded but its final fsync failed.
        fs::remove(root / kJournal);
        fs::remove_all(root / kStage);
        fs::remove(root / (std::string(kJournal) + ".tmp"));
        throw;
    }
}

std::string applyPendingRestore(const fs::path& root) {
    if (!fs::exists(root / kJournal)) {
        // A crash before publication cannot have changed active data.
        fs::remove_all(root / kStage);
        return "none";
    }
    auto j = readJournal(root);
    auto phase = j.value("phase", "");
    if (phase == "committed") return "committed";
    if (phase == "replacing") { rollback(root, j); return "rolled_back"; }
    need(phase == "prepared", "recovery_invalid_journal");
    if (!(fs::is_regular_file(root / kStage / "data" / "emails.db") &&
         fs::is_regular_file(root / kStage / "config" / "oim.conf") &&
         fs::is_regular_file(root / kStage / "config" / "secrets.vault"))) {
        rollback(root, j);
        return "rolled_back";
    }
    need(!fs::exists(root / kOld), "recovery_already_pending");
    j["phase"] = "replacing";
    journal(root, j);
    try {
        privateDirectory(root / kOld);
        for (const auto* part : {"data", "config"}) {
            if (fs::exists(root / part)) move(root / part, root / kOld / part);
            move(root / kStage / part, root / part);
        }
        j["phase"] = "committed";
        journal(root, j);
    } catch (...) {
        rollback(root, j);
        throw;
    }
    return "committed";
}

void finishRestore(const fs::path& root) {
    if (!fs::exists(root / kJournal)) return;
    auto j = readJournal(root);
    need(j.value("phase", "") == "committed", "recovery_not_committed");
    fs::remove_all(root / kOld);
    fs::remove_all(root / kStage);
    fs::remove(root / kJournal);
    syncPath(root);
}
} // namespace backup

extern "C" int oim_backup_action(const char* operation, const char* appRoot,
    const char* file, const char* password, char* outJson, int outSize) {
    using json = nlohmann::json;
    namespace fs = std::filesystem;
    if (!operation || !appRoot || !file || !password || !outJson || outSize < 1024) return -1;
    static std::mutex actionMutex;
    std::lock_guard<std::mutex> actionLock(actionMutex);
    json result;
    try {
        std::string op(operation);
        auto root = fs::u8path(appRoot);
        backup::need(root.is_absolute(), "recovery_invalid_root");
        if (op == "startup") {
            // Warm relaunch: the process survived (foreground service, engine
            // re-attach) so g_db is still open. A pending restore cannot be
            // applied while the db is in use — there is nothing to do here.
            if (g_db) {
                result = {{"status", "success"}, {"recovery", "skipped_db_open"}};
            } else {
                result = {{"status", "success"}, {"recovery", backup::applyPendingRestore(root)}};
            }
        } else if (op == "abort") {
            backup::need(!g_db, "recovery_requires_startup");
            if (fs::exists(root / backup::kJournal))
                backup::rollback(root, backup::readJournal(root));
            result = {{"status", "success"}};
        } else if (op == "finish") {
            backup::need(g_db != nullptr, "recovery_database_not_open");
            backup::finishRestore(root);
            result = {{"status", "success"}};
        } else if (op == "preview") {
            auto snapshot = backup::readSnapshot(fs::u8path(file), password);
            auto& data = snapshot.data;
            result = {{"status", "success"}, {"accounts", data["config"]["accounts"].size()},
                {"messages", data["tables"].contains("email_body") ?
                    data["tables"]["email_body"]["rows"].size() : 0},
                {"originals", data["originals"].size()},
                {"missing_originals", data.value("missing_originals", 0)}};
        } else {
            std::lock_guard<std::mutex> databaseLock(g_db_mutex);
            backup::need(g_db != nullptr, "recovery_database_not_open");
            auto dbPath = fs::u8path(sqlite3_db_filename(g_db, "main"));
            std::string key = keychain_store::db_master_key_hex();
            struct KeyWiper { std::string& key; ~KeyWiper() { OPENSSL_cleanse(key.data(), key.size()); } } wiper{key};
            if (op == "export") {
                email_db_close();
                try {
                    backup::Database source;
                    source.open(dbPath, key, true);
                    backup::exportStoppedSnapshot(source.db, root / "config" / "oim.conf",
                                                   fs::u8path(file), password);
                } catch (...) {
                    email_db_init(dbPath.u8string().c_str());
                    throw;
                }
                backup::need(email_db_init(dbPath.u8string().c_str()) == 0, "recovery_reopen_failed");
            } else if (op == "prepare") {
#ifdef _WIN32
                throw std::runtime_error("recovery_windows_import_not_supported");
#else
                auto snapshot = backup::readSnapshot(fs::u8path(file), password);
                backup::stageRestore(g_db, snapshot, root, key,
                    [](const fs::path& path, const json& credentials) {
                        auto values = credentials.get<std::map<std::string, std::string>>();
                        bool ok = keychain_store::stage_snapshot_secrets(path.u8string(), values);
                        for (auto& entry : values) OPENSSL_cleanse(entry.second.data(), entry.second.size());
                        backup::need(ok, "recovery_vault_write_failed");
                    });
#endif
            } else throw std::runtime_error("recovery_unknown_operation");
            result = {{"status", "success"}};
        }
    } catch (const std::exception& e) {
        // Do not include JSON/parser diagnostics, paths, passwords or account data.
        std::string error = e.what();
        if (error.rfind("recovery_", 0) != 0 && error.rfind("snapshot_", 0) != 0 &&
            error.rfind("backup_container_", 0) != 0) error = "recovery_failed";
        if ((std::string(operation) == "prepare" &&
             fs::exists(fs::u8path(appRoot) / backup::kJournal)) ||
            (std::string(operation) == "export" && !g_db))
            error = "recovery_requires_restart";
        result = {{"status", "error"}, {"error", error}};
    }
    auto response = result.dump();
    snprintf(outJson, outSize, "%s", response.c_str());
    return result["status"] == "success" ? 0 : -1;
}
