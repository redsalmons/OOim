#include "snapshot.h"
#include "backup_ffi.h"
#include "email_core_common.h"
#include "keychain_store.h"
#include <openssl/crypto.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <memory>
#include <set>
#include <stdexcept>

namespace backup {
namespace {
using json = nlohmann::json;
constexpr size_t kLimit = 256 * 1024 * 1024;
const std::set<std::string> kTables = {
    "schema_meta", "signal_identity", "signal_prekey", "signal_session",
    "signal_skipped_key", "signal_peer_prekey", "signal_peer_identity",
    "contact", "addressbook", "keyinfo", "code", "localemail", "email_body",
    "session", "group_session_email", "file_transfer", "file_chunk",
    "group_session", "mls_state", "mls_member_binding", "mls_group_meta",
    "unified_session", "sqlite_sequence",
};

void require(bool condition, const char* error) {
    if (!condition) throw std::runtime_error(error);
}

// Best-effort clearing of owned JSON strings/binaries. Callers still own their
// input copies, and SQLite/JSON allocators may hold intermediate copies.
void wipe(json& value) {
    if (value.is_string()) {
        auto& s = value.get_ref<std::string&>();
        OPENSSL_cleanse(s.data(), s.size());
    } else if (value.is_binary()) {
        auto& b = value.get_binary();
        OPENSSL_cleanse(b.data(), b.size());
    } else if (value.is_structured()) {
        for (auto& child : value) wipe(child);
    }
}

struct Bytes {
    std::vector<uint8_t> data;
    ~Bytes() { OPENSSL_cleanse(data.data(), data.size()); }
};

struct Budget {
    size_t used = 0;
    void add(size_t n) {
        require(n <= kLimit - used, "snapshot_too_large");
        used += n;
    }
};

using Statement = std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)>;
Statement prepare(sqlite3* db, const std::string& sql) {
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
    Statement owned(stmt, sqlite3_finalize);
    require(rc == SQLITE_OK, "snapshot_sql_prepare_failed");
    return owned;
}

void exec(sqlite3* db, const char* sql) {
    require(sqlite3_exec(db, sql, nullptr, nullptr, nullptr) == SQLITE_OK,
            "snapshot_sql_failed");
}

struct Transaction {
    sqlite3* db;
    bool active = true;
    Transaction(sqlite3* handle, bool write) : db(handle) {
        require(db && sqlite3_get_autocommit(db), "snapshot_requires_idle_connection");
        exec(db, write ? "BEGIN IMMEDIATE" : "BEGIN");
    }
    ~Transaction() { if (active) sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr); }
    void commit() { exec(db, "COMMIT"); active = false; }
};

std::string text(sqlite3_stmt* stmt, int column) {
    const auto* p = sqlite3_column_text(stmt, column);
    int n = sqlite3_column_bytes(stmt, column);
    return p ? std::string(reinterpret_cast<const char*>(p), n) : "";
}

std::set<std::string> tables(sqlite3* db) {
    auto stmt = prepare(db, "SELECT name FROM sqlite_master WHERE type='table' ORDER BY name");
    std::set<std::string> result;
    int rc;
    while ((rc = sqlite3_step(stmt.get())) == SQLITE_ROW) {
        auto name = text(stmt.get(), 0);
        if (name == "task" || name.rfind("sqlite_stat", 0) == 0) continue;
        require(kTables.count(name) != 0, "snapshot_unknown_table");
        result.insert(name);
    }
    require(rc == SQLITE_DONE, "snapshot_read_failed");
    return result;
}

json schema(sqlite3* db, const std::string& name) {
    require(kTables.count(name) != 0, "snapshot_unknown_table");
    auto stmt = prepare(db, "PRAGMA table_info(\"" + name + "\")");
    json columns = json::array();
    int rc;
    while ((rc = sqlite3_step(stmt.get())) == SQLITE_ROW) {
        columns.push_back({
            {"name", text(stmt.get(), 1)}, {"type", text(stmt.get(), 2)},
            {"not_null", sqlite3_column_int(stmt.get(), 3)},
            {"default", sqlite3_column_type(stmt.get(), 4) == SQLITE_NULL
                ? json(nullptr) : json(text(stmt.get(), 4))},
            {"pk", sqlite3_column_int(stmt.get(), 5)},
        });
    }
    require(rc == SQLITE_DONE && !columns.empty(), "snapshot_schema_missing");
    return columns;
}

