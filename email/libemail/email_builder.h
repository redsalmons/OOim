#ifndef EMAIL_BUILDER_H
#define EMAIL_BUILDER_H

#include <string>
#include <vector>
#include <filesystem>
#include "crypto_aes.h"
#include "compress_zlib.h"

namespace oemail {

// 邮件构建类，用于将本地文件和目录转换为邮件格式
class EmailBuilder {
public:
    EmailBuilder();
    ~EmailBuilder();
    
    // 设置加密密钥
    void set_encryption_key(const std::string& key);
    
    // 设置发送者邮箱
    void set_sender_email(const std::string& email);
    
    // 创建邮件内容
    std::string create_email_content(const std::string& message_id, const std::string& subject, 
                                    const std::vector<uint8_t>& metadata, const std::vector<uint8_t>& data, 
                                    const std::string& reply_to = "");
    
    // 读取文件内容
    std::vector<uint8_t> read_file_content(const std::filesystem::path& file_path);
    
    // 压缩和加密数据
    std::vector<uint8_t> compress_and_encrypt(const std::vector<uint8_t>& data);
    
    // 解密和解压数据
    std::vector<uint8_t> decrypt_and_decompress(const std::vector<uint8_t>& encrypted_data);
    
    // 从邮件内容中提取数据附件
    std::vector<uint8_t> extract_data_from_email(const std::string& email_content);
    
    // 获取最后错误信息
    std::string get_last_error() const;

private:
    class Impl;
    std::unique_ptr<Impl> p_impl;
};

} // namespace oemail

#endif // EMAIL_BUILDER_H
