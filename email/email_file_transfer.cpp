#include "email_core_common.h"
#include "email_core.h"
#include "logger.h"
#include "x_mailer.h"
#include "db_connection.h"
#include "persistence/file_transfer_repo.h"
#include "persistence/task_repo.h"
#include "persistence/session_repo.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <sstream>
#include <chrono>
#include <vector>

using json = nlohmann::json;

static FileTransferRepo s_fileTransferRepo;
static TaskRepo s_taskRepo;

// Default chunk size: 3 MB (can be adjusted)
static const int DEFAULT_CHUNK_SIZE = 3 * 1024 * 1024; // 3 MB

// Generate a unique file_id
static std::string generate_file_id() {
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    return "file_" + std::to_string(ms) + "_" + std::to_string(rand() % 100000);
}

// Compute MD5 of a file's contents
static std::string compute_file_md5(const std::string& filePath) {
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) return "";

    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
    file.close();
    return compute_md5(content);
}

// Read a specific chunk from a file
static std::vector<uint8_t> read_file_chunk(const std::string& filePath, int chunkIndex, int chunkSize) {
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) return {};

    file.seekg((std::streamoff)chunkIndex * chunkSize, std::ios::beg);
    std::vector<uint8_t> buffer(chunkSize);
    file.read(reinterpret_cast<char*>(buffer.data()), chunkSize);
    std::streamsize bytesRead = file.gcount();
    buffer.resize(bytesRead);
    return buffer;
}

