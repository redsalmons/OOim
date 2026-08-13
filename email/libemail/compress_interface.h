#ifndef COMPRESS_INTERFACE_H
#define COMPRESS_INTERFACE_H

#include <string>
#include <vector>

namespace oemail {

// 压缩接口
class CompressInterface {
public:
    virtual ~CompressInterface() = default;
    
    // 压缩数据
    virtual std::vector<uint8_t> compress(const std::vector<uint8_t>& data) = 0;
    
    // 解压数据
    virtual std::vector<uint8_t> decompress(const std::vector<uint8_t>& compressed_data) = 0;
    
    // 获取最后错误信息
    virtual std::string get_last_error() const = 0;
};

} // namespace oemail

#endif // COMPRESS_INTERFACE_H
