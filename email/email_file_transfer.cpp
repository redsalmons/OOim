#include "email_core_common.h"
#include "email_core.h"
#include "logger.h"
#include "x_mailer.h"
#include "db_connection.h"
#include "persistence/file_transfer_repo.h"
#include "persistence/task_repo.h"
#include "persistence/session_repo.h"
#include "file_chunk_util.h"
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

// Generate a unique file_id
static std::string generate_file_id() {
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    return "file_" + std::to_string(ms) + "_" + std::to_string(rand() % 100000);
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
// Pipeline: whole-file compress (zlib, skipped when it does not pay off) -> 3 MB chunks
// -> one "file" metadata task (1.0.4) + N "truck" chunk tasks (1.0.5). Bodies are stored
// as plaintext JSON; the task processor Double-Ratchet-encrypts each one right before
// sending so ratchet order == send order. sessionId is the unified session id.
// Returns 0 on success, negative on error.
extern "C" int email_file_split_and_send(const char* filePath, const char* fileName,
                                          const char* account, const char* recipient,
                                          const char* sessionId, const char* inReplyTo,
                                          const char* subject, const char* text,
                                          const char* batchId,
                                          char* outJson, int outSize) {
    auto fail = [&](int rc, const char* err) {
        if (outJson && outSize > 0) snprintf(outJson, outSize, R"({"status":"failed","error":"%s"})", err);
        return rc;
    };
    if (!filePath || !fileName || !account || !recipient) return fail(-1, "null_parameter");

    std::string accountStr(account), recipientStr(recipient);
    std::string sessionIdStr(sessionId ? sessionId : "");
    std::string inReplyToStr(inReplyTo ? inReplyTo : "");
    std::string subjectStr(subject ? subject : "");
    std::string domain = accountStr.substr(accountStr.find('@') + 1);

    filechunk::PreparedFile pf;
    if (!filechunk::prepareFileForSend(filePath, fileName, pf)) return fail(-2, "file_not_found");

    std::string fileId = generate_file_id();
    LOG_INFO("email_file_split_and_send: file=%s, size=%lld, md5=%s, compression=%s, stream=%lld, chunks=%d, file_id=%s\n",
             pf.fileName.c_str(), (long long)pf.fileSize, pf.fileMd5.c_str(), pf.compression.c_str(),
             (long long)pf.compressedSize, pf.totalChunks, fileId.c_str());

    FileTransferRecord ftRec;
    ftRec.fileId = fileId;
    ftRec.sessionId = sessionIdStr;
    ftRec.account = accountStr;
    ftRec.sender = accountStr;
    ftRec.fileName = pf.fileName;
    ftRec.fileSize = pf.fileSize;
    ftRec.fileMd5 = pf.fileMd5;
    ftRec.totalChunks = pf.totalChunks;
    ftRec.chunkSize = pf.chunkSize;
    ftRec.status = 0;
    ftRec.originalPath = filePath;
    ftRec.compression = pf.compression;
    ftRec.compressedMd5 = pf.compressedMd5;
    if (!s_fileTransferRepo.insertFileTransfer(ftRec)) {
        filechunk::releasePrepared(pf);
        return fail(-4, "db_insert_failed");
    }

    // "file" metadata task (1.0.4). x_reply_to = last message of the conversation.
    std::string fileMsgId = "<file_" + fileId + "@" + domain + ">";
    {
        json meta = {
            {"msg_type", "file"}, {"file_id", fileId}, {"file_name", pf.fileName},
            {"file_size", pf.fileSize}, {"file_md5", pf.fileMd5},
            {"compression", pf.compression}, {"compressed_size", pf.compressedSize},
            {"compressed_md5", pf.compressedMd5},
            {"total_chunks", pf.totalChunks}, {"chunk_size", pf.chunkSize},
            {"text", text ? text : ""}, {"batch_id", batchId ? batchId : ""},
            {"x_message_id", fileMsgId}, {"x_reply_to", inReplyToStr},
        };
        s_taskRepo.insert(accountStr, recipientStr, subjectStr, meta.dump(),
                          inReplyToStr, fileMsgId, fileMsgId, sessionIdStr, XMailer::ATTACH_META);
        LOG_INFO("email_file_split_and_send: created file metadata task (1.0.4), msg_id=%s\n", fileMsgId.c_str());
    }

    // "truck" chunk tasks (1.0.5). Chain: truck_0 -> file meta, truck_i -> truck_(i-1).
    std::string prevMsgId = fileMsgId;
    int queued = 0;
    for (int i = 0; i < pf.totalChunks; i++) {
        auto chunk = filechunk::readChunk(pf, i);
        if (chunk.empty() && i < pf.totalChunks - 1) {
            LOG_INFO("email_file_split_and_send: failed to read chunk %d\n", i);
            continue;
        }
        std::string truckMsgId = "<truck_" + fileId + "_" + std::to_string(i) + "@" + domain + ">";
        json truck = {
            {"msg_type", "truck"}, {"file_id", fileId}, {"chunk_index", i},
            {"chunk_data", base64_encode(chunk.data(), chunk.size())},
            {"chunk_md5", compute_md5(std::string(chunk.begin(), chunk.end()))},
            {"x_message_id", truckMsgId}, {"x_reply_to", prevMsgId},
        };
        s_taskRepo.insert(accountStr, recipientStr, subjectStr, truck.dump(),
                          prevMsgId, truckMsgId, truckMsgId, sessionIdStr, XMailer::ATTACH_CHUNK);
        prevMsgId = truckMsgId;
        queued++;
    }
    filechunk::releasePrepared(pf);

    if (outJson && outSize > 0) {
        json resp = {{"status", "success"}, {"file_id", fileId}, {"file_name", pf.fileName},
                     {"file_size", pf.fileSize}, {"file_md5", pf.fileMd5},
                     {"compression", pf.compression}, {"total_chunks", pf.totalChunks},
                     {"chunk_size", pf.chunkSize}, {"message_id", fileMsgId}};
        snprintf(outJson, outSize, "%s", resp.dump().c_str());
    }
    LOG_INFO("email_file_split_and_send: success, file_id=%s, total_tasks=%d\n", fileId.c_str(), queued + 1);
    return 0;
}

// Process a received "file" message: create file_transfer record on receiver side.
// Called after decryption when msg_type == "file".
extern "C" int email_file_transfer_receive_file(const char* fileId, const char* sessionId,
                                                  const char* account, const char* sender,
                                                  const char* fileName, int64_t fileSize,
                                                  const char* fileMd5, int totalChunks, int chunkSize,
                                                  const char* messageId,
                                                  const char* compression, const char* compressedMd5,
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
    rec.compression = compression ? compression : "";
    rec.compressedMd5 = compressedMd5 ? compressedMd5 : "";

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
        auto chunks = s_fileTransferRepo.queryChunksByFileId(fileIdStr);
        std::vector<std::string> ordered;
        for (const auto& c : chunks) ordered.push_back(c.chunkData);
        std::string outPath = std::string(outputDir) + "/" + ftRec.fileName;
        int rrc = filechunk::reassembleFile(ordered, {ftRec.fileMd5, ftRec.compression, ftRec.compressedMd5}, outPath);
        if (rrc == 0) {
            LOG_INFO("email_file_transfer_receive_truck: file reassembled successfully at %s\n", outPath.c_str());
            s_fileTransferRepo.updateStatus(fileIdStr, 1);
        } else {
            LOG_INFO("email_file_transfer_receive_truck: reassemble failed rc=%d for %s\n", rrc, outPath.c_str());
            s_fileTransferRepo.updateStatus(fileIdStr, 2);
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
    std::vector<std::string> ordered;
    for (const auto& c : chunks) ordered.push_back(c.chunkData);
    std::string outPath = std::string(outputDir) + "/" + rec.fileName;
    int rrc = filechunk::reassembleFile(ordered, {rec.fileMd5, rec.compression, rec.compressedMd5}, outPath);
    bool md5Ok = (rrc == 0);
    s_fileTransferRepo.updateStatus(fileId, md5Ok ? 1 : 2);

    json resp;
    resp["status"] = md5Ok ? "success" : "failed";
    resp["file_id"] = fileId;
    resp["file_name"] = rec.fileName;
    resp["output_path"] = outPath;
    resp["md5_match"] = md5Ok;
    resp["expected_md5"] = rec.fileMd5;
    resp["reassemble_rc"] = rrc;
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
