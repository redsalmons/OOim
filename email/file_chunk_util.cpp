#include "file_chunk_util.h"
#include "email_core_common.h"
#include "logger.h"
#include <fstream>
#include <filesystem>
#include <chrono>
#include <random>

namespace fs = std::filesystem;

namespace filechunk {

namespace {

// Compression must save at least this fraction of the original size to be kept.
constexpr double kMinCompressionGain = 0.05;

std::vector<uint8_t> readAll(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) return {};
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

std::string md5Of(const std::vector<uint8_t>& data) {
    return compute_md5(std::string(data.begin(), data.end()));
}

std::string tempStreamPath() {
    static std::mt19937_64 rng{std::random_device{}()};
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    return (fs::temp_directory_path() / ("oim_fx_" + std::to_string(ms) + "_" + std::to_string(rng() % 100000000) + ".z")).string();
}

} // namespace

bool prepareFileForSend(const std::string& filePath, const std::string& fileName,
                        PreparedFile& out, int chunkSize) {
    out = PreparedFile{};
    out.fileName = fileName;
    out.chunkSize = chunkSize > 0 ? chunkSize : kDefaultChunkSize;

    auto original = readAll(filePath);
    if (original.empty() && !fs::exists(filePath)) {
        LOG_INFO("[filechunk] cannot read %s\n", filePath.c_str());
        return false;
    }
    out.fileSize = (int64_t)original.size();
    out.fileMd5 = md5Of(original);

    auto compressed = zlib_compress(original);
    bool useCompressed = !compressed.empty() &&
        (double)compressed.size() <= (double)original.size() * (1.0 - kMinCompressionGain);

    if (useCompressed) {
        std::string tmp = tempStreamPath();
        std::ofstream o(tmp, std::ios::binary | std::ios::trunc);
        if (!o.is_open()) {
            LOG_INFO("[filechunk] cannot create temp stream %s\n", tmp.c_str());
            return false;
        }
        o.write(reinterpret_cast<const char*>(compressed.data()), (std::streamsize)compressed.size());
        o.close();
        out.compression = "zlib";
        out.compressedSize = (int64_t)compressed.size();
        out.compressedMd5 = md5Of(compressed);
        out.streamPath = tmp;
        out.streamIsTemp = true;
    } else {
        out.compression = "none";
        out.compressedSize = out.fileSize;
        out.compressedMd5 = out.fileMd5;
        out.streamPath = filePath;
        out.streamIsTemp = false;
    }

    out.totalChunks = (int)((out.compressedSize + out.chunkSize - 1) / out.chunkSize);
    if (out.totalChunks == 0) out.totalChunks = 1;

    LOG_INFO("[filechunk] prepared %s: size=%lld compression=%s stream=%lld chunks=%d\n",
             fileName.c_str(), (long long)out.fileSize, out.compression.c_str(),
             (long long)out.compressedSize, out.totalChunks);
    return true;
}

void releasePrepared(const PreparedFile& pf) {
    if (pf.streamIsTemp && !pf.streamPath.empty()) {
        std::error_code ec;
        fs::remove(pf.streamPath, ec);
    }
}

std::vector<uint8_t> readChunk(const PreparedFile& pf, int chunkIndex) {
    std::ifstream in(pf.streamPath, std::ios::binary);
    if (!in.is_open()) return {};
    in.seekg((std::streamoff)chunkIndex * pf.chunkSize);
    std::vector<uint8_t> buf(pf.chunkSize);
    in.read(reinterpret_cast<char*>(buf.data()), pf.chunkSize);
    buf.resize((size_t)in.gcount());
    return buf;
}

int reassembleFile(const std::vector<std::string>& orderedChunksB64,
                   const ReassembleMeta& meta, const std::string& outPath) {
    std::vector<uint8_t> stream;
    for (const auto& b64 : orderedChunksB64) {
        auto bytes = base64_decode(b64);
        stream.insert(stream.end(), bytes.begin(), bytes.end());
    }

    if (!meta.compressedMd5.empty() && md5Of(stream) != meta.compressedMd5) {
        LOG_INFO("[filechunk] stream md5 mismatch for %s\n", outPath.c_str());
        return -2;
    }

    std::vector<uint8_t> content;
    if (meta.compression == "zlib") {
        content = zlib_decompress(stream);
        if (content.empty() && !stream.empty()) {
            LOG_INFO("[filechunk] decompress failed for %s\n", outPath.c_str());
            return -3;
        }
    } else {
        content = std::move(stream);
    }

    if (!meta.fileMd5.empty() && md5Of(content) != meta.fileMd5) {
        LOG_INFO("[filechunk] file md5 mismatch for %s\n", outPath.c_str());
        return -4;
    }

    std::ofstream o(outPath, std::ios::binary | std::ios::trunc);
    if (!o.is_open()) return -1;
    o.write(reinterpret_cast<const char*>(content.data()), (std::streamsize)content.size());
    return 0;
}

} // namespace filechunk
