#include "file_transfer_repo.h"
#include "db_connection.h"
#include "../email_core_common.h"
#include <sqlite3.h>
#include <cstring>

// --- file_transfer table ---

bool FileTransferRepo::insertFileTransfer(const FileTransferRecord& rec) {
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db || rec.fileId.empty()) return false;

    const char* sql = "INSERT OR REPLACE INTO file_transfer "
                      "(file_id, session_id, account, sender, file_name, file_size, file_md5, total_chunks, chunk_size, status) "
                      "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, rec.fileId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, rec.sessionId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, rec.account.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, rec.sender.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, rec.fileName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 6, rec.fileSize);
    sqlite3_bind_text(stmt, 7, rec.fileMd5.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 8, rec.totalChunks);
    sqlite3_bind_int(stmt, 9, rec.chunkSize);
    sqlite3_bind_int(stmt, 10, rec.status);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool FileTransferRepo::queryByFileId(const std::string& fileId, FileTransferRecord& out) {
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db || fileId.empty()) return false;

    const char* sql = "SELECT id, file_id, session_id, account, sender, file_name, file_size, file_md5, total_chunks, chunk_size, status, created_at, updated_at "
                      "FROM file_transfer WHERE file_id = ?;";
    sqlite3_stmt* stmt;
    bool found = false;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, fileId.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            out.id = sqlite3_column_int64(stmt, 0);
            out.fileId = (const char*)sqlite3_column_text(stmt, 1);
            out.sessionId = sqlite3_column_text(stmt, 2) ? (const char*)sqlite3_column_text(stmt, 2) : "";
            out.account = (const char*)sqlite3_column_text(stmt, 3);
            out.sender = sqlite3_column_text(stmt, 4) ? (const char*)sqlite3_column_text(stmt, 4) : "";
            out.fileName = (const char*)sqlite3_column_text(stmt, 5);
            out.fileSize = sqlite3_column_int64(stmt, 6);
            out.fileMd5 = sqlite3_column_text(stmt, 7) ? (const char*)sqlite3_column_text(stmt, 7) : "";
            out.totalChunks = sqlite3_column_int(stmt, 8);
            out.chunkSize = sqlite3_column_int(stmt, 9);
            out.status = sqlite3_column_int(stmt, 10);
            out.createdAt = sqlite3_column_text(stmt, 11) ? (const char*)sqlite3_column_text(stmt, 11) : "";
            out.updatedAt = sqlite3_column_text(stmt, 12) ? (const char*)sqlite3_column_text(stmt, 12) : "";
            found = true;
        }
        sqlite3_finalize(stmt);
    }
    return found;
}

std::vector<FileTransferRecord> FileTransferRepo::queryPendingByAccount(const std::string& account) {
    std::vector<FileTransferRecord> result;
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db) return result;

    const char* sql = "SELECT id, file_id, session_id, account, sender, file_name, file_size, file_md5, total_chunks, chunk_size, status, created_at, updated_at "
                      "FROM file_transfer WHERE account = ? AND status = 0 ORDER BY id ASC;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, account.c_str(), -1, SQLITE_TRANSIENT);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            FileTransferRecord r;
            r.id = sqlite3_column_int64(stmt, 0);
            r.fileId = (const char*)sqlite3_column_text(stmt, 1);
            r.sessionId = sqlite3_column_text(stmt, 2) ? (const char*)sqlite3_column_text(stmt, 2) : "";
            r.account = (const char*)sqlite3_column_text(stmt, 3);
            r.sender = sqlite3_column_text(stmt, 4) ? (const char*)sqlite3_column_text(stmt, 4) : "";
            r.fileName = (const char*)sqlite3_column_text(stmt, 5);
            r.fileSize = sqlite3_column_int64(stmt, 6);
            r.fileMd5 = sqlite3_column_text(stmt, 7) ? (const char*)sqlite3_column_text(stmt, 7) : "";
            r.totalChunks = sqlite3_column_int(stmt, 8);
            r.chunkSize = sqlite3_column_int(stmt, 9);
            r.status = sqlite3_column_int(stmt, 10);
            r.createdAt = sqlite3_column_text(stmt, 11) ? (const char*)sqlite3_column_text(stmt, 11) : "";
            r.updatedAt = sqlite3_column_text(stmt, 12) ? (const char*)sqlite3_column_text(stmt, 12) : "";
            result.push_back(r);
        }
        sqlite3_finalize(stmt);
    }
    return result;
}

bool FileTransferRepo::updateStatus(const std::string& fileId, int status) {
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db || fileId.empty()) return false;

    const char* sql = "UPDATE file_transfer SET status = ?, updated_at = datetime('now','localtime') WHERE file_id = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;
    sqlite3_bind_int(stmt, 1, status);
    sqlite3_bind_text(stmt, 2, fileId.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

// --- file_chunk table ---

bool FileTransferRepo::upsertChunk(const std::string& fileId, int chunkIndex,
                                    const std::string& chunkData, const std::string& chunkMd5) {
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db || fileId.empty()) return false;

    const char* sql = "INSERT OR REPLACE INTO file_chunk (file_id, chunk_index, chunk_data, chunk_md5, status) "
                      "VALUES (?, ?, ?, ?, 1);";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, fileId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, chunkIndex);
    sqlite3_bind_text(stmt, 3, chunkData.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, chunkMd5.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

int FileTransferRepo::countReceivedChunks(const std::string& fileId) {
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db || fileId.empty()) return 0;

    const char* sql = "SELECT COUNT(*) FROM file_chunk WHERE file_id = ? AND status = 1;";
    sqlite3_stmt* stmt;
    int count = 0;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, fileId.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            count = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }
    return count;
}

std::vector<FileChunkRecord> FileTransferRepo::queryChunksByFileId(const std::string& fileId) {
    std::vector<FileChunkRecord> result;
    auto& conn = DbConnection::instance();
    sqlite3* db = conn.get();
    if (!db || fileId.empty()) return result;

    const char* sql = "SELECT id, file_id, chunk_index, chunk_data, chunk_md5, status, received_at "
                      "FROM file_chunk WHERE file_id = ? AND status = 1 ORDER BY chunk_index ASC;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, fileId.c_str(), -1, SQLITE_TRANSIENT);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            FileChunkRecord r;
            r.id = sqlite3_column_int64(stmt, 0);
            r.fileId = (const char*)sqlite3_column_text(stmt, 1);
            r.chunkIndex = sqlite3_column_int(stmt, 2);
            r.chunkData = sqlite3_column_text(stmt, 3) ? (const char*)sqlite3_column_text(stmt, 3) : "";
            r.chunkMd5 = sqlite3_column_text(stmt, 4) ? (const char*)sqlite3_column_text(stmt, 4) : "";
            r.status = sqlite3_column_int(stmt, 5);
            r.receivedAt = sqlite3_column_text(stmt, 6) ? (const char*)sqlite3_column_text(stmt, 6) : "";
            result.push_back(r);
        }
        sqlite3_finalize(stmt);
    }
    return result;
}

std::vector<int> FileTransferRepo::queryMissingChunkIndices(const std::string& fileId, int totalChunks) {
    std::vector<int> missing;
    auto chunks = queryChunksByFileId(fileId);

    std::vector<bool> received(totalChunks, false);
    for (const auto& c : chunks) {
        if (c.chunkIndex >= 0 && c.chunkIndex < totalChunks) {
            received[c.chunkIndex] = true;
        }
    }
    for (int i = 0; i < totalChunks; i++) {
        if (!received[i]) missing.push_back(i);
    }
    return missing;
}
