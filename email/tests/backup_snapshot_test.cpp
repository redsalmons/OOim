// Isolated logical-snapshot tests: no real accounts, g_db, Keychain or network.
#include "backup/snapshot.h"
#include "backup/backup_ffi.h"
#include "backup/recovery.h"
#include <sqlite3.h>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <functional>
#include <memory>
#include <unistd.h>

using json = nlohmann::json;
namespace fs = std::filesystem;

static void check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
static void sql(sqlite3* db, const char* query) {
    check(sqlite3_exec(db, query, nullptr, nullptr, nullptr) == SQLITE_OK, "fixture SQL failed");
}
static int64_t integer(sqlite3* db, const char* query) {
    sqlite3_stmt* stmt = nullptr;
    check(sqlite3_prepare_v2(db, query, -1, &stmt, nullptr) == SQLITE_OK, "query prepare");
    std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> owner(stmt, sqlite3_finalize);
    check(sqlite3_step(stmt) == SQLITE_ROW, "query row");
    return sqlite3_column_int64(stmt, 0);
}
static void rejected(const std::function<void()>& action, const std::string& expected) {
    bool caught = false;
    try { action(); }
    catch (const std::exception& e) {
        caught = true;
        check(std::string(e.what()).find(expected) != std::string::npos, "unexpected rejection");
    }
    check(caught, "expected rejection");
}
struct Database {
    sqlite3* db = nullptr;
    explicit Database(const std::string& path = ":memory:", const char* keySql = nullptr) {
        check(sqlite3_open(path.c_str(), &db) == SQLITE_OK, "open fixture");
        if (keySql) sql(db, keySql);
        // Representative SQLCipher-backed schema: use real storage types and
        // relationships without initializing globals or accessing the Keychain.
        sql(db,
            "CREATE TABLE schema_meta(key TEXT PRIMARY KEY,value TEXT);"
            "INSERT INTO schema_meta VALUES('signal_schema','2');"
            "CREATE TABLE localemail(id INTEGER PRIMARY KEY AUTOINCREMENT,message_id TEXT,in_reply_to TEXT);"
            "CREATE TABLE email_body(email_id INTEGER PRIMARY KEY,body TEXT NOT NULL);"
            "CREATE TABLE session(id INTEGER PRIMARY KEY,email_id INTEGER,session_id TEXT);"
            "CREATE TABLE group_session_email(id INTEGER PRIMARY KEY,account TEXT,group_id INTEGER,email_id INTEGER);"
            "CREATE TABLE unified_session(session_id TEXT PRIMARY KEY,root_message_id TEXT,pinned INTEGER,hidden INTEGER);"
            "CREATE TABLE signal_session(session_id TEXT PRIMARY KEY,root_key TEXT,recv_n INTEGER);"
            "CREATE TABLE signal_skipped_key(id INTEGER PRIMARY KEY,message_key TEXT);"
            "CREATE TABLE mls_state(account TEXT PRIMARY KEY,state BLOB);"
            "CREATE TABLE file_transfer(id INTEGER PRIMARY KEY,file_id TEXT,original_path TEXT);"
            "CREATE TABLE file_chunk(id INTEGER PRIMARY KEY,file_id TEXT,chunk_data TEXT);"
            "CREATE TABLE contact(id INTEGER PRIMARY KEY,email TEXT,notes TEXT,score REAL);"
            "CREATE TABLE task(id INTEGER PRIMARY KEY AUTOINCREMENT,body TEXT);");
    }
    ~Database() { sqlite3_close(db); }
};
struct Temp {
    fs::path path;
    Temp() {
        std::string pattern = (fs::temp_directory_path() / "oim-backup-test-XXXXXX").string();
        check(mkdtemp(pattern.data()) != nullptr, "tempdir");
        path = pattern;
    }
    ~Temp() { std::error_code ec; fs::remove_all(path, ec); }
};
static const std::string password = "snapshot test password";