json cell(sqlite3_stmt* stmt, int col, Budget& budget) {
    budget.add(32);
    switch (sqlite3_column_type(stmt, col)) {
        case SQLITE_NULL: return nullptr;
        case SQLITE_INTEGER: return sqlite3_column_int64(stmt, col);
        case SQLITE_FLOAT: {
            double v = sqlite3_column_double(stmt, col);
            require(std::isfinite(v), "snapshot_nonfinite_number");
            return v;
        }
        case SQLITE_TEXT:
            budget.add(sqlite3_column_bytes(stmt, col));
            return text(stmt, col);
        case SQLITE_BLOB: {
            size_t n = sqlite3_column_bytes(stmt, col);
            budget.add(n);
            const auto* p = static_cast<const uint8_t*>(sqlite3_column_blob(stmt, col));
            return json::binary(n ? std::vector<uint8_t>(p, p + n) : std::vector<uint8_t>{});
        }
        default: throw std::runtime_error("snapshot_unknown_storage_class");
    }
}

size_t columnIndex(const json& table, const std::string& name) {
    const auto& columns = table.at("columns");
    for (size_t i = 0; i < columns.size(); ++i)
        if (columns[i].at("name") == name) return i;
    throw std::runtime_error("snapshot_required_column_missing");
}

void validateConfig(const json& config, const json& credentials) {
    require(config.is_object() && config.contains("accounts") && config["accounts"].is_array(),
            "snapshot_invalid_config");
    require(credentials.is_object(), "snapshot_invalid_credentials");
    std::set<std::string> expected;
    for (const auto& account : config["accounts"]) {
        require(account.is_object() && account.contains("email") && account["email"].is_string(),
                "snapshot_invalid_account");
        std::string email = account["email"];
        require(!email.empty() && expected.insert(email).second &&
                expected.insert(email + ":refresh").second, "snapshot_duplicate_account");
    }
    require(credentials.size() == expected.size(), "snapshot_credentials_incomplete");
    for (const auto& key : expected)
        require(credentials.contains(key) && credentials[key].is_string(),
                "snapshot_credentials_incomplete");
}

void validate(const json& root) {
    require(root.is_object() && root.value("snapshot_format", 0) == 1 &&
            root.contains("tables") && root["tables"].is_object() &&
            root.contains("config") && root.contains("credentials") &&
            root.contains("originals") && root["originals"].is_array(),
            "snapshot_invalid_payload");
    validateConfig(root["config"], root["credentials"]);
    require(!root["tables"].empty(), "snapshot_empty_schema");
    for (const auto& item : root["tables"].items()) {
        require(kTables.count(item.key()) != 0, "snapshot_unknown_table");
        const auto& table = item.value();
        require(table.is_object() && table.contains("columns") && table["columns"].is_array() &&
                !table["columns"].empty() && table.contains("rows") && table["rows"].is_array(),
                "snapshot_invalid_table");
        std::set<std::string> names;
        for (const auto& col : table["columns"])
            require(col.is_object() && col.contains("name") && col["name"].is_string() &&
                    names.insert(col["name"].get<std::string>()).second,
                    "snapshot_invalid_columns");
        for (const auto& row : table["rows"]) {
            require(row.is_array() && row.size() == table["columns"].size(), "snapshot_invalid_row");
            for (const auto& v : row)
                require(v.is_null() || v.is_string() || v.is_binary() ||
                        v.is_number_integer() || (v.is_number_float() &&
                        std::isfinite(v.get<double>())), "snapshot_invalid_cell");
        }
    }
    std::set<int64_t> expected;
    if (root["tables"].contains("file_transfer")) {
        const auto& ft = root["tables"]["file_transfer"];
        size_t id = columnIndex(ft, "id"), path = columnIndex(ft, "original_path");
        for (const auto& row : ft["rows"]) {
            require(row[path].is_null() || row[path].is_string(), "snapshot_invalid_file_path");
            if (row[path].is_string() && !row[path].get_ref<const std::string&>().empty()) {
                require(row[id].is_number_integer() && row[id].get<int64_t>() > 0 &&
                        expected.insert(row[id].get<int64_t>()).second, "snapshot_invalid_file_id");
            }
        }
    }
    for (const auto& file : root["originals"]) {
        require(file.is_object() && file.contains("transfer_id") &&
                file["transfer_id"].is_number_integer() && file.contains("data") &&
                file["data"].is_binary(), "snapshot_invalid_original");
        require(expected.erase(file["transfer_id"].get<int64_t>()) == 1,
                "snapshot_unexpected_original");
    }
    require(expected.empty(), "snapshot_original_missing");
    if (root["tables"].contains("sqlite_sequence")) {
        const auto& seq = root["tables"]["sqlite_sequence"];
        size_t name = columnIndex(seq, "name");
        for (const auto& row : seq["rows"])
            require(row[name].is_string() &&
                    root["tables"].contains(row[name].get<std::string>()) &&
                    row[name] != "sqlite_sequence", "snapshot_invalid_sequence");
    }
}

