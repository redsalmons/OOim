#ifndef CRYPTO_AES_H
#define CRYPTO_AES_H

#include "crypto_interface.h"
#include <string>
#include <vector>
#include <memory>

namespace oemail {

// AES 加密实现类
class CryptoAES : public CryptoInterface {
public:
    CryptoAES();
    ~CryptoAES() override;
    
    // 加密数据
    std::vector<uint8_t> encrypt(const std::vector<uint8_t>& data) override;
    
    // 解密数据
    std::vector<uint8_t> decrypt(const std::vector<uint8_t>& encrypted_data) override;
    
    // 设置加密密钥
    void set_key(const std::string& key) override;
    
    // 获取最后错误信息
    std::string get_last_error() const override;

private:
    class Impl;
    std::unique_ptr<Impl> p_impl;
};

} // namespace oemail

#endif // CRYPTO_AES_H
