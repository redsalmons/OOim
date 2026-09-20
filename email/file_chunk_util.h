#ifndef FILE_CHUNK_UTIL_H
#define FILE_CHUNK_UTIL_H

// Protocol-agnostic file chunking helpers shared by the 1:1 (Signal) and 1:n (MLS)
// file transfer paths. Pipeline: whole-file compress -> fixed-size chunks -> (caller
// encrypts each chunk). Reassembly is the inverse: concat -> verify -> decompress -> verify.
// Nothing here touches Signal or MLS.

#include <string>
#include <vector>
#include <cstdint>

namespace filechunk {

constexpr int kDefaultChunkSize = 3 * 1024 * 1024;  // 3 MB per chunk (pre-base64)

struct PreparedFile {
    std::string fileName;
    int64_t     fileSize = 0;        // original size
    std::string fileMd5;             // original content MD5
    std::string compression;         // "zlib" or "none"
    int64_t     compressedSize = 0;  // size of the stream that is chunked
    std::string compressedMd5;       // MD5 of that stream ("" when compression=="none")
    int         chunkSize = kDefaultChunkSize;
    int         totalChunks = 0;
    std::string streamPath;          // file to read chunks from (temp file when compressed,
                                     // the original path otherwise)
    bool        streamIsTemp = false;
};

// Compress the file into a temp file (unless compression does not pay off), compute
// hashes and chunk count. Returns false on I/O error. Caller must call
// releasePrepared() when finished reading chunks.
bool prepareFileForSend(const std::string& filePath, const std::string& fileName,
                        PreparedFile& out, int chunkSize = kDefaultChunkSize);

// Remove the temp stream file, if any.
void releasePrepared(const PreparedFile& pf);

// Read chunk i (0-based) of the prepared stream. Empty on error.
std::vector<uint8_t> readChunk(const PreparedFile& pf, int chunkIndex);

// Metadata needed on the receiving side to rebuild the file.
struct ReassembleMeta {
    std::string fileMd5;
    std::string compression;    // "zlib" / "none" / "" (treated as none)
    std::string compressedMd5;  // may be empty
};

// Concatenate base64 chunks (already ordered by index), verify the stream MD5,
// decompress if needed, verify the file MD5 and write to outPath.
// Returns 0 on success, -1 write error, -2 stream md5 mismatch, -3 decompress
// failed, -4 file md5 mismatch.
int reassembleFile(const std::vector<std::string>& orderedChunksB64,
                   const ReassembleMeta& meta, const std::string& outPath);

} // namespace filechunk

#endif // FILE_CHUNK_UTIL_H