void captureOriginals(json& root, Budget& budget) {
    root["originals"] = json::array();
    if (!root["tables"].contains("file_transfer")) return;
    auto& table = root["tables"]["file_transfer"];
    size_t id = columnIndex(table, "id"), path = columnIndex(table, "original_path");
    size_t missing = 0;
    for (auto& row : table["rows"]) {
        if (row[path].is_null() || row[path] == "") continue;
        auto source = std::filesystem::u8path(row[path].get<std::string>());
        if (!std::filesystem::is_regular_file(source)) {
            // Original was moved/deleted after sending. Degrade rather than
            // fail the entire backup: keep the transfer row, mark the file
            // unavailable, and count it for the restore preview.
            row[path] = "";
            missing++;
            continue;
        }
        auto size = std::filesystem::file_size(source);
        auto modified = std::filesystem::last_write_time(source);
        require(size <= kLimit, "snapshot_too_large");
        budget.add(static_cast<size_t>(size) + 64);
        Bytes bytes;
        bytes.data.resize(static_cast<size_t>(size));
        std::ifstream in(source, std::ios::binary);
        require(in.is_open(), "snapshot_original_unreadable");
        if (size) in.read(reinterpret_cast<char*>(bytes.data.data()), bytes.data.size());
        require(in.good() && in.peek() == std::char_traits<char>::eof() &&
                !in.bad() && std::filesystem::file_size(source) == size &&
                std::filesystem::last_write_time(source) == modified,
                "snapshot_original_changed");
        root["originals"].push_back({{"transfer_id", row[id]},
                                    {"data", json::binary(std::move(bytes.data))}});
    }
    if (missing) root["missing_originals"] = missing;
}

void bindCell(sqlite3_stmt* stmt, int pos, const json& v) {
    int rc = SQLITE_MISUSE;
    if (v.is_null()) rc = sqlite3_bind_null(stmt, pos);
    else if (v.is_string()) {
        const auto& s = v.get_ref<const std::string&>();
        rc = sqlite3_bind_text64(stmt, pos, s.data(), s.size(), SQLITE_TRANSIENT, SQLITE_UTF8);
    } else if (v.is_binary()) {
        const auto& b = v.get_binary();
        static const uint8_t empty = 0;
        rc = sqlite3_bind_blob64(stmt, pos, b.empty() ? &empty : b.data(), b.size(), SQLITE_TRANSIENT);
    } else if (v.is_number_float()) rc = sqlite3_bind_double(stmt, pos, v.get<double>());
    else {
        require(!v.is_number_unsigned() ||
                v.get<uint64_t>() <= static_cast<uint64_t>(std::numeric_limits<int64_t>::max()),
                "snapshot_integer_overflow");
        rc = sqlite3_bind_int64(stmt, pos, v.get<int64_t>());
    }
    require(rc == SQLITE_OK, "snapshot_bind_failed");
}
} // namespace

Snapshot::~Snapshot() { wipe(data); }
Snapshot::Snapshot(Snapshot&& other) noexcept : data(std::move(other.data)) {}
Snapshot& Snapshot::operator=(Snapshot&& other) noexcept {
    if (this != &other) { wipe(data); data = std::move(other.data); }
    return *this;
}