// Get file size
static int64_t get_file_size(const std::string& filePath) {
    std::ifstream file(filePath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return -1;
    return file.tellg();
}

// --- C API functions ---

// Prepare file metadata message JSON (plaintext, to be encrypted by email_prepare_data_body)
// Returns the JSON string in outJson.
extern "C" int email_prepare_file_message(const char* fileId, const char* fileName,
                                           int64_t fileSize, const char* fileMd5,
                                           int totalChunks, int chunkSize,
                                           const char* text, const char* batchId,
                                           char* outJson, int outSize) {
    if (!fileId || !fileName || !outJson || outSize <= 0) return -1;

    json msg;
    msg["msg_type"] = "file";
    msg["file_id"] = fileId;
    msg["file_name"] = fileName;
    msg["file_size"] = fileSize;
    msg["file_md5"] = fileMd5 ? fileMd5 : "";
    msg["total_chunks"] = totalChunks;
    msg["chunk_size"] = chunkSize;
    msg["text"] = text ? text : "";
    msg["batch_id"] = batchId ? batchId : "";

    std::string result = msg.dump();
    if ((int)result.size() >= outSize) return -2;
    snprintf(outJson, outSize, "%s", result.c_str());
    return 0;
}

// Prepare chunk message JSON (plaintext, to be encrypted by email_prepare_data_body)
// Returns the JSON string in outJson.
extern "C" int email_prepare_truck_message(const char* fileId, int chunkIndex,
                                            const char* chunkDataB64, const char* chunkMd5,
                                            char* outJson, int outSize) {
    if (!fileId || !chunkDataB64 || !outJson || outSize <= 0) return -1;

    json msg;
    msg["msg_type"] = "truck";
    msg["file_id"] = fileId;
    msg["chunk_index"] = chunkIndex;
    msg["chunk_data"] = chunkDataB64;
    msg["chunk_md5"] = chunkMd5 ? chunkMd5 : "";

    std::string result = msg.dump();
    if ((int)result.size() >= outSize) return -2;
    snprintf(outJson, outSize, "%s", result.c_str());
    return 0;
}

// Split a file into chunks and create send tasks for each chunk.
// This creates:
//   1. A "file" metadata message task
//   2. N "truck" chunk message tasks
// Each task is inserted into the task table for async sending.
// Returns 0 on success, negative on error.
extern "C" int email_file_split_and_send(const char* filePath, const char* fileName,
                                          const char* account, const char* recipient,
                                          const char* sessionId, const char* inReplyTo,
                                          const char* subject, const char* text,
                                          const char* batchId,
                                          char* outJson, int outSize) {
    if (!filePath || !fileName || !account || !recipient) {
        if (outJson && outSize > 0) {
            snprintf(outJson, outSize, R"({"status":"failed","error":"null_parameter"})");
        }
        return -1;
    }

    std::string filePathStr(filePath);
    std::string fileNameStr(fileName);
    std::string accountStr(account);
    std::string recipientStr(recipient);
    std::string sessionIdStr(sessionId ? sessionId : "");
    std::string inReplyToStr(inReplyTo ? inReplyTo : "");
    std::string subjectStr(subject ? subject : "");

    // Get file size
    int64_t fileSize = get_file_size(filePathStr);
    if (fileSize < 0) {
        LOG_INFO("email_file_split_and_send: cannot get file size for %s\n", filePathStr.c_str());
        if (outJson && outSize > 0) {
            snprintf(outJson, outSize, R"({"status":"failed","error":"file_not_found"})");
        }
        return -2;
    }

    // Compute file MD5
    std::string fileMd5 = compute_file_md5(filePathStr);
    if (fileMd5.empty()) {
        LOG_INFO("email_file_split_and_send: cannot compute MD5 for %s\n", filePathStr.c_str());
        if (outJson && outSize > 0) {
            snprintf(outJson, outSize, R"({"status":"failed","error":"md5_failed"})");
        }
        return -3;
    }

    // Calculate chunks
    int chunkSize = DEFAULT_CHUNK_SIZE;
    int totalChunks = (int)((fileSize + chunkSize - 1) / chunkSize);
    if (totalChunks == 0) totalChunks = 1;

    // Generate file_id
    std::string fileId = generate_file_id();

    LOG_INFO("email_file_split_and_send: file=%s, size=%lld, md5=%s, chunks=%d, chunk_size=%d, file_id=%s\n",
             fileNameStr.c_str(), (long long)fileSize, fileMd5.c_str(), totalChunks, chunkSize, fileId.c_str());

    // Insert file_transfer record
    FileTransferRecord ftRec;
    ftRec.fileId = fileId;
    ftRec.sessionId = sessionIdStr;
    ftRec.account = accountStr;
    ftRec.sender = accountStr;
    ftRec.fileName = fileNameStr;
    ftRec.fileSize = fileSize;
    ftRec.fileMd5 = fileMd5;
    ftRec.totalChunks = totalChunks;
    ftRec.chunkSize = chunkSize;
    ftRec.status = 0; // Pending until confirmed via INBOX
    ftRec.originalPath = filePathStr;

    if (!s_fileTransferRepo.insertFileTransfer(ftRec)) {
        LOG_INFO("email_file_split_and_send: failed to insert file_transfer record\n");
        if (outJson && outSize > 0) {
            snprintf(outJson, outSize, R"({"status":"failed","error":"db_insert_failed"})");
        }
        return -4;
    }

    // Create "file" metadata message task (X-Mailer=0.1.3)
    std::string fileMsgId;
    {
        char fileMsgJson[8192];
        int rc = email_prepare_file_message(fileId.c_str(), fileNameStr.c_str(),
                                            fileSize, fileMd5.c_str(),
                                            totalChunks, chunkSize,
                                            text ? text : "",
                                            batchId ? batchId : "",
                                            fileMsgJson, sizeof(fileMsgJson));
        if (rc != 0) {
            LOG_INFO("email_file_split_and_send: failed to prepare file message, rc=%d\n", rc);
        } else {
            std::string domain = accountStr.substr(accountStr.find('@') + 1);
            fileMsgId = "<file_" + fileId + "@" + domain + ">";
            s_taskRepo.insert(accountStr, recipientStr, subjectStr, fileMsgJson,
                              inReplyToStr, fileMsgId, fileMsgId, sessionIdStr, XMailer::FILE_META);
            LOG_INFO("email_file_split_and_send: created file metadata task (0.1.3), msg_id=%s\n", fileMsgId.c_str());
        }
    }

    // Create "truck" chunk message tasks (X-Mailer=0.1.4)
    // Chain: truck_0.last_message_id → file metadata, truck_i.last_message_id → truck_(i-1)
    std::string prevMsgId = fileMsgId; // First truck points to file metadata
    for (int i = 0; i < totalChunks; i++) {
        auto chunkData = read_file_chunk(filePathStr, i, chunkSize);
        if (chunkData.empty() && i < totalChunks - 1) {
            LOG_INFO("email_file_split_and_send: failed to read chunk %d\n", i);
            continue;
        }

        std::string chunkB64 = base64_encode(chunkData.data(), chunkData.size());
        std::string chunkMd5 = compute_md5(std::string(chunkData.begin(), chunkData.end()));

        size_t truckBufSize = chunkB64.size() + 4096;
        std::vector<char> truckMsgJson(truckBufSize);
        int rc = email_prepare_truck_message(fileId.c_str(), i,
                                             chunkB64.c_str(), chunkMd5.c_str(),
                                             truckMsgJson.data(), (int)truckMsgJson.size());
        if (rc != 0) {
            LOG_INFO("email_file_split_and_send: failed to prepare truck message %d, rc=%d\n", i, rc);
            continue;
        }

        std::string domain = accountStr.substr(accountStr.find('@') + 1);
        std::string truckMsgId = "<truck_" + fileId + "_" + std::to_string(i) + "@" + domain + ">";
        s_taskRepo.insert(accountStr, recipientStr, subjectStr, truckMsgJson.data(),
                          prevMsgId, truckMsgId, truckMsgId, sessionIdStr, XMailer::FILE_CHUNK);
        LOG_INFO("email_file_split_and_send: created chunk %d task (0.1.4), msg_id=%s, last_msg_id=%s\n",
                 i, truckMsgId.c_str(), prevMsgId.c_str());
        prevMsgId = truckMsgId; // Next truck points to this one
    }

    // Build response
    if (outJson && outSize > 0) {
        json resp;
        resp["status"] = "success";
        resp["file_id"] = fileId;
        resp["file_name"] = fileNameStr;
        resp["file_size"] = fileSize;
        resp["file_md5"] = fileMd5;
        resp["total_chunks"] = totalChunks;
        resp["chunk_size"] = chunkSize;
        std::string jsonStr = resp.dump();
        snprintf(outJson, outSize, "%s", jsonStr.c_str());
    }

    LOG_INFO("email_file_split_and_send: success, file_id=%s, total_tasks=%d\n",
             fileId.c_str(), totalChunks + 1);
    return 0;
}

// Process a received "file" message: create file_transfer record on receiver side.
// Called after decryption when msg_type == "file".
extern "C" int email_file_transfer_receive_file(const char* fileId, const char* sessionId,
                                                  const char* account, const char* sender,
                                                  const char* fileName, int64_t fileSize,
                                                  const char* fileMd5, int totalChunks, int chunkSize,
                                                  const char* messageId,
                                                  char* outJson, int outSize) {
    if (!fileId || !account || !fileName) {
        if (outJson && outSize > 0) {
            snprintf(outJson, outSize, R"({"status":"failed","error":"null_parameter"})");
        }
        return -1;
    }

    std::string fileIdStr(fileId);
    std::string messageIdStr(messageId ? messageId : "");

    // Check if already exists
    FileTransferRecord existing;
    if (s_fileTransferRepo.queryByFileId(fileIdStr, existing)) {
        LOG_INFO("email_file_transfer_receive_file: file_id=%s already exists, skipping\n", fileIdStr.c_str());

        // Update message_id if provided (it may be empty for the original send record)
        if (!messageIdStr.empty()) {
            s_fileTransferRepo.updateMessageId(fileIdStr, messageIdStr);
            LOG_INFO("email_file_transfer_receive_file: updated message_id=%s for file_id=%s\n",
                     messageIdStr.c_str(), fileIdStr.c_str());
        }

        // If this is the sender downloading their own sent file message, mark as complete
        std::string accountStr(account ? account : "");
        std::string senderStr(sender ? sender : "");
        if (!accountStr.empty() &&
            existing.sender == accountStr &&
            senderStr == accountStr) {
            LOG_INFO("email_file_transfer_receive_file: sender %s downloading own sent message, updating status to complete\n", accountStr.c_str());
            s_fileTransferRepo.updateStatus(fileIdStr, 1);
        }

        if (outJson && outSize > 0) {
            json resp;
            resp["status"] = "success";
            resp["file_id"] = fileIdStr;
            resp["exists"] = true;
            std::string jsonStr = resp.dump();
            snprintf(outJson, outSize, "%s", jsonStr.c_str());
        }
        return 0;
    }

    FileTransferRecord rec;
    rec.fileId = fileIdStr;
    rec.sessionId = sessionId ? sessionId : "";
    rec.account = account;
    rec.sender = sender ? sender : "";
    rec.fileName = fileName;
    rec.fileSize = fileSize;
    rec.fileMd5 = fileMd5 ? fileMd5 : "";
    rec.totalChunks = totalChunks;
    rec.chunkSize = chunkSize;
    rec.messageId = messageIdStr;
    rec.status = 0;

    if (!s_fileTransferRepo.insertFileTransfer(rec)) {
        LOG_INFO("email_file_transfer_receive_file: failed to insert record for file_id=%s\n", fileIdStr.c_str());
        if (outJson && outSize > 0) {
            snprintf(outJson, outSize, R"({"status":"failed","error":"db_insert_failed"})");
        }
        return -2;
    }

    LOG_INFO("email_file_transfer_receive_file: created record for file_id=%s, name=%s, chunks=%d, message_id=%s\n",
             fileIdStr.c_str(), fileName, totalChunks, messageIdStr.c_str());

    if (outJson && outSize > 0) {
        json resp;
        resp["status"] = "success";
        resp["file_id"] = fileIdStr;
        resp["exists"] = false;
        std::string jsonStr = resp.dump();
        snprintf(outJson, outSize, "%s", jsonStr.c_str());
    }
    return 0;
}

// Process a received "truck" message: store chunk data.
// Called after decryption when msg_type == "truck".
// If all chunks received, attempts reassembly.
extern "C" int email_file_transfer_receive_truck(const char* fileId, int chunkIndex,
                                                   const char* chunkDataB64, const char* chunkMd5,
                                                   const char* outputDir,
                                                   char* outJson, int outSize) {
    if (!fileId || !chunkDataB64) {
        if (outJson && outSize > 0) {
            snprintf(outJson, outSize, R"({"status":"failed","error":"null_parameter"})");
        }
        return -1;
    }

    std::string fileIdStr(fileId);
    std::string chunkDataStr(chunkDataB64);
    std::string chunkMd5Str(chunkMd5 ? chunkMd5 : "");

    // Verify chunk MD5
    auto decoded = base64_decode(chunkDataStr);
    std::string calculatedMd5 = compute_md5(std::string(decoded.begin(), decoded.end()));
    if (!chunkMd5Str.empty() && calculatedMd5 != chunkMd5Str) {
        LOG_INFO("email_file_transfer_receive_truck: MD5 mismatch for chunk %d, expected=%s, got=%s\n",
                 chunkIndex, chunkMd5Str.c_str(), calculatedMd5.c_str());
        if (outJson && outSize > 0) {
            snprintf(outJson, outSize, R"({"status":"failed","error":"md5_mismatch"})");
        }
        return -2;
    }

    // Store chunk
    if (!s_fileTransferRepo.upsertChunk(fileIdStr, chunkIndex, chunkDataStr, chunkMd5Str)) {
        LOG_INFO("email_file_transfer_receive_truck: failed to store chunk %d for file_id=%s\n",
                 chunkIndex, fileIdStr.c_str());
        if (outJson && outSize > 0) {
            snprintf(outJson, outSize, R"({"status":"failed","error":"db_insert_failed"})");
        }
        return -3;
    }

    LOG_INFO("email_file_transfer_receive_truck: stored chunk %d for file_id=%s\n",
             chunkIndex, fileIdStr.c_str());

    // Check if all chunks received
    FileTransferRecord ftRec;
    if (!s_fileTransferRepo.queryByFileId(fileIdStr, ftRec)) {
        LOG_INFO("email_file_transfer_receive_truck: no file_transfer record for file_id=%s\n",
                 fileIdStr.c_str());
        if (outJson && outSize > 0) {
            snprintf(outJson, outSize, R"({"status":"success","complete":false})");
        }
        return 0;
    }

    int receivedCount = s_fileTransferRepo.countReceivedChunks(fileIdStr);
    bool complete = (receivedCount >= ftRec.totalChunks);

    LOG_INFO("email_file_transfer_receive_truck: received=%d/%d for file_id=%s, complete=%d\n",
             receivedCount, ftRec.totalChunks, fileIdStr.c_str(), complete);

    if (complete && outputDir && outputDir[0]) {
        // Reassemble file
        auto chunks = s_fileTransferRepo.queryChunksByFileId(fileIdStr);
        std::string outPath = std::string(outputDir) + "/" + ftRec.fileName;

        std::ofstream outFile(outPath, std::ios::binary);
        if (!outFile.is_open()) {
            LOG_INFO("email_file_transfer_receive_truck: cannot open output file %s\n", outPath.c_str());
        } else {
            for (const auto& chunk : chunks) {
                auto chunkBytes = base64_decode(chunk.chunkData);
                outFile.write(reinterpret_cast<const char*>(chunkBytes.data()), chunkBytes.size());
            }
            outFile.close();

            // Verify file MD5
            std::string actualMd5 = compute_file_md5(outPath);
            if (!ftRec.fileMd5.empty() && actualMd5 != ftRec.fileMd5) {
                LOG_INFO("email_file_transfer_receive_truck: file MD5 mismatch! expected=%s, got=%s\n",
                         ftRec.fileMd5.c_str(), actualMd5.c_str());
                s_fileTransferRepo.updateStatus(fileIdStr, 2);  // failed
            } else {
                LOG_INFO("email_file_transfer_receive_truck: file reassembled successfully at %s\n",
                         outPath.c_str());
                s_fileTransferRepo.updateStatus(fileIdStr, 1);  // complete
            }
        }
    }

    if (outJson && outSize > 0) {
        json resp;
        resp["status"] = "success";
        resp["file_id"] = fileIdStr;
        resp["chunk_index"] = chunkIndex;
        resp["received_count"] = receivedCount;
        resp["total_chunks"] = ftRec.totalChunks;
        resp["complete"] = complete;
        std::string jsonStr = resp.dump();
        snprintf(outJson, outSize, "%s", jsonStr.c_str());
    }
    return 0;
}

// Query file transfer status by file_id
extern "C" int email_file_transfer_query(const char* fileId, char* outJson, int outSize) {
    if (!fileId || !outJson || outSize <= 0) return -1;

    FileTransferRecord rec;
    if (!s_fileTransferRepo.queryByFileId(fileId, rec)) {
        snprintf(outJson, outSize, R"({"status":"failed","error":"not_found"})");
        return -2;
    }

    int receivedCount = s_fileTransferRepo.countReceivedChunks(fileId);

    json resp;
    resp["status"] = "success";
    resp["file_id"] = rec.fileId;
    resp["session_id"] = rec.sessionId;
    resp["account"] = rec.account;
    resp["sender"] = rec.sender;
    resp["file_name"] = rec.fileName;
    resp["file_size"] = rec.fileSize;
    resp["file_md5"] = rec.fileMd5;
    resp["total_chunks"] = rec.totalChunks;
    resp["chunk_size"] = rec.chunkSize;
    resp["transfer_status"] = rec.status;
    resp["received_chunks"] = receivedCount;
    resp["message_id"] = rec.messageId;
    resp["created_at"] = rec.createdAt;
    resp["updated_at"] = rec.updatedAt;

    std::string jsonStr = resp.dump();
    snprintf(outJson, outSize, "%s", jsonStr.c_str());
    return 0;
}

// Query all pending file transfers for an account
extern "C" int email_file_transfer_query_pending(const char* account, char* outJson, int outSize) {
    if (!account || !outJson || outSize <= 0) return -1;

    auto records = s_fileTransferRepo.queryPendingByAccount(account);

    json arr = json::array();
    for (const auto& rec : records) {
        int receivedCount = s_fileTransferRepo.countReceivedChunks(rec.fileId);
        json item;
        item["file_id"] = rec.fileId;
        item["session_id"] = rec.sessionId;
        item["file_name"] = rec.fileName;
        item["file_size"] = rec.fileSize;
        item["file_md5"] = rec.fileMd5;
        item["total_chunks"] = rec.totalChunks;
        item["received_chunks"] = receivedCount;
        item["transfer_status"] = rec.status;
        item["message_id"] = rec.messageId;
        item["created_at"] = rec.createdAt;
        arr.push_back(item);
    }

    json resp;
    resp["status"] = "success";
    resp["count"] = records.size();
    resp["transfers"] = arr;

    std::string jsonStr = resp.dump();
    snprintf(outJson, outSize, "%s", jsonStr.c_str());
    return 0;
}

// Reassemble a file from received chunks (manual trigger)
extern "C" int email_file_transfer_reassemble(const char* fileId, const char* outputDir,
                                               char* outJson, int outSize) {
    if (!fileId || !outputDir || !outJson || outSize <= 0) return -1;

    FileTransferRecord rec;
    if (!s_fileTransferRepo.queryByFileId(fileId, rec)) {
        snprintf(outJson, outSize, R"({"status":"failed","error":"not_found"})");
        return -1;
    }

    int receivedCount = s_fileTransferRepo.countReceivedChunks(fileId);
    if (receivedCount < rec.totalChunks) {
        json resp;
        resp["status"] = "failed";
        resp["error"] = "incomplete";
        resp["received"] = receivedCount;
        resp["total"] = rec.totalChunks;
        std::string jsonStr = resp.dump();
        snprintf(outJson, outSize, "%s", jsonStr.c_str());
        return -2;
    }

    auto chunks = s_fileTransferRepo.queryChunksByFileId(fileId);
    std::string outPath = std::string(outputDir) + "/" + rec.fileName;

    std::ofstream outFile(outPath, std::ios::binary);
    if (!outFile.is_open()) {
        snprintf(outJson, outSize, R"({"status":"failed","error":"cannot_open_output"})");
        return -3;
    }

    for (const auto& chunk : chunks) {
        auto chunkBytes = base64_decode(chunk.chunkData);
        outFile.write(reinterpret_cast<const char*>(chunkBytes.data()), chunkBytes.size());
    }
    outFile.close();

    // Verify MD5
    std::string actualMd5 = compute_file_md5(outPath);
    bool md5Ok = rec.fileMd5.empty() || actualMd5 == rec.fileMd5;

    if (md5Ok) {
        s_fileTransferRepo.updateStatus(fileId, 1);
    } else {
        s_fileTransferRepo.updateStatus(fileId, 2);
    }

    json resp;
    resp["status"] = md5Ok ? "success" : "failed";
    resp["file_id"] = fileId;
    resp["file_name"] = rec.fileName;
    resp["output_path"] = outPath;
    resp["md5_match"] = md5Ok;
    resp["expected_md5"] = rec.fileMd5;
    resp["actual_md5"] = actualMd5;
    std::string jsonStr = resp.dump();
    snprintf(outJson, outSize, "%s", jsonStr.c_str());

    LOG_INFO("email_file_transfer_reassemble: file_id=%s, output=%s, md5_ok=%d\n",
             fileId, outPath.c_str(), md5Ok);
    return md5Ok ? 0 : -4;
}

// Copy a sent file from its original path to the output directory (for sender Save As)
extern "C" int email_file_transfer_copy_original(const char* fileId, const char* outputDir,
                                                  char* outJson, int outSize) {
    if (!fileId || !outputDir || !outJson || outSize <= 0) return -1;

    FileTransferRecord rec;
    if (!s_fileTransferRepo.queryByFileId(fileId, rec)) {
        snprintf(outJson, outSize, R"({"status":"failed","error":"not_found"})");
        return -1;
    }

    if (rec.originalPath.empty()) {
        snprintf(outJson, outSize, R"({"status":"failed","error":"no_original_path"})");
        return -2;
    }

    std::filesystem::path srcPath(rec.originalPath);
    if (!std::filesystem::exists(srcPath)) {
        snprintf(outJson, outSize, R"({"status":"failed","error":"original_file_not_found"})");
        return -3;
    }

    std::string outPath = std::string(outputDir) + "/" + rec.fileName;
    try {
        if (std::filesystem::is_directory(srcPath)) {
            // Remove existing destination directory first (overwrite)
            std::filesystem::remove_all(outPath);
            std::filesystem::copy(srcPath, outPath,
                std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing);
        } else {
            std::filesystem::copy_file(srcPath, outPath, std::filesystem::copy_options::overwrite_existing);
        }
    } catch (const std::exception& e) {
        snprintf(outJson, outSize, R"({"status":"failed","error":"copy_failed"})");
        return -4;
    }

    json resp;
    resp["status"] = "success";
    resp["file_id"] = fileId;
    resp["file_name"] = rec.fileName;
    resp["output_path"] = outPath;
    std::string jsonStr = resp.dump();
    snprintf(outJson, outSize, "%s", jsonStr.c_str());

    LOG_INFO("email_file_transfer_copy_original: file_id=%s, output=%s\n",
             fileId, outPath.c_str());
    return 0;
}
