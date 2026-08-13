#ifndef CRYPTO_INTERFACE_H
#define CRYPTO_INTERFACE_H

#include <string>
#include <vector>

namespace oemail {

// 加密接口
class CryptoInterface {
public:
    virtual ~CryptoInterface() = default;
    
    // 加密数据
    virtual std::vector<uint8_t> encrypt(const std::vector<uint8_t>& data) = 0;
    
    // 解密数据
    virtual std::vector<uint8_t> decrypt(const std::vector<uint8_t>& encrypted_data) = 0;
    
    // 设置加密密钥
    virtual void set_key(const std::string& key) = 0;
    
    // 获取最后错误信息
    virtual std::string get_last_error() const = 0;
};

} // namespace oemail

#endif // CRYPTO_INTERFACE_H
