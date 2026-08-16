#ifndef PERSISTENCE_DB_CONNECTION_H
#define PERSISTENCE_DB_CONNECTION_H

#include <sqlite3.h>
#include <string>
#include <mutex>

class DbConnection {
public:
    static DbConnection& instance();

    bool open(const std::string& path);
    void close();
    sqlite3* get() const;
    std::mutex& mutex();

    bool beginTransaction();
    bool commitTransaction();
    bool rollbackTransaction();

private:
    DbConnection() = default;
    ~DbConnection();
    DbConnection(const DbConnection&) = delete;
    DbConnection& operator=(const DbConnection&) = delete;
};

#endif // PERSISTENCE_DB_CONNECTION_H
