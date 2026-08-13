#include "compress_zlib.h"
#include <zlib.h>
#include <stdexcept>
#include <memory>

namespace oemail {

class CompressZlib::Impl {
public:
    Impl() : compression_level_(6) {}
    
    std::vector<uint8_t> compress(const std::vector<uint8_t>& data) {
        if (data.empty()) {
            last_error_ = "Input data is empty";
            return {};
        }
        
        // Estimate compressed size (worst case)
        uLongf compressed_size = compressBound(data.size());
        std::vector<uint8_t> compressed(compressed_size);
        
        // Compress data
        int result = compress2(compressed.data(), &compressed_size,
                              data.data(), data.size(),
                              compression_level_);
        
        if (result != Z_OK) {
            last_error_ = "Compression failed with error: " + std::to_string(result);
            return {};
        }
        
        // Resize to actual compressed size
        compressed.resize(compressed_size);
        
        last_error_.clear();
        return compressed;
    }
    
    std::vector<uint8_t> decompress(const std::vector<uint8_t>& compressed_data) {
        if (compressed_data.empty()) {
            last_error_ = "Input data is empty";
            return {};
        }
        
        // Start with a reasonable estimate (uncompressed size is usually larger)
        uLongf decompressed_size = compressed_data.size() * 4;
        std::vector<uint8_t> decompressed(decompressed_size);
        
        // Try to decompress
        int result = uncompress(decompressed.data(), &decompressed_size,
                                compressed_data.data(), compressed_data.size());
        
        // If buffer was too small, retry with larger buffer
        if (result == Z_BUF_ERROR) {
            decompressed_size *= 2;
            decompressed.resize(decompressed_size);
            result = uncompress(decompressed.data(), &decompressed_size,
                                compressed_data.data(), compressed_data.size());
        }
        
        if (result != Z_OK) {
            last_error_ = "Decompression failed with error: " + std::to_string(result);
            return {};
        }
        
        // Resize to actual decompressed size
        decompressed.resize(decompressed_size);
        
        last_error_.clear();
        return decompressed;
    }
    
    std::string get_last_error() const {
        return last_error_;
    }
    
    void set_compression_level(int level) {
        if (level < 0 || level > 9) {
            last_error_ = "Compression level must be between 0 and 9";
            return;
        }
        compression_level_ = level;
        last_error_.clear();
    }

private:
    int compression_level_;
    std::string last_error_;
};

CompressZlib::CompressZlib() : p_impl(std::make_unique<Impl>()) {}

CompressZlib::~CompressZlib() = default;

std::vector<uint8_t> CompressZlib::compress(const std::vector<uint8_t>& data) {
    return p_impl->compress(data);
}

std::vector<uint8_t> CompressZlib::decompress(const std::vector<uint8_t>& compressed_data) {
    return p_impl->decompress(compressed_data);
}

std::string CompressZlib::get_last_error() const {
    return p_impl->get_last_error();
}

void CompressZlib::set_compression_level(int level) {
    p_impl->set_compression_level(level);
}

} // namespace oemail
