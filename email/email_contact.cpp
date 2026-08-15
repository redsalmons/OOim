#include "email_core_common.h"
#include "email_core.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>
#include <string>

extern "C" int contact_add(const char* email, const char* name, const char* categories, const char* notes, const char* key) {
    if (!email || !name) {
        LOG_INFO("contact_add: email or name is null\n");
        return -1;
    }

    sqlite3* db = email_core_get_db();
    if (!db) {
        LOG_INFO("contact_add: database not initialized\n");
        return -2;
    }

    std::lock_guard<std::mutex> lock(email_core_get_db_mutex());

    const char* sql = "INSERT INTO contact (email, name, categories, notes, key) VALUES (?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        LOG_INFO("contact_add: prepare failed: %s\n", sqlite3_errmsg(db));
        return -3;
    }

    sqlite3_bind_text(stmt, 1, email, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, categories ? categories : "", -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, notes ? notes : "", -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, key ? key : "", -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        LOG_INFO("contact_add: step failed: %s\n", sqlite3_errmsg(db));
        return -4;
    }

    int id = (int)sqlite3_last_insert_rowid(db);
    LOG_INFO("contact_add: inserted contact id=%d, email=%s, name=%s\n", id, email, name);
    return id;
}

extern "C" const char* contact_query_all() {
    sqlite3* db = email_core_get_db();
    if (!db) {
        LOG_INFO("contact_query_all: database not initialized\n");
        return NULL;
    }

    std::lock_guard<std::mutex> lock(email_core_get_db_mutex());

    const char* sql = "SELECT id, email, name, categories, notes, key FROM contact ORDER BY name;";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        LOG_INFO("contact_query_all: prepare failed: %s\n", sqlite3_errmsg(db));
        return NULL;
    }

    std::string json = "[";
    bool first = true;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        if (!first) json += ",";
        first = false;
        json += "{";
        json += "\"id\":" + std::to_string(sqlite3_column_int(stmt, 0)) + ",";
        json += "\"email\":\"" + std::string((const char*)sqlite3_column_text(stmt, 1)) + "\",";
        json += "\"name\":\"" + std::string((const char*)sqlite3_column_text(stmt, 2)) + "\",";
        json += "\"categories\":\"" + std::string((const char*)sqlite3_column_text(stmt, 3)) + "\",";
        json += "\"notes\":\"" + std::string((const char*)sqlite3_column_text(stmt, 4)) + "\",";
        json += "\"key\":\"" + std::string((const char*)sqlite3_column_text(stmt, 5)) + "\"";
        json += "}";
    }
    json += "]";
    sqlite3_finalize(stmt);

    char* result = (char*)malloc(json.size() + 1);
    if (result) {
        strcpy(result, json.c_str());
    }
    return result;
}

extern "C" int contact_delete(int id) {
    sqlite3* db = email_core_get_db();
    if (!db) {
        LOG_INFO("contact_delete: database not initialized\n");
        return -2;
    }

    std::lock_guard<std::mutex> lock(email_core_get_db_mutex());

    const char* sql = "DELETE FROM contact WHERE id = ?;";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        LOG_INFO("contact_delete: prepare failed: %s\n", sqlite3_errmsg(db));
        return -3;
    }

    sqlite3_bind_int(stmt, 1, id);
    rc = sqlite3_step(stmt);
    int affected = sqlite3_changes(db);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        LOG_INFO("contact_delete: step failed: %s\n", sqlite3_errmsg(db));
        return -4;
    }

    LOG_INFO("contact_delete: deleted id=%d, %d rows affected\n", id, affected);
    return 0;
}
