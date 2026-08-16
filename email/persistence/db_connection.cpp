#include "db_connection.h"
#include "email_core_common.h"
#include <cstdio>

DbConnection& DbConnection::instance() {
    static DbConnection inst;
    return inst;
}

DbConnection::~DbConnection() {
    // Don't close g_db here — it's managed by email_db_close()
}

bool DbConnection::open(const std::string& path) {
    // Database is opened by email_db_init() which sets g_db.
    // This method is kept for interface compatibility but is a no-op.
    return g_db != nullptr;
}

void DbConnection::close() {
    // Don't close g_db here — it's managed by email_db_close()
}

sqlite3* DbConnection::get() const {
    return g_db;
}

std::mutex& DbConnection::mutex() {
    return g_db_mutex;
}

bool DbConnection::beginTransaction() {
    return sqlite3_exec(g_db, "BEGIN IMMEDIATE;", NULL, NULL, NULL) == SQLITE_OK;
}

bool DbConnection::commitTransaction() {
    return sqlite3_exec(g_db, "COMMIT;", NULL, NULL, NULL) == SQLITE_OK;
}

bool DbConnection::rollbackTransaction() {
    return sqlite3_exec(g_db, "ROLLBACK;", NULL, NULL, NULL) == SQLITE_OK;
}
