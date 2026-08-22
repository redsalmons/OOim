#include "email_core_common.h"
#include "email_core.h"
#include "logger.h"
#include "addressbook_repo.h"
#include "db_connection.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <sstream>

static AddressBookRepo s_addressBookRepo;

// Escape a string for JSON
static std::string jsonEscape(const std::string& s) {
    std::string out;
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out += c; break;
        }
    }
    return out;
}

// Add an email address to addressbook if not already present (called during email receiving)
extern "C" int addressbook_add_email(const char* email, const char* name) {
    if (!email) return -1;

    auto& conn = DbConnection::instance();
    if (!conn.get()) return -2;

    std::lock_guard<std::mutex> lock(conn.mutex());

    std::string emailStr(email);
    std::string nameStr = name ? name : "";

    if (!s_addressBookRepo.insertIfNotExists(emailStr, nameStr)) {
        // Already exists or failed - not an error
        return 0;
    }

    LOG_INFO("addressbook_add_email: added email=%s, name=%s\n", emailStr.c_str(), nameStr.c_str());
    return 0;
}

// Query all addressbook entries as JSON array
extern "C" const char* addressbook_query_all() {
    auto& conn = DbConnection::instance();
    if (!conn.get()) {
        LOG_INFO("addressbook_query_all: database not initialized\n");
        return NULL;
    }

    std::lock_guard<std::mutex> lock(conn.mutex());

    auto entries = s_addressBookRepo.queryAll();

    std::string json = "[";
    bool first = true;
    for (const auto& e : entries) {
        if (!first) json += ",";
        first = false;
        json += "{";
        json += "\"id\":" + std::to_string(e.id) + ",";
        json += "\"email\":\"" + jsonEscape(e.email) + "\",";
        json += "\"name\":\"" + jsonEscape(e.name) + "\",";
        json += "\"group_name\":\"" + jsonEscape(e.groupName) + "\",";
        json += "\"notes\":\"" + jsonEscape(e.notes) + "\"";
        json += "}";
    }
    json += "]";

    char* result = (char*)malloc(json.size() + 1);
    if (result) {
        strcpy(result, json.c_str());
    }
    return result;
}

// Query all distinct group names as JSON array
extern "C" const char* addressbook_query_groups() {
    auto& conn = DbConnection::instance();
    if (!conn.get()) return NULL;

    std::lock_guard<std::mutex> lock(conn.mutex());

    auto groups = s_addressBookRepo.queryGroups();

    std::string json = "[";
    bool first = true;
    for (const auto& g : groups) {
        if (!first) json += ",";
        first = false;
        json += "\"" + jsonEscape(g) + "\"";
    }
    json += "]";

    char* result = (char*)malloc(json.size() + 1);
    if (result) {
        strcpy(result, json.c_str());
    }
    return result;
}

// Update addressbook entry (name, group, notes)
extern "C" int addressbook_update(int id, const char* name, const char* groupName, const char* notes) {
    if (!name || !groupName || !notes) return -1;

    auto& conn = DbConnection::instance();
    if (!conn.get()) return -2;

    std::lock_guard<std::mutex> lock(conn.mutex());

    if (!s_addressBookRepo.update(id, name, groupName, notes)) {
        LOG_INFO("addressbook_update: failed for id=%d\n", id);
        return -4;
    }

    LOG_INFO("addressbook_update: updated id=%d\n", id);
    return 0;
}

// Delete addressbook entry by id
extern "C" int addressbook_delete(int id) {
    auto& conn = DbConnection::instance();
    if (!conn.get()) return -2;

    std::lock_guard<std::mutex> lock(conn.mutex());

    if (!s_addressBookRepo.remove(id)) {
        LOG_INFO("addressbook_delete: failed for id=%d\n", id);
        return -4;
    }

    LOG_INFO("addressbook_delete: deleted id=%d\n", id);
    return 0;
}

