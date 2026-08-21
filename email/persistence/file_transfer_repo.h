#ifndef PERSISTENCE_FILE_TRANSFER_REPO_H
#define PERSISTENCE_FILE_TRANSFER_REPO_H

#include <string>
#include <cstdint>
#include <vector>

struct FileTransferRecord {
    int64_t id = 0;
    std::string fileId;
    std::string sessionId;
    std::string account;
    std::string sender;
    std::string fileName;
    int64_t fileSize = 0;
    std::string fileMd5;
    int totalChunks = 0;
    int chunkSize = 0;
    int status = 0;  // 0=pending, 1=complete, 2=failed
    std::string createdAt;
    std::string updatedAt;
};

struct FileChunkRecord {
    int64_t id = 0;
    std::string fileId;
    int chunkIndex = 0;
    std::string chunkData;
    std::string chunkMd5;
    int status = 0;  // 0=pending, 1=received
    std::string receivedAt;
};

class FileTransferRepo {
public:
    // Insert a file_transfer record. Returns true on success.
    bool insertFileTransfer(const FileTransferRecord& rec);

    // Query file_transfer by file_id. Returns true if found.
    bool queryByFileId(const std::string& fileId, FileTransferRecord& out);

    // Query pending (status=0) file_transfers for an account.
    std::vector<FileTransferRecord> queryPendingByAccount(const std::string& account);

    // Update file_transfer status.
    bool updateStatus(const std::string& fileId, int status);

    // Insert or replace a file_chunk. Returns true on success.
    bool upsertChunk(const std::string& fileId, int chunkIndex,
                     const std::string& chunkData, const std::string& chunkMd5);

    // Count received chunks (status=1) for a file_id.
    int countReceivedChunks(const std::string& fileId);

    // Query all received chunks for a file_id, ordered by chunk_index ASC.
    std::vector<FileChunkRecord> queryChunksByFileId(const std::string& fileId);

    // Query missing chunk indices for a file_id (based on total_chunks).
    std::vector<int> queryMissingChunkIndices(const std::string& fileId, int totalChunks);
};

#endif // PERSISTENCE_FILE_TRANSFER_REPO_H