int main() {
    try {
        Temp temp;
        Database source;
        auto original = temp.path / "source-original.bin";
        { std::ofstream out(original, std::ios::binary); out.write("\0\xff" "abc", 5); }
        // Large integer, NULL, empty BLOB, embedded NUL text, binary MLS state,
        // body identities and row IDs all survive the archive exactly.
        sql(source.db,
            "INSERT INTO localemail VALUES(77,'<body-id>','<parent-body-id>');"
            "INSERT INTO email_body VALUES(77,'{\"x-message-id\":\"<body-id>\",\"x-reply-to\":\"<parent-body-id>\",\"text\":\"历史正文\"}');"
            "INSERT INTO session VALUES(8,77,'signal-local');"
            "INSERT INTO group_session_email VALUES(9,'alice@example.com',3,77);"
            "INSERT INTO unified_session VALUES('unified-local','<parent-body-id>',1,1);"
            "INSERT INTO signal_session VALUES('signal-local','private-root',9007199254740993);"
            "INSERT INTO signal_skipped_key VALUES(11,'private-skipped');"
            "INSERT INTO mls_state VALUES('alice@example.com',X'00FF0100');"
            "INSERT INTO mls_state VALUES('empty@example.com',X'');"
            "INSERT INTO file_chunk VALUES(4,'received','YWJj');"
            "INSERT INTO contact VALUES(1,'bob@example.com',NULL,1.5);"
            "INSERT INTO contact VALUES(2,'nul@example.com',CAST(X'610062' AS TEXT),2.5);"
            "INSERT INTO task VALUES(200,'must never be resent');"
            "UPDATE sqlite_sequence SET seq=1234 WHERE name='localemail';");
        sqlite3_stmt* stmt = nullptr;
        check(sqlite3_prepare_v2(source.db, "INSERT INTO file_transfer VALUES(6,'outgoing',?)",
                                -1, &stmt, nullptr) == SQLITE_OK, "file fixture");
        auto originalStr = original.u8string();
        sqlite3_bind_text(stmt, 1, originalStr.c_str(), -1, SQLITE_TRANSIENT);
        check(sqlite3_step(stmt) == SQLITE_DONE, "insert original");
        sqlite3_finalize(stmt);
        json config = {
            {"local_data_path", "/old-device/data"}, {"username", "Alice"},
            {"accounts", {{{"email", "alice@example.com"}, {"type", "outlook"},
                           {"auth_code", "stale-inline-secret"}, {"refresh_token", "stale-token"}}}},
        };
        json secrets = {{"alice@example.com", "secret-auth"},
                        {"alice@example.com:refresh", "secret-refresh"}};
        auto archive = temp.path / "full.oimbak";
        backup::exportSnapshot(source.db, config, secrets, archive, password);
        auto snapshot = backup::readSnapshot(archive, password);
        check(!snapshot.data["tables"].contains("task"), "tasks excluded");
        check(snapshot.data["credentials"] == secrets, "auth and refresh tokens");
        check(snapshot.data["config"]["accounts"][0]["auth_code"] == "", "inline auth scrubbed");
        check(snapshot.data["config"]["accounts"][0]["refresh_token"] == "", "inline refresh scrubbed");
        check(snapshot.data["config"]["username"] == "Alice", "settings preserved");
        check(snapshot.data["originals"][0]["data"].get_binary().size() == 5, "original bytes captured");

        auto relocated = temp.path / "restored-original.bin";
        const auto& bytes = snapshot.data["originals"][0]["data"].get_binary();
        { std::ofstream out(relocated, std::ios::binary);
          out.write(reinterpret_cast<const char*>(bytes.data()), bytes.size()); }
        Database target;
        backup::restoreTables(target.db, snapshot, {{6, relocated.u8string()}});
        check(integer(target.db, "SELECT count(*) FROM task") == 0, "tasks never restored");
        check(integer(target.db, "SELECT count(*) FROM localemail WHERE id=77 AND message_id='<body-id>' AND in_reply_to='<parent-body-id>'") == 1, "body identity unchanged");
        check(integer(target.db, "SELECT count(*) FROM email_body WHERE email_id=77 AND body LIKE '%历史正文%'") == 1, "history readable");
        check(integer(target.db, "SELECT count(*) FROM session WHERE id=8 AND email_id=77") == 1, "signal association preserved");
        check(integer(target.db, "SELECT count(*) FROM group_session_email WHERE id=9 AND email_id=77 AND group_id=3") == 1, "group association preserved");
        check(integer(target.db, "SELECT count(*) FROM unified_session WHERE pinned=1 AND hidden=1") == 1, "flags preserved");
        check(integer(target.db, "SELECT recv_n FROM signal_session") == 9007199254740993LL, "64-bit integer exact");
        check(integer(target.db, "SELECT count(*) FROM mls_state WHERE hex(state)='00FF0100'") == 1, "MLS blob exact");
        check(integer(target.db, "SELECT count(*) FROM mls_state WHERE typeof(state)='blob' AND length(state)=0") == 1, "empty blob stays blob");
        check(integer(target.db, "SELECT count(*) FROM contact WHERE hex(notes)='610062'") == 1, "embedded NUL text");
        check(integer(target.db, "SELECT count(*) FROM contact WHERE notes IS NULL AND score=1.5") == 1, "null and real");
        check(integer(target.db, "SELECT seq FROM sqlite_sequence WHERE name='localemail'") == 1234, "ID high water preserved");
        check(integer(target.db, "SELECT count(*) FROM sqlite_sequence WHERE name='task'") == 0, "task sequence excluded");
        check(integer(target.db, "SELECT count(*) FROM file_transfer WHERE original_path LIKE '%restored-original.bin'") == 1, "new device path remapped");
        check(integer(source.db, "SELECT count(*) FROM task") == 1, "source tasks untouched");
        check(sqlite3_get_autocommit(source.db) != 0, "source transaction ended");

        // A logical snapshot imports with the NEW device's SQLCipher key.
        auto keyedPath = (temp.path / "new-device.db").u8string();
        {
            Database keyed(keyedPath, "PRAGMA key='isolated-test-device-key'");
            backup::restoreTables(keyed.db, snapshot, {{6, relocated.u8string()}});
        }
        sqlite3* reopened = nullptr;
        check(sqlite3_open(keyedPath.c_str(), &reopened) == SQLITE_OK, "reopen keyed target");
        sql(reopened, "PRAGMA key='isolated-test-device-key'");
        check(integer(reopened, "SELECT count(*) FROM email_body WHERE email_id=77") == 1,
              "new device key opens restored history");
        sqlite3_close(reopened);
        check(sqlite3_open(keyedPath.c_str(), &reopened) == SQLITE_OK, "open without key");
        check(sqlite3_exec(reopened, "SELECT count(*) FROM email_body", nullptr, nullptr, nullptr)
                  != SQLITE_OK, "restored database is encrypted at rest");
        sqlite3_close(reopened);

        rejected([&] { backup::readSnapshot(archive, "wrong password"); }, "backup_container_open:-2");
        rejected([&] { backup::exportSnapshot(source.db, config, secrets, archive, password); },
                 "backup_container_export:-4");
        rejected([&] { backup::restoreTables(target.db, snapshot, {{6, relocated.u8string()}}); },
                 "snapshot_target_not_empty");
        Database empty;
        rejected([&] { backup::restoreTables(empty.db, snapshot, {}); }, "snapshot_paths_incomplete");
        check(integer(empty.db, "SELECT count(*) FROM localemail") == 0, "failed import left target empty");

        // Force a late constraint failure after earlier tables were populated:
        // the entire transaction (including schema_meta/sequence) must roll back.
        auto& contacts = snapshot.data["tables"]["contact"]["rows"];
        contacts.push_back(contacts.front());
        rejected([&] { backup::restoreTables(empty.db, snapshot, {{6, relocated.u8string()}}); },
                 "snapshot_insert_failed");
        check(integer(empty.db, "SELECT count(*) FROM contact") == 0, "failed inserts rolled back");
        check(integer(empty.db, "SELECT count(*) FROM schema_meta") == 1, "seed metadata intact");
        check(sqlite3_get_autocommit(empty.db) != 0, "target transaction ended");
        contacts.erase(contacts.size() - 1);

        Database mismatch;
        sql(mismatch.db, "ALTER TABLE contact ADD COLUMN unexpected TEXT");
        rejected([&] { backup::restoreTables(mismatch.db, snapshot, {{6, relocated.u8string()}}); },
                 "snapshot_schema_mismatch");
        rejected([&] { backup::exportSnapshot(source.db, config, json::object(),
                                              temp.path / "bad.oimbak", password); },
                 "snapshot_credentials_incomplete");
        check(!fs::exists(temp.path / "bad.oimbak"), "no incomplete credential backup");

        // Config adapter with only fixture inline credentials and NO vault:
        // never invokes the system Keychain or creates a master key.
        auto configPath = temp.path / "oim.conf";
        { std::ofstream out(configPath); out << config.dump(); }
        auto fromConfig = temp.path / "config-adapter.oimbak";
        backup::exportStoppedSnapshot(source.db, configPath, fromConfig, password);
        auto configured = backup::readSnapshot(fromConfig, password);
        check(configured.data["credentials"]["alice@example.com"] == "stale-inline-secret",
              "inline auth captured without migration");
        check(configured.data["credentials"]["alice@example.com:refresh"] == "stale-token",
              "inline refresh captured");
        // Invalid magic rejects before key lookup; never silently exports missing secrets.
        { std::ofstream out(temp.path / "secrets.vault"); out << "invalid-vault"; }
        rejected([&] { backup::exportStoppedSnapshot(source.db, configPath,
            temp.path / "bad-vault.oimbak", password); }, "snapshot_credential_store_unreadable");
        check(!fs::exists(temp.path / "bad-vault.oimbak"), "unreadable vault aborts export");

        // Stage/cutover uses only fixtures. A fake vault writer avoids Keychain
        // access; encryption of the real vault stays in keychain_store.
        const auto vaultWriter = [](const fs::path& path, const json&) {
            std::ofstream out(path, std::ios::binary);
            out << "fixture-vault";
        };
        const std::string deviceKey(64, '1');
        auto makeRoot = [&](const std::string& name) {
            auto root = temp.path / name;
            fs::create_directories(root / "data");
            fs::create_directories(root / "config");
            std::ofstream(root / "data" / "original-marker") << "old data";
            std::ofstream(root / "config" / "original-marker") << "old config";
            return root;
        };
        auto root = makeRoot("cutover");
        backup::stageRestore(source.db, snapshot, root, deviceKey, vaultWriter);
        check(fs::exists(root / "data" / "original-marker"), "staging leaves live data untouched");
        check(backup::applyPendingRestore(root) == "committed", "startup applies prepared restore");
        check(fs::exists(root / ".oim-restore-old" / "data" / "original-marker"), "old data retained until initialization");
        check(fs::exists(root / "data" / "backup-originals" / "6.bin"), "original file installed");
        {
            std::ifstream restoredConfig(root / "config" / "oim.conf");
            json cfg; restoredConfig >> cfg;
            check(cfg["local_data_path"] == (root / "data").u8string(), "data path remapped");
            check(cfg["accounts"][0]["auth_code"] == "", "no plaintext config credential");
        }
        check(backup::applyPendingRestore(root) == "committed", "committed restart is idempotent");
        backup::finishRestore(root);
        check(!fs::exists(root / ".oim-restore.json"), "journal removed after successful initialization");
        check(backup::applyPendingRestore(root) == "none", "normal startup after restore");

        // Simulate a crash after each directory rename, including before any.
        // Startup rolls back the entire switch, never a mixture of new/old.
        for (int cut = 0; cut <= 4; ++cut) {
            auto crashRoot = makeRoot("crash-" + std::to_string(cut));
            backup::stageRestore(source.db, snapshot, crashRoot, deviceKey, vaultWriter);
            json journal = {{"version", 1}, {"phase", "replacing"},
                            {"had_data", true}, {"had_config", true}};
            std::ofstream(crashRoot / ".oim-restore.json") << journal.dump();
            fs::create_directory(crashRoot / ".oim-restore-old");
            if (cut >= 1) fs::rename(crashRoot / "data", crashRoot / ".oim-restore-old" / "data");
            if (cut >= 2) fs::rename(crashRoot / ".oim-restore-stage" / "data", crashRoot / "data");
            if (cut >= 3) fs::rename(crashRoot / "config", crashRoot / ".oim-restore-old" / "config");
            if (cut >= 4) fs::rename(crashRoot / ".oim-restore-stage" / "config", crashRoot / "config");
            check(backup::applyPendingRestore(crashRoot) == "rolled_back", "interrupted cutover rolls back");
            check(fs::exists(crashRoot / "data" / "original-marker") &&
                  fs::exists(crashRoot / "config" / "original-marker"), "both original directories restored");
            check(backup::applyPendingRestore(crashRoot) == "none", "rollback restart idempotent");
        }
        auto failedRoot = makeRoot("failed-stage");
        rejected([&] {
            backup::stageRestore(source.db, snapshot, failedRoot, deviceKey,
                [](const fs::path&, const json&) { throw std::runtime_error("fixture-vault-failure"); });
        }, "fixture-vault-failure");
        check(!fs::exists(failedRoot / ".oim-restore.json") &&
              !fs::exists(failedRoot / ".oim-restore-stage") &&
              fs::exists(failedRoot / "data" / "original-marker"), "failed staging does not publish or alter live data");

        fs::remove(original);
        // Missing originals degrade instead of failing: the transfer row is
        // kept with an empty original_path and counted in missing_originals.
        backup::exportSnapshot(source.db, config, secrets,
                               temp.path / "missing.oimbak", password);
        {
            auto degraded = backup::readSnapshot(temp.path / "missing.oimbak", password);
            check(degraded.data["originals"].empty(), "missing file excluded from originals");
            check(degraded.data.value("missing_originals", 0) == 1, "missing original counted");
        }

        // C ABI rejects invalid inputs and clears outputs on authentication failure.
        uint8_t* output = reinterpret_cast<uint8_t*>(1);
        size_t outputLen = 99;
        check(oim_backup_open_file(nullptr, nullptr, 0, &output, &outputLen) == -1 &&
              output == nullptr && outputLen == 0, "FFI output cleared");
        check(oim_backup_seal_file(nullptr, nullptr, 0, nullptr, 0) == -1, "FFI invalid path");
        std::cout << "backup snapshot roundtrip and failure checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