// Parse email addresses from a header value like "Name <email@domain.com>, Name2 <email2@domain.com>"
// and insert each into addressbook. Also extracts display names.
extern "C" int addressbook_extract_from_header(const char* headerValue) {
    if (!headerValue) return -1;

    std::string header(headerValue);
    if (header.empty()) return 0;

    auto& conn = DbConnection::instance();
    if (!conn.get()) return -2;

    std::lock_guard<std::mutex> lock(conn.mutex());

    // Split by comma, then parse each address
    std::stringstream ss(header);
    std::string segment;
    int count = 0;

    while (std::getline(ss, segment, ',')) {
        // Trim whitespace
        size_t start = segment.find_first_not_of(" \t\r\n");
        size_t end = segment.find_last_not_of(" \t\r\n");
        if (start == std::string::npos) continue;
        std::string addr = segment.substr(start, end - start + 1);

        // Extract email from "Name <email>" or "email" format
        std::string email, name;
        size_t lt = addr.find('<');
        size_t gt = addr.find('>');
        if (lt != std::string::npos && gt != std::string::npos && gt > lt) {
            email = addr.substr(lt + 1, gt - lt - 1);
            name = addr.substr(0, lt);
            // Trim name
            size_t ns = name.find_first_not_of(" \t\r\n\"");
            size_t ne = name.find_last_not_of(" \t\r\n\"");
            if (ns != std::string::npos) {
                name = name.substr(ns, ne - ns + 1);
            } else {
                name = "";
            }
        } else {
            email = addr;
        }

        // Trim email
        size_t es = email.find_first_not_of(" \t\r\n<>");
        size_t ee = email.find_last_not_of(" \t\r\n<>");
        if (es != std::string::npos) {
            email = email.substr(es, ee - es + 1);
        }

        if (!email.empty() && email.find('@') != std::string::npos) {
            if (s_addressBookRepo.insertIfNotExists(email, name)) {
                count++;
            }
        }
    }

    if (count > 0) {
        LOG_INFO("addressbook_extract_from_header: added %d new contacts from header\n", count);
    }
    return count;
}

// Migrate contacts from existing localemail records (from_addr, to_addr) into addressbook
extern "C" int addressbook_migrate_from_emails() {
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db) return -2;

    std::lock_guard<std::mutex> lock(conn.mutex());

    const char* sql = "SELECT from_addr, to_addr FROM localemail;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return -3;

    int count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* from = (const char*)sqlite3_column_text(stmt, 0);
        const char* to = (const char*)sqlite3_column_text(stmt, 1);
        if (from && from[0]) {
            std::string fromStr(from);
            std::stringstream ss(fromStr);
            std::string segment;
            while (std::getline(ss, segment, ',')) {
                size_t start = segment.find_first_not_of(" \t\r\n");
                if (start == std::string::npos) continue;
                size_t end = segment.find_last_not_of(" \t\r\n");
                std::string addr = segment.substr(start, end - start + 1);
                size_t lt = addr.find('<');
                size_t gt = addr.find('>');
                std::string email, name;
                if (lt != std::string::npos && gt != std::string::npos && gt > lt) {
                    email = addr.substr(lt + 1, gt - lt - 1);
                    name = addr.substr(0, lt);
                } else {
                    email = addr;
                }
                size_t es = email.find_first_not_of(" \t\r\n<>");
                size_t ee = email.find_last_not_of(" \t\r\n<>");
                if (es != std::string::npos) email = email.substr(es, ee - es + 1);
                if (!email.empty() && email.find('@') != std::string::npos) {
                    if (s_addressBookRepo.insertIfNotExists(email, name)) count++;
                }
            }
        }
        if (to && to[0]) {
            std::string toStr(to);
            std::stringstream ss(toStr);
            std::string segment;
            while (std::getline(ss, segment, ',')) {
                size_t start = segment.find_first_not_of(" \t\r\n");
                if (start == std::string::npos) continue;
                size_t end = segment.find_last_not_of(" \t\r\n");
                std::string addr = segment.substr(start, end - start + 1);
                size_t lt = addr.find('<');
                size_t gt = addr.find('>');
                std::string email, name;
                if (lt != std::string::npos && gt != std::string::npos && gt > lt) {
                    email = addr.substr(lt + 1, gt - lt - 1);
                    name = addr.substr(0, lt);
                } else {
                    email = addr;
                }
                size_t es = email.find_first_not_of(" \t\r\n<>");
                size_t ee = email.find_last_not_of(" \t\r\n<>");
                if (es != std::string::npos) email = email.substr(es, ee - es + 1);
                if (!email.empty() && email.find('@') != std::string::npos) {
                    if (s_addressBookRepo.insertIfNotExists(email, name)) count++;
                }
            }
        }
    }
    sqlite3_finalize(stmt);

    LOG_INFO("addressbook_migrate_from_emails: migrated %d contacts\n", count);
    return count;
}
