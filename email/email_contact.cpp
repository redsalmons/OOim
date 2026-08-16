#include "email_core_common.h"
#include "email_core.h"
#include "logger.h"
#include "contact_repo.h"
#include "db_connection.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>

static ContactRepo s_contactRepo;

extern "C" int contact_add(const char* email, const char* name, const char* categories, const char* notes, const char* key) {
    if (!email || !name) {
        LOG_INFO("contact_add: email or name is null\n");
        return -1;
    }

    auto& conn = DbConnection::instance();
    if (!conn.get()) {
        LOG_INFO("contact_add: database not initialized\n");
        return -2;
    }

    std::lock_guard<std::mutex> lock(conn.mutex());

    if (!s_contactRepo.add(email, name, categories ? categories : "", notes ? notes : "", key ? key : "")) {
        LOG_INFO("contact_add: insert failed\n");
        return -4;
    }

    int id = (int)sqlite3_last_insert_rowid(conn.get());
    LOG_INFO("contact_add: inserted contact id=%d, email=%s, name=%s\n", id, email, name);
    return id;
}

extern "C" const char* contact_query_all() {
    auto& conn = DbConnection::instance();
    if (!conn.get()) {
        LOG_INFO("contact_query_all: database not initialized\n");
        return NULL;
    }

    std::lock_guard<std::mutex> lock(conn.mutex());

    auto contacts = s_contactRepo.queryAll();

    std::string json = "[";
    bool first = true;
    for (const auto& c : contacts) {
        if (!first) json += ",";
        first = false;
        json += "{";
        json += "\"id\":" + std::to_string(c.id) + ",";
        json += "\"email\":\"" + c.email + "\",";
        json += "\"name\":\"" + c.name + "\",";
        json += "\"categories\":\"" + c.categories + "\",";
        json += "\"notes\":\"" + c.notes + "\",";
        json += "\"key\":\"" + c.key + "\"";
        json += "}";
    }
    json += "]";

    char* result = (char*)malloc(json.size() + 1);
    if (result) {
        strcpy(result, json.c_str());
    }
    return result;
}

extern "C" int contact_delete(int id) {
    auto& conn = DbConnection::instance();
    if (!conn.get()) {
        LOG_INFO("contact_delete: database not initialized\n");
        return -2;
    }

    std::lock_guard<std::mutex> lock(conn.mutex());

    if (!s_contactRepo.remove(id)) {
        LOG_INFO("contact_delete: failed for id=%d\n", id);
        return -4;
    }

    LOG_INFO("contact_delete: deleted id=%d\n", id);
    return 0;
}