void exportStoppedSnapshot(sqlite3* source, const std::filesystem::path& configPath,
                           const std::filesystem::path& destination,
                           const std::string& password) {
    require(g_db == nullptr, "snapshot_requires_stopped_core");
    require(source && sqlite3_get_autocommit(source), "snapshot_requires_idle_connection");
    require(std::filesystem::file_size(configPath) <= 8 * 1024 * 1024,
            "snapshot_config_too_large");
    Snapshot input;
    std::ifstream file(configPath, std::ios::binary);
    require(file.is_open(), "snapshot_config_unreadable");
    try { file >> input.data; }
    catch (const json::exception&) { throw std::runtime_error("snapshot_invalid_config"); }
    require(!file.bad(), "snapshot_config_unreadable");
    const auto& config = input.data;
    require(config.is_object() && config.contains("accounts") && config["accounts"].is_array(),
            "snapshot_invalid_config");
    std::vector<std::string> keys;
    for (const auto& account : config["accounts"]) {
        require(account.is_object() && account.contains("email") && account["email"].is_string(),
                "snapshot_invalid_account");
        std::string email = account["email"];
        keys.push_back(email);
        keys.push_back(email + ":refresh");
    }
    struct Secrets {
        std::map<std::string, std::string> values;
        ~Secrets() {
            for (auto& item : values) OPENSSL_cleanse(item.second.data(), item.second.size());
        }
    } secrets;
    require(keychain_store::snapshot_secrets(
        (configPath.parent_path() / "secrets.vault").u8string(), keys, secrets.values),
        "snapshot_credential_store_unreadable");
    Snapshot credentialInput;
    credentialInput.data = secrets.values;
    // The current config loader accepts inline secrets too. Capture them if
    // present, without rewriting/migrating the source configuration.
    for (const auto& account : config["accounts"]) {
        std::string email = account["email"];
        for (const auto* field : {"auth_code", "refresh_token"}) {
            auto it = account.find(field);
            if (it != account.end() && it->is_string() && !it->get_ref<const std::string&>().empty())
                credentialInput.data[email + (std::string(field) == "refresh_token" ? ":refresh" : "")] = *it;
        }
    }
    exportSnapshot(source, config, credentialInput.data, destination, password);
}

void exportSnapshot(sqlite3* source, const json& config, const json& credentials,
                    const std::filesystem::path& destination, const std::string& password) {
    validateConfig(config, credentials);
    Snapshot snapshot;
    auto& root = snapshot.data;
    root = {{"snapshot_format", 1}, {"config", config}, {"credentials", credentials},
            {"tables", json::object()}};
    // Configuration never becomes a second, stale source of credentials.
    for (auto& account : root["config"]["accounts"]) {
        for (const auto* field : {"auth_code", "refresh_token"}) {
            if (account.contains(field)) {
                wipe(account[field]);
                account[field] = "";
            }
        }
    }
    Budget budget;
    budget.add(config.dump().size() + credentials.dump().size());
    {
        Transaction tx(source, false);
        auto names = tables(source);
        require(!names.empty(), "snapshot_empty_schema");
        for (const auto& name : names) {
            json table = {{"columns", schema(source, name)}, {"rows", json::array()}};
            auto& output = root["tables"][name];
            output = std::move(table);
            budget.add(output["columns"].dump().size());
            auto stmt = prepare(source, "SELECT * FROM \"" + name + "\" ORDER BY rowid");
            int rc;
            while ((rc = sqlite3_step(stmt.get())) == SQLITE_ROW) {
                if (name == "sqlite_sequence" &&
                    (text(stmt.get(), 0) == "task" || !names.count(text(stmt.get(), 0)))) continue;
                budget.add(32);
                output["rows"].push_back(json::array());
                auto& row = output["rows"].back();
                for (int c = 0; c < sqlite3_column_count(stmt.get()); ++c)
                    row.push_back(cell(stmt.get(), c, budget));
            }
            require(rc == SQLITE_DONE, "snapshot_read_failed");
        }
        captureOriginals(root, budget);
        validate(root);
        tx.commit();
    }
    Bytes payload;
    payload.data = json::to_cbor(root);
    require(payload.data.size() <= kLimit, "snapshot_too_large");
    int rc = oim_backup_seal_file(destination.u8string().c_str(),
        payload.data.data(), payload.data.size(),
        reinterpret_cast<const uint8_t*>(password.data()), password.size());
    if (rc != 0) throw std::runtime_error("backup_container_export:" + std::to_string(rc));
}

