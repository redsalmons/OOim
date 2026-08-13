#ifndef COMPRESS_ZLIB_H
#define COMPRESS_ZLIB_H

#include "compress_interface.h"
#include <string>
#include <vector>
#include <memory>

namespace oemail {

// Zlib 压缩实现类
class CompressZlib : public CompressInterface {
public:
    CompressZlib();
    ~CompressZlib() override;
    
    // 压缩数据
    std::vector<uint8_t> compress(const std::vector<uint8_t>& data) override;
    
    // 解压数据
    std::vector<uint8_t> decompress(const std::vector<uint8_t>& compressed_data) override;
    
    // 获取最后错误信息
    std::string get_last_error() const override;
    
    // 设置压缩级别 (0-9, 默认6)
    void set_compression_level(int level);

private:
    class Impl;
    std::unique_ptr<Impl> p_impl;
};

} // namespace oemail

#endif // COMPRESS_ZLIB_H