Snapshot readSnapshot(const std::filesystem::path& source, const std::string& password) {
    struct Buffer {
        uint8_t* data = nullptr;
        size_t size = 0;
        ~Buffer() { oim_backup_free_buffer(data, size); }
    } payload;
    int rc = oim_backup_open_file(source.u8string().c_str(),
        reinterpret_cast<const uint8_t*>(password.data()), password.size(),
        &payload.data, &payload.size);
    if (rc != 0) throw std::runtime_error("backup_container_open:" + std::to_string(rc));
    Snapshot snapshot;
    try {
        const char* begin = reinterpret_cast<const char*>(payload.data);
        snapshot.data = json::from_cbor(begin, begin + payload.size);
        validate(snapshot.data);
    } catch (const json::exception&) {
        throw std::runtime_error("snapshot_invalid_payload");
    }
    return snapshot;
}

void restoreTables(sqlite3* target, const Snapshot& snapshot,
                   const std::map<int64_t, std::string>& originalPaths) {
    const auto& root = snapshot.data;
    validate(root);
    require(originalPaths.size() == root["originals"].size(), "snapshot_paths_incomplete");
    for (const auto& file : root["originals"]) {
        auto it = originalPaths.find(file["transfer_id"].get<int64_t>());
        require(it != originalPaths.end() && !it->second.empty(), "snapshot_paths_incomplete");
    }
    Transaction tx(target, true);
    auto targetNames = tables(target);
    require(targetNames.size() == root["tables"].size(), "snapshot_schema_mismatch");
    for (const auto& name : targetNames) {
        require(root["tables"].contains(name) &&
                schema(target, name) == root["tables"][name]["columns"], "snapshot_schema_mismatch");
        if (name == "schema_meta" || name == "sqlite_sequence") continue;
        auto stmt = prepare(target, "SELECT 1 FROM \"" + name + "\" LIMIT 1");
        require(sqlite3_step(stmt.get()) == SQLITE_DONE, "snapshot_target_not_empty");
    }
    // task is excluded from the archive, but a target with queued tasks is not empty.
    auto task = prepare(target, "SELECT count(*) FROM sqlite_master WHERE type='table' AND name='task'");
    require(sqlite3_step(task.get()) == SQLITE_ROW, "snapshot_read_failed");
    if (sqlite3_column_int(task.get(), 0) != 0) {
        auto stmt = prepare(target, "SELECT 1 FROM task LIMIT 1");
        require(sqlite3_step(stmt.get()) == SQLITE_DONE, "snapshot_target_not_empty");
    }
    for (const auto& name : targetNames) {
        // sqlite_sequence must be restored last, after inserts advanced counters.
        if (name == "sqlite_sequence") continue;
        const auto& table = root["tables"][name];
        if (name == "schema_meta") exec(target, "DELETE FROM schema_meta");
        std::string sql = "INSERT INTO \"" + name + "\" VALUES(";
        for (size_t i = 0; i < table["columns"].size(); ++i) sql += i ? ",?" : "?";
        sql += ")";
        auto stmt = prepare(target, sql);
        size_t id = 0, path = 0;
        if (name == "file_transfer") {
            id = columnIndex(table, "id");
            path = columnIndex(table, "original_path");
        }
        for (const auto& row : table["rows"]) {
            sqlite3_reset(stmt.get());
            sqlite3_clear_bindings(stmt.get());
            for (size_t i = 0; i < row.size(); ++i) {
                if (name == "file_transfer" && i == path &&
                    row[i].is_string() && row[i] != "")
                    bindCell(stmt.get(), static_cast<int>(i + 1), originalPaths.at(row[id].get<int64_t>()));
                else bindCell(stmt.get(), static_cast<int>(i + 1), row[i]);
            }
            require(sqlite3_step(stmt.get()) == SQLITE_DONE, "snapshot_insert_failed");
        }
    }
    if (targetNames.count("sqlite_sequence")) {
        exec(target, "DELETE FROM sqlite_sequence");
        auto stmt = prepare(target, "INSERT INTO sqlite_sequence(name,seq) VALUES(?,?)");
        for (const auto& row : root["tables"]["sqlite_sequence"]["rows"]) {
            require(row.size() == 2, "snapshot_invalid_sequence");
            sqlite3_reset(stmt.get());
            bindCell(stmt.get(), 1, row[0]); bindCell(stmt.get(), 2, row[1]);
            require(sqlite3_step(stmt.get()) == SQLITE_DONE, "snapshot_insert_failed");
        }
    }
    tx.commit();
}
} // namespace backup
