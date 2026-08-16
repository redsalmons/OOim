#include "email_opt_163_impl.h"
#include "email_core.h"
#include "email_core_common.h"
#include "db_connection.h"
#include "session_repo.h"
#include "email_handler.h"
#include "logger.h"
#include <nlohmann/json.hpp>
#include <iostream>
#include <vector>
#include <unistd.h>
#include <ctime>
#include <vmime/vmime.hpp>
#include <vmime/platforms/posix/posixHandler.hpp>
#include <vmime/security/cert/defaultCertificateVerifier.hpp>
#include <vmime/net/imap/IMAPStore.hpp>
#include <vmime/net/imap/IMAPConnection.hpp>
#include <vmime/net/imap/IMAPCommand.hpp>
#include <vmime/net/imap/IMAPMessage.hpp>
#include <vmime/net/imap/IMAPFolder.hpp>
#include <vmime/net/imap/IMAPUtils.hpp>
#include <vmime/net/folder.hpp>
#include <vmime/net/message.hpp>
#include <vmime/net/fetchAttributes.hpp>
#include <vmime/net/messageSet.hpp>
#include <vmime/header.hpp>
#include <vmime/mediaType.hpp>
#include <vmime/net/socket.hpp>
#include <vmime/text.hpp>
#include <vmime/charset.hpp>
#include <vmime/charsetConverter.hpp>
#include <functional>
// #include "imap_opt_163.h"  // Requires gmime - commented out
// #include "imap_auth_163.h"  // Requires gmime - commented out

// Custom certificate verifier that accepts all certificates
class TrustAllCertificateVerifier : public vmime::security::cert::defaultCertificateVerifier {
public:
    void verify(
        const vmime::shared_ptr<vmime::security::cert::certificateChain>& chain,
        const vmime::string& hostname
    ) override {
        // Accept all certificates without verification
        std::cout << "TrustAllCertificateVerifier: Accepting certificate for " << hostname << std::endl;
    }
};

namespace EmailComm {

EmailOpt163Impl::EmailOpt163Impl(std::shared_ptr<::oemailim::EmailHandler> email_handler)
    : EmailOptInterface(email_handler), is_valid_(false), smtp_port_(465), imap_port_(993), current_selected_folder_("") {
}

EmailOpt163Impl::~EmailOpt163Impl() {
}

bool EmailOpt163Impl::connect() {
    return connect_();
}

bool EmailOpt163Impl::connect_() {
    LOG_INFO("163 connect_ - starting connection process\n");
    // Check if already connected — verify with NOOP to detect dead connections
    if (store_ && store_->isConnected()) {
        // Send NOOP to check if connection is truly alive
        try {
            auto imapStore = vmime::dynamic_pointer_cast<vmime::net::imap::IMAPStore>(store_);
            if (imapStore) {
                auto conn = imapStore->getConnection();
                if (conn) {
                    auto noopCmd = vmime::net::imap::IMAPCommand::createCommand("NOOP");
                    conn->sendCommand(noopCmd);
                    auto resp = conn->readResponse();
                    if (resp && !resp->isBad()) {
                        LOG_INFO("163 connect_ - already connected (NOOP OK)\n");
                        return true;
                    }
                }
            }
        } catch (const std::exception& e) {
            LOG_INFO("163 connect_ - NOOP failed, reconnecting: %s\n", e.what());
        }
        // NOOP failed — disconnect and fall through to reconnect
        try { store_->disconnect(); } catch (...) {}
        session_.reset();
        store_.reset();
        // Wait 5s before reconnecting to avoid server rejection due to concurrent connections
        LOG_INFO("163 connect_ - waiting 5s before reconnect...\n");
        usleep(5000000);
    }

    LOG_INFO("163 connect_ - checking auth code...\n");
    is_valid_ = !auth_code_.empty();
    if (!is_valid_) {
        LOG_INFO("163 connect_ - no auth code, connection failed\n");
        return false;
    }

    LOG_INFO("163 connect_ - establishing TCP connection to %s:%d...\n", 
             imap_server_.empty() ? "imap.163.com" : imap_server_.c_str(), 
             imap_port_ > 0 ? imap_port_ : 993);

    try {
        // Initialize vmime platform handler (only once)
        init_vmime_platform();

        // Create session using static method and save to member variable
        session_ = vmime::net::session::create();

        // Set connection timeout
        session_->getProperties()["connection.timeout"] = "30";
        session_->getProperties()["imap.timeout"] = "30";

        // Set client ID to simulate Apple Mail (required by 163 IMAP)
        // Must set both imap and imaps prefixes, and explicitly enable ID extension
        const std::vector<std::string> idPrefixes = {"net.imap.", "net.imaps."};
        for (const auto& prefix : idPrefixes) {
            session_->getProperties()[prefix + "client.id.enable"] = "true";
            session_->getProperties()[prefix + "client.id.name"] = "iOS Mail";
            session_->getProperties()[prefix + "client.id.version"] = "17.0";
            session_->getProperties()[prefix + "client.id.os"] = "iOS";
            session_->getProperties()[prefix + "client.id.os-version"] = "17.0";
            session_->getProperties()[prefix + "client.id.vendor"] = "Apple Inc.";
        }

        // Set authentication properties for plain auth
        session_->getProperties()["auth.username"] = email_;
        session_->getProperties()["auth.password"] = auth_code_;

        // Disable SSL certificate validation at session level
        session_->getProperties()["ssl.validate-certificates"] = "false";
        session_->getProperties()["ssl.check-server-identity"] = "false";
        session_->getProperties()["ssl.ca-file"] = "";
        session_->getProperties()["ssl.ca-path"] = "";

        // Create IMAP store with imaps:// protocol for SSL/TLS
        std::string imap_host = imap_server_.empty() ? "imap.163.com" : imap_server_;
        int imap_p = imap_port_ > 0 ? imap_port_ : 993;
        vmime::utility::url store_url("imaps", imap_host, imap_p);
        store_ = session_->getStore(store_url);

        // Set authentication properties on the store
        store_->setProperty("options.need-authentication", true);
        store_->setProperty("auth.username", email_);
        store_->setProperty("auth.password", auth_code_);

        // Set custom certificate verifier that accepts all certificates
        vmime::shared_ptr<TrustAllCertificateVerifier> verifier = vmime::make_shared<TrustAllCertificateVerifier>();
        store_->setCertificateVerifier(verifier);

        // Connect to the server
        store_->connect();

        LOG_INFO("163 connect_ - TCP connection established and login successful\n");

        // Send ID command immediately after login (required by 163 IMAP to avoid Unsafe Login)
        // Use sendCommand for proper tagging, then read raw socket to consume response
        // (vmime's parser can't parse * ID (...) untagged response, leaves parser state corrupted)
        try {
            vmime::shared_ptr<vmime::net::imap::IMAPStore> imapStore =
                vmime::dynamic_pointer_cast<vmime::net::imap::IMAPStore>(store_);
            if (imapStore) {
                vmime::shared_ptr<vmime::net::imap::IMAPConnection> conn = imapStore->getConnection();
                if (conn) {
                    vmime::shared_ptr<vmime::net::imap::IMAPCommand> idCmd =
                        vmime::net::imap::IMAPCommand::createCommand(
                            "ID (\"name\" \"iOS Mail\" \"version\" \"17.0\" \"os\" \"iOS\" \"os-version\" \"17.0\" \"vendor\" \"Apple Inc.\")");
                    conn->sendCommand(idCmd);

                    // Get tag AFTER sendCommand increments it
                    vmime::shared_ptr<vmime::net::imap::IMAPTag> tag = conn->getTag();
                    std::string tagStr = *tag;
                    LOG_INFO("163 connect_ - ID tagStr=[%s]\n", tagStr.c_str());

                    // Read raw socket to consume ID response without using vmime parser
                    // This avoids corrupting the parser's internal state
                    vmime::shared_ptr<const vmime::net::socket> sock = conn->getSocket();
                    if (sock) {
                        auto nonConstSock = vmime::const_pointer_cast<vmime::net::socket>(sock);
                        bool gotTagged = false;
                        int safety = 20;
                        while (!gotTagged && safety-- > 0) {
                            if (!nonConstSock->waitForRead(10000)) break;
                            std::string data;
                            nonConstSock->receive(data);
                            LOG_INFO("163 connect_ - ID raw read: %s\n", data.c_str());
                            // Check for tagged response (tagStr + " OK" or tagStr + " BAD")
                            if (data.find(tagStr + " OK") != std::string::npos ||
                                data.find(tagStr + " BAD") != std::string::npos ||
                                data.find(tagStr + " NO") != std::string::npos) {
                                gotTagged = true;
                            }
                        }
                        LOG_INFO("163 connect_ - ID command done, gotTagged=%d\n", gotTagged);
                    }
                }
            }
        } catch (const vmime::exception& e) {
            LOG_INFO("163 connect_ - ID command failed: %s\n", e.what());
        }

        is_valid_ = true;
        return true;

    } catch (const vmime::exception& e) {
        LOG_INFO("163 connect_ - vmime exception: %s\n", e.what());
        last_error_ = std::string("vmime exception: ") + e.what();
        is_valid_ = false;
        session_.reset();
        store_.reset();
        return false;
    } catch (const std::exception& e) {
        LOG_INFO("163 connect_ - std exception: %s\n", e.what());
        last_error_ = std::string("std exception: ") + e.what();
        is_valid_ = false;
        session_.reset();
        store_.reset();
        return false;
    }
}

bool EmailOpt163Impl::authority(int timeout_seconds) {
    // 163 doesn't use OAuth 2.0, uses password/authorization code directly
    // Return true since no OAuth flow is needed
    is_valid_ = !auth_code_.empty();
    return is_valid_;
}

void EmailOpt163Impl::set_auth_code(const std::string& code) {
    auth_code_ = code;
    is_valid_ = !code.empty();
}

void EmailOpt163Impl::set_email(const std::string& email) {
    email_ = email;
}

void EmailOpt163Impl::set_smtp_server(const std::string& server, int port) {
    smtp_server_ = server;
    smtp_port_ = port;
}

void EmailOpt163Impl::set_imap_server(const std::string& server, int port) {
    imap_server_ = server;
    imap_port_ = port;
}

bool EmailOpt163Impl::select_folder(const std::string& folder_name) {
    if (!store_ || !store_->isConnected()) {
        last_error_ = "Not connected to IMAP server";
        return false;
    }

    try {
        vmime::shared_ptr<vmime::net::folder> folder = store_->getFolder(vmime::utility::path(folder_name));
        folder->open(vmime::net::folder::MODE_READ_ONLY);
        folder_ = folder;
        std::cout << "163 select_folder - successfully selected folder: " << folder_name << std::endl;
        return true;
    } catch (const vmime::exception& e) {
        last_error_ = std::string("Failed to select folder: ") + e.what();
        std::cout << "163 select_folder - vmime exception: " << e.what() << std::endl;
        return false;
    }
}

std::vector<std::string> EmailOpt163Impl::fetch_emails_since_uid(const std::string& folder, const std::string& start_uid) {
    if (!store_ || !store_->isConnected()) {
        last_error_ = "Not connected to IMAP server";
        return {};
    }

    try {
        vmime::shared_ptr<vmime::net::folder> folder_obj = store_->getFolder(vmime::utility::path(folder));
        folder_obj->open(vmime::net::folder::MODE_READ_ONLY);

        // Get all messages using 1:* range
        vmime::net::messageSet allMessages = vmime::net::messageSet::byNumber(1, folder_obj->getMessageCount());
        std::vector<vmime::shared_ptr<vmime::net::message>> messages = folder_obj->getMessages(allMessages);
        std::vector<std::string> uids;

        int start_uid_int = 0;
        try {
            start_uid_int = std::stoi(start_uid);
        } catch (...) {
            start_uid_int = 0;
        }

        for (const auto& msg : messages) {
            std::string uid_str = msg->getUID();
            int uid = 0;
            try {
                uid = std::stoi(uid_str);
            } catch (...) {
                uid = 0;
            }
            if (uid >= start_uid_int) {
                uids.push_back(uid_str);
            }
        }

        folder_obj->close(false);
        std::cout << "163 fetch_emails_since_uid - found " << uids.size() << " emails" << std::endl;
        return uids;
    } catch (const vmime::exception& e) {
        last_error_ = std::string("Failed to fetch emails: ") + e.what();
        std::cout << "163 fetch_emails_since_uid - vmime exception: " << e.what() << std::endl;
        return {};
    }
}

std::string EmailOpt163Impl::get_email(const std::string& folder, const std::string& uid) {
    LOG_INFO("163 get_email - folder=%s, uid=%s, email_=%s, auth_code_set=%d\n", folder.c_str(), uid.c_str(), email_.c_str(), !auth_code_.empty());
    
    // Ensure connection is established
    if (!store_ || !store_->isConnected()) {
        LOG_INFO("163 get_email - not connected, attempting to reconnect\n");
        if (!connect_()) {
            LOG_INFO("163 get_email - reconnect failed\n");
            return "";
        }
    }

    try {
        // Use shared connection like fetch_email_headers does
        LOG_INFO("163 get_email - using shared connection\n");

        vmime::shared_ptr<vmime::net::imap::IMAPStore> imapStore =
            vmime::dynamic_pointer_cast<vmime::net::imap::IMAPStore>(store_);
        if (!imapStore) {
            last_error_ = "Failed to cast to IMAPStore";
            LOG_INFO("163 get_email - failed to cast to IMAPStore\n");
            return "";
        }

        // Map folder names to 163-specific paths
        std::string folderPath = folder;
        if (folderPath.find(' ') != std::string::npos && folderPath.front() != '"') {
            folderPath = "\"" + folderPath + "\"";
        }

        // Use IMAP FETCH command directly to avoid folder open issues
        // First ensure the correct folder is SELECTed, then FETCH
        try {
            vmime::shared_ptr<vmime::net::imap::IMAPStore> imapStore =
                vmime::dynamic_pointer_cast<vmime::net::imap::IMAPStore>(store_);
            if (!imapStore) {
                last_error_ = "Failed to cast to IMAPStore";
                LOG_INFO("163 get_email - failed to cast to IMAPStore\n");
                return "";
            }

            vmime::shared_ptr<vmime::net::imap::IMAPConnection> conn = imapStore->getConnection();
            if (!conn) {
                last_error_ = "No connection";
                LOG_INFO("163 get_email - no connection\n");
                return "";
            }

            // Always SELECT the folder before FETCH to ensure correct IMAP state
            // This is necessary because each instance has its own connection
            LOG_INFO("163 get_email - SELECTing folder=%s (path=%s)\n", folder.c_str(), folderPath.c_str());
            std::string selectCmd = "SELECT " + folderPath;
            vmime::shared_ptr<vmime::net::imap::IMAPCommand> selCmd =
                vmime::net::imap::IMAPCommand::createCommand(selectCmd);
            conn->sendCommand(selCmd);
            vmime::shared_ptr<vmime::net::imap::IMAPParser::response> selResp(conn->readResponse());
            if (!selResp || selResp->isBad()) {
                last_error_ = "SELECT command failed";
                LOG_INFO("163 get_email - SELECT command failed\n");
                return "";
            }
            current_selected_folder_ = folder;
            LOG_INFO("163 get_email - SELECT successful\n");

            // Send FETCH command directly using UID
            std::string fetchCmd = "UID FETCH " + uid + " BODY[]";
            vmime::shared_ptr<vmime::net::imap::IMAPCommand> cmd =
                vmime::net::imap::IMAPCommand::createCommand(fetchCmd);
            conn->sendCommand(cmd);

            // Read raw socket to get FETCH response
            vmime::shared_ptr<const vmime::net::socket> sock = conn->getSocket();
            if (!sock) {
                last_error_ = "No socket";
                LOG_INFO("163 get_email - no socket\n");
                return "";
            }

            auto nonConstSock = vmime::const_pointer_cast<vmime::net::socket>(sock);
            std::string fullResponse;
            int safety = 50;
            bool gotComplete = false;
            
            while (!gotComplete && safety-- > 0) {
                if (!nonConstSock->waitForRead(10000)) break;
                std::string data;
                nonConstSock->receive(data);
                fullResponse += data;
                
                // Check for tagged response (end of FETCH)
                vmime::shared_ptr<vmime::net::imap::IMAPTag> tag = conn->getTag();
                std::string tagStr = *tag;
                if (data.find(tagStr + " OK") != std::string::npos ||
                    data.find(tagStr + " BAD") != std::string::npos ||
                    data.find(tagStr + " NO") != std::string::npos) {
                    gotComplete = true;
                }
            }

            LOG_INFO("163 get_email - FETCH response received, size=%zu\n", fullResponse.size());
            LOG_INFO("163 get_email - FETCH response content: %s\n", fullResponse.c_str());
            
            // Parse FETCH response to extract email body
            // Format: * UID FETCH (BODY[] {size}\r\n<email content>\r\n)
            size_t bodyStart = fullResponse.find("BODY[]");
            if (bodyStart == std::string::npos) {
                last_error_ = "No BODY[] in response";
                LOG_INFO("163 get_email - no BODY[] in response, full response: %s\n", fullResponse.c_str());
                return "";
            }
            
            // Find the opening brace after BODY[]
            size_t braceStart = fullResponse.find("{", bodyStart);
            if (braceStart == std::string::npos) {
                last_error_ = "No size in BODY[]";
                LOG_INFO("163 get_email - no size in BODY[]\n");
                return "";
            }
            
            // Find the closing brace
            size_t braceEnd = fullResponse.find("}", braceStart);
            if (braceEnd == std::string::npos) {
                last_error_ = "Invalid BODY[] format";
                LOG_INFO("163 get_email - invalid BODY[] format\n");
                return "";
            }
            
            // Email content starts after }\r\n
            size_t contentStart = braceEnd + 1;
            if (contentStart + 2 < fullResponse.size() && fullResponse[contentStart] == '\r' && fullResponse[contentStart + 1] == '\n') {
                contentStart += 2;
            }
            
            // Email content ends before the closing ) of the FETCH response
            size_t contentEnd = fullResponse.rfind(")");
            if (contentEnd == std::string::npos || contentEnd <= contentStart) {
                last_error_ = "Invalid FETCH response format";
                LOG_INFO("163 get_email - invalid FETCH response format\n");
                return "";
            }
            
            // Remove trailing \r\n before the closing )
            size_t actualEnd = contentEnd;
            if (actualEnd > 2 && fullResponse[actualEnd - 2] == '\r' && fullResponse[actualEnd - 1] == '\n') {
                actualEnd -= 2;
            }
            
            std::string emailContent = fullResponse.substr(contentStart, actualEnd - contentStart);
            LOG_INFO("163 get_email - successfully extracted email content, size=%zu\n", emailContent.size());
            return emailContent;
        } catch (const vmime::exception& e) {
            last_error_ = std::string("Failed to get email: ") + e.what();
            LOG_INFO("163 get_email - vmime exception: %s\n", e.what());
            return "";
        }
    } catch (const std::exception& e) {
        last_error_ = std::string("Failed to get email: ") + e.what();
        LOG_INFO("163 get_email - std exception: %s\n", e.what());
        return "";
    }
}
#if 0
bool EmailOpt163Impl::send_email(const std::string& folder, const std::string& content) {
    LOG_INFO("163 send_email - folder=%s, content_size=%zu\n", folder.c_str(), content.size());
    LOG_INFO("163 send_email - content preview: %s\n", content.substr(0, std::min(size_t(200), content.size())).c_str());
    LOG_INFO("163 send_email - auth_code_=%s, email_=%s\n", auth_code_.c_str(), email_.c_str());
    
    is_valid_ = !auth_code_.empty();
    if (!is_valid_) {
        last_error_ = "No auth code";
        LOG_INFO("163 send_email - no auth code\n");
        return false;
    }

    try {
        // 1. 解析 JSON 内容
        nlohmann::json json_content = nlohmann::json::parse(content);
        std::string recipient = json_content.value("recipient", "");
        std::string subject = json_content.value("subject", "");
        std::string body = json_content.value("body", "");
        std::string in_reply_to = json_content.value("in_reply_to", "");
        std::string session_id = json_content.value("session_id", "");
        
        LOG_INFO("163 send_email - parsed JSON: recipient=%s, subject=%s, body_size=%zu, session_id=%s\n", 
                 recipient.c_str(), subject.c_str(), body.size(), session_id.c_str());

        if (recipient.empty()) {
            last_error_ = "No recipient in JSON";
            LOG_INFO("163 send_email - no recipient in JSON\n");
            return false;
        }

        init_vmime_platform();

        // 2. 创建 Session 并在 Session 层级强制设置认证参数
        vmime::shared_ptr<vmime::net::session> session = vmime::net::session::create();

        session->getProperties()["connection.timeout"] = "30";
        session->getProperties()["smtp.timeout"] = "30";
        session->getProperties()["auth.username"] = email_;
        session->getProperties()["auth.password"] = auth_code_;
        
        session->getProperties()["smtp.auth.login.enable"] = "true";
        session->getProperties()["smtp.auth.plain.enable"] = "true";

        session->getProperties()["ssl.validate-certificates"] = "false";
        session->getProperties()["ssl.check-server-identity"] = "false";

        // 3. 构建 URL (对 email 中的 @ 符号进行 percent-encoding: %40)
        std::string smtpServer = smtp_server_.empty() ? "smtp.163.com" : smtp_server_;
        int smtpPort = smtp_port_ > 0 ? smtp_port_ : 465;

        // 对 email_ 转义 @ -> %40
        std::string encoded_email = email_;
        size_t at_pos = encoded_email.find('@');
        if (at_pos != std::string::npos) {
            encoded_email.replace(at_pos, 1, "%40");
        }

        std::string authUrl = "smtps://" + encoded_email + ":" + auth_code_ + "@" + smtpServer + ":" + std::to_string(smtpPort);
        vmime::utility::url smtp_url(authUrl);
        vmime::shared_ptr<vmime::net::transport> tr = session->getTransport(smtp_url);

        // 设置证书验证器
        vmime::shared_ptr<TrustAllCertificateVerifier> verifier = vmime::make_shared<TrustAllCertificateVerifier>();
        tr->setCertificateVerifier(verifier);

        // 4. 使用 messageBuilder 构建符合标准的 MIME 邮件
        vmime::messageBuilder builder;
        builder.setExpeditor(vmime::mailbox(email_));
        
        vmime::addressList toList;
        toList.appendAddress(vmime::make_shared<vmime::mailbox>(recipient));
        builder.setRecipients(toList);
        
        builder.setSubject(vmime::text(subject, vmime::charset("UTF-8")));
        builder.getTextPart()->setCharset(vmime::charset("UTF-8"));
        builder.getTextPart()->setText(vmime::make_shared<vmime::stringContentHandler>(body));

        vmime::shared_ptr<vmime::message> msg = builder.construct();

        // 补全邮件头 (In-Reply-To 等)
        if (!in_reply_to.empty()) {
            msg->getHeader()->InReplyTo()->setValue(in_reply_to);
        }

        // 5. 连接并发送
        tr->connect();
        LOG_INFO("163 send_email - SMTP connected & authenticated\n");
        
        tr->send(msg);
        
        LOG_INFO("163 send_email - email sent successfully\n");
        
        tr->disconnect();
        return true;

    } catch (const nlohmann::json::exception& e) {
        last_error_ = std::string("JSON parse error: ") + e.what();
        LOG_INFO("163 send_email - JSON exception: %s\n", e.what());
        return false;
    } catch (const vmime::exception& e) {
        last_error_ = std::string("SMTP exception: ") + e.what();
        LOG_INFO("163 send_email - vmime exception: %s\n", e.what());
        return false;
    } catch (const std::exception& e) {
        last_error_ = std::string("std exception: ") + e.what();
        LOG_INFO("163 send_email - std exception: %s\n", e.what());
        return false;
    }
}
#endif


#include <iostream>
#include <string>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <vmime/vmime.hpp>
#include <vmime/platforms/posix/posixHandler.hpp>

// ==========================================
// 1. 自定义 Authenticator 类
// 继承自 vmime::security::sasl::SASLAuthenticator 以支持SASL认证
// ==========================================
class Simple163Authenticator : public vmime::security::sasl::SASLAuthenticator {
public:
    Simple163Authenticator(const std::string& username, const std::string& password)
        : m_username(username), m_password(password) {}

    const vmime::string getUsername() const override {
        return m_username;
    }

    const vmime::string getPassword() const override {
        return m_password;
    }

    const vmime::string getAccessToken() const override {
        return "";
    }

    const vmime::string getHostname() const override {
        return "";
    }

    const vmime::string getAnonymousToken() const override {
        return "";
    }

    const vmime::string getServiceName() const override {
        return "smtp";
    }

    void setService(const vmime::shared_ptr<vmime::net::service>& serv) override {
        m_service = serv;
    }

    // SASL认证需要的方法
    const std::vector <vmime::shared_ptr <vmime::security::sasl::SASLMechanism> > getAcceptableMechanisms(
        const std::vector <vmime::shared_ptr <vmime::security::sasl::SASLMechanism> >& available,
        const vmime::shared_ptr <vmime::security::sasl::SASLMechanism>& suggested
    ) const override {
        // 优先使用LOGIN，其次PLAIN
        std::vector <vmime::shared_ptr <vmime::security::sasl::SASLMechanism> > result;
        for (size_t i = 0; i < available.size(); ++i) {
            std::string name = vmime::utility::stringUtils::toUpper(available[i]->getName());
            if (name == "LOGIN" || name == "PLAIN") {
                result.push_back(available[i]);
            }
        }
        return result;
    }

    void setSASLSession(const vmime::shared_ptr <vmime::security::sasl::SASLSession>& sess) override {
        m_saslSession = sess;
    }

    void setSASLMechanism(const vmime::shared_ptr <vmime::security::sasl::SASLMechanism>& mech) override {
        m_saslMechanism = mech;
    }

private:
    vmime::string m_username;
    vmime::string m_password;
    vmime::shared_ptr<vmime::net::service> m_service;
    vmime::shared_ptr <vmime::security::sasl::SASLSession> m_saslSession;
    vmime::shared_ptr <vmime::security::sasl::SASLMechanism> m_saslMechanism;
};

// ==========================================
// 2. 自定义 Certificate Verifier (信任所有证书)
// ==========================================
class TrustAllCertificateVerifier : public vmime::security::cert::defaultCertificateVerifier {
public:
    void verify(const vmime::shared_ptr<vmime::security::cert::certificateChain>& /*chain*/,
                const vmime::string& /*hostname*/) override {
        // 允许所有自签名或非法证书
    }
};

// ==========================================
// 3. 平台初始化 helper
// ==========================================
static void init_vmime_platform() {
    static std::once_flag flag;
    std::call_once(flag, []() {
        vmime::platform::setHandler<vmime::platforms::posix::posixHandler>();
    });
}

// ==========================================
// 4. 邮件发送核心实现函数
// ==========================================
bool EmailOpt163Impl::send_email(const std::string& folder, const std::string& content) {
    LOG_INFO("163 send_email - folder=%s, content_size=%zu\n", folder.c_str(), content.size());
    LOG_INFO("163 send_email - auth_code_=%s, email_=%s\n", auth_code_.c_str(), email_.c_str());

    is_valid_ = !auth_code_.empty();
    if (!is_valid_) {
        last_error_ = "No auth code";
        return false;
    }

    try {
        // 解析输入 JSON 报文
        nlohmann::json json_content = nlohmann::json::parse(content);
        std::string recipient   = json_content.value("recipient", "");
        std::string subject     = json_content.value("subject", "");
        std::string body        = json_content.value("body", "");
        std::string in_reply_to = json_content.value("in_reply_to", "");
        std::string message_id  = json_content.value("message_id", "");
        std::string session_id  = json_content.value("session_id", "");
        std::string x_message_id = json_content.value("x_message_id", "");
        std::string x_session_chart  = json_content.value("x_session_chart", "");
        
        LOG_INFO("163 send_email - parsed: recipient='%s', subject='%s', in_reply_to='%s'\n", 
                 recipient.c_str(), subject.c_str(), in_reply_to.c_str());

        if (recipient.empty()) {
            last_error_ = "No recipient in JSON";
            return false;
        }

        // 初始化底层平台处理器
        init_vmime_platform();

        // 1. 创建 Session 实例
        vmime::shared_ptr<vmime::net::session> session = vmime::net::session::create();

        // 2. 设置 Session 级别的全局属性
        vmime::propertySet& props = session->getProperties();
        props["connection.timeout"] = "30";
        props["smtp.timeout"] = "30";

        // 强制开启认证并传入基本凭证（使用正确的属性前缀）
        props["transport.smtps.options.need-authentication"] = "true";
        props["transport.smtps.auth.username"] = email_;
        props["transport.smtps.auth.password"] = auth_code_;

        // 启用SASL认证
        props["transport.smtps.options.sasl"] = "true";
        props["transport.smtps.options.sasl.fallback"] = "false";

        props["ssl.validate-certificates"] = "false";
        props["ssl.check-server-identity"] = "false";

        LOG_INFO("163 send_email - Session properties set, username=%s\n", email_.c_str());

        std::string smtpServer = smtp_server_.empty() ? "smtp.163.com" : smtp_server_;
        int smtpPort = smtp_port_ > 0 ? smtp_port_ : 465;

        // 3. 构建标准的 SMTPS URL 并获取 Transport
        std::string authUrl = "smtps://" + smtpServer + ":" + std::to_string(smtpPort);
        vmime::utility::url smtp_url(authUrl);

        vmime::shared_ptr<vmime::net::transport> tr = session->getTransport(smtp_url);

        // 绑定认证器与证书校验器
        vmime::shared_ptr<Simple163Authenticator> auth =
            vmime::make_shared<Simple163Authenticator>(email_, auth_code_);
        tr->setAuthenticator(auth);

        vmime::shared_ptr<TrustAllCertificateVerifier> verifier =
            vmime::make_shared<TrustAllCertificateVerifier>();
        tr->setCertificateVerifier(verifier);

        // 5. 构建邮件 Message 报文
        vmime::messageBuilder builder;
        builder.setExpeditor(vmime::mailbox(email_));

        vmime::addressList toList;
        // Split recipient string by comma and add each as a separate mailbox
        {
            std::string recStr(recipient);
            size_t start = 0, end;
            while ((end = recStr.find(',', start)) != std::string::npos) {
                std::string addr = recStr.substr(start, end - start);
                size_t b = addr.find_first_not_of(" \t");
                size_t e = addr.find_last_not_of(" \t");
                if (b != std::string::npos) {
                    toList.appendAddress(vmime::make_shared<vmime::mailbox>(addr.substr(b, e - b + 1)));
                }
                start = end + 1;
            }
            std::string addr = recStr.substr(start);
            size_t b = addr.find_first_not_of(" \t");
            size_t e = addr.find_last_not_of(" \t");
            if (b != std::string::npos) {
                toList.appendAddress(vmime::make_shared<vmime::mailbox>(addr.substr(b, e - b + 1)));
            }
        }
        builder.setRecipients(toList);

        builder.setSubject(vmime::text(subject, vmime::charset("UTF-8")));
        builder.getTextPart()->setCharset(vmime::charset("UTF-8"));

        // For x_session_chart=data, encrypt the body
        std::string bodyToSend = body;
        if (x_session_chart == "data") {
            char encBody[65536];
            int encRc = email_prepare_data_body(body.c_str(), recipient.c_str(), email_.c_str(), encBody, sizeof(encBody));
            if (encRc == 0) {
                bodyToSend = encBody;
                LOG_INFO("163 send_email: encrypted data body, len=%zu\n", bodyToSend.size());
            } else {
                LOG_INFO("163 send_email: email_prepare_data_body failed, rc=%d, sending plaintext\n", encRc);
            }
        }

        builder.getTextPart()->setText(vmime::make_shared<vmime::stringContentHandler>(bodyToSend));

        vmime::shared_ptr<vmime::message> msg = builder.construct();

        // 6. 设置与网易 163 兼容的 RFC Header 字段
        vmime::shared_ptr<vmime::header> header = msg->getHeader();
        header->From()->setValue(vmime::make_shared<vmime::mailbox>(email_));
        header->Date()->setValue(vmime::datetime::now());
        
        // Use provided message_id or generate one
        std::string msg_id;
        if (!message_id.empty()) {
            msg_id = message_id;
            if (msg_id.front() != '<') msg_id = "<" + msg_id + ">";
        } else {
            msg_id = "<" + std::to_string(std::time(nullptr)) + "." + 
                                std::to_string(rand()) + "@163.com>";
        }
        header->MessageId()->setValue(msg_id);
        LOG_INFO("163 send_email - manually set Message-ID: %s\n", msg_id.c_str());

        // Set X-Message-ID header with the locally generated message_id
        // This allows FetchAndStore_c to match the sent email when QQ/163 server rewrites Message-ID
        vmime::shared_ptr<vmime::headerField> xMsgIdField =
            vmime::headerFieldFactory::getInstance()->create("X-Message-ID", msg_id);
        header->appendField(xMsgIdField);
        LOG_INFO("163 send_email - set X-Message-ID: %s\n", msg_id.c_str());

        // Set X-Session-Chart header if provided (for new session creation)
        if (!x_session_chart.empty()) {
            vmime::shared_ptr<vmime::headerField> xStartNewField =
                vmime::headerFieldFactory::getInstance()->create("X-Session-Chart", x_session_chart);
            header->appendField(xStartNewField);
            LOG_INFO("163 send_email - set X-Session-Chart: %s\n", x_session_chart.c_str());
        }

        // Handle In-Reply-To and References for conversation threading
        if (!in_reply_to.empty() && in_reply_to != "<>" && in_reply_to != "<<> <>") {
            // Ensure In-Reply-To is wrapped in angle brackets
            std::string irt = in_reply_to;
            if (irt.front() != '<') {
                irt = "<" + irt + ">";
            }
            // Validate that the value is not empty after wrapping
            if (irt != "<>" && irt != "< >") {
                header->InReplyTo()->setValue(irt);
                
                // Also set References field for proper threading
                header->References()->setValue(irt);
                LOG_INFO("163 send_email - set In-Reply-To and References: %s\n", irt.c_str());
            } else {
                LOG_INFO("163 send_email - skipped invalid in_reply_to: '%s'\n", in_reply_to.c_str());
            }
        }

        // 7. 连接并发送邮件
        LOG_INFO("163 send_email - About to connect to SMTP server\n");
        tr->connect();
        LOG_INFO("163 send_email - SMTP connected and authenticated\n");

        tr->send(msg);

        LOG_INFO("163 send_email - email sent successfully\n");

        tr->disconnect();

        // Insert sent email into database and session
        time_t now = time(NULL);
        struct tm* tm_info = localtime(&now);
        char date_str[64];
        strftime(date_str, sizeof(date_str), "%a, %d %b %Y %H:%M:%S %z", tm_info);

        char json_buffer[8192];
        int insert_result = email_insert_sent_email(
            email_.c_str(),
            email_.c_str(),
            email_.c_str(),
            recipient.c_str(),
            subject.c_str(),
            date_str,
            msg_id.c_str(),
            in_reply_to.c_str(),
            bodyToSend.c_str(),
            data_dir_.c_str(),
            json_buffer,
            sizeof(json_buffer)
        );

        if (insert_result == 0) {
            LOG_INFO("163 send_email: inserted sent email to database, json_buffer='%s'\n", json_buffer);
            try {
                nlohmann::json ins_response = nlohmann::json::parse(json_buffer);
                if (ins_response.contains("uuid")) {
                    std::string email_id = ins_response["uuid"].get<std::string>();

                    // For x_session_chart=new, create session locally
                    std::string sid = session_id;
                    if (x_session_chart == "new" && sid.empty()) {
                        char create_json[4096];
                        int create_rc = email_create_session(
                            email_.c_str(), subject.c_str(), email_.c_str(),
                            message_id.c_str(), 0, create_json, sizeof(create_json));
                        if (create_rc == 0) {
                            try {
                                auto resp = nlohmann::json::parse(create_json);
                                if (resp.value("status", "") == "success") {
                                    sid = resp.value("session_id", "");
                                }
                            } catch (...) {}
                        }
                        LOG_INFO("163 send_email: x_session_chart=new, created session_id=%s\n", sid.c_str());
                    }

                    // For x_session_chart=exchange or reply, find session via in_reply_to
                    if (sid.empty() && !in_reply_to.empty()) {
                        static SessionRepo s_sessionRepo;
                        sid = s_sessionRepo.querySessionByInReplyTo(in_reply_to, email_);
                        LOG_INFO("163 send_email: x_session_chart=%s, found session_id=%s via in_reply_to=%s\n",
                                 x_session_chart.c_str(), sid.c_str(), in_reply_to.c_str());
                    }

                    LOG_INFO("163 send_email: using session_id=%s\n", sid.c_str());

                    char session_buffer[8192];
                    int encMethod = (x_session_chart == "data") ? 1 : 0;
                    int session_result = email_add_email_to_session(
                        sid.c_str(),
                        email_id.c_str(),
                        email_.c_str(),
                        encMethod,
                        session_buffer,
                        sizeof(session_buffer)
                    );

                    if (session_result == 0) {
                        LOG_INFO("163 send_email: added email to session %s\n", sid.c_str());
                    } else {
                        LOG_INFO("163 send_email: failed to add email to session\n");
                    }
                }
            } catch (const std::exception& e) {
                LOG_INFO("163 send_email: failed to parse insert response: %s\n", e.what());
            }
        } else {
            LOG_INFO("163 send_email: failed to insert sent email to database\n");
        }

        return true;

    } catch (const vmime::exceptions::command_error& e) {
        std::string err_details = "SMTP Command Error: " + std::string(e.what()) +
                                  " | Server Response: " + e.response();
        last_error_ = err_details;
        LOG_INFO("163 send_email - %s\n", err_details.c_str());
        return false;

    } catch (const vmime::exceptions::authentication_error& e) {
        std::string err_details = "Authentication Error: " + std::string(e.what());
        last_error_ = err_details;
        LOG_INFO("163 send_email - %s\n", err_details.c_str());
        return false;

    } catch (const vmime::exception& e) {
        last_error_ = std::string("VMime exception: ") + e.what();
        LOG_INFO("163 send_email - vmime exception: %s\n", e.what());
        return false;

    } catch (const std::exception& e) {
        last_error_ = std::string("std exception: ") + e.what();
        LOG_INFO("163 send_email - std exception: %s\n", e.what());
        return false;
    }
}


std::string EmailOpt163Impl::find_sent_folder() {
    if (!store_ || !store_->isConnected()) {
        if (!connect_()) {
            LOG_INFO("163 find_sent_folder: connect failed\n");
            return "Sent";
        }
    }

    try {
        auto root = store_->getRootFolder();
        auto folders = root->getFolders(true);

        LOG_INFO("163 find_sent_folder: discovered %zu folders\n", folders.size());

        for (const auto& f : folders) {
            std::string name = f->getName().getBuffer();
            int specialUse = f->getAttributes().getSpecialUse();
            LOG_INFO("  folder: '%s', specialUse=%d\n", name.c_str(), specialUse);

            if (specialUse == vmime::net::folderAttributes::SPECIALUSE_SENT) {
                LOG_INFO("163 find_sent_folder: found Sent folder via SPECIAL-USE: '%s'\n", name.c_str());
                return name;
            }
        }

        // Fallback: check by name
        for (const auto& f : folders) {
            std::string name = f->getName().getBuffer();
            std::string lower = name;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            if (lower == "sent" || lower == "sent items" || lower == "sent messages" || lower == "已发送" || lower == "&xfjt0zab-") {
                LOG_INFO("163 find_sent_folder: found Sent folder by name: '%s'\n", name.c_str());
                return name;
            }
        }

        LOG_INFO("163 find_sent_folder: no Sent folder found, defaulting to '&XfJT0ZAB-'\n");
        return "&XfJT0ZAB-";
    } catch (const std::exception& e) {
        LOG_INFO("163 find_sent_folder: exception: %s\n", e.what());
        return "&XfJT0ZAB-"; // Default 163 sent folder in UTF-7
    }
}

std::string EmailOpt163Impl::fetch_email_headers(const std::string& folder, const std::string& start_uid) {
    LOG_INFO("163 fetch_email_headers - folder=%s, start_uid=%s\n", folder.c_str(), start_uid.c_str());
    
    // Ensure we are connected using connect_()
    if (!store_ || !store_->isConnected()) {
        LOG_INFO("163 fetch_email_headers - not connected, calling connect_()\n");
        if (!connect_()) {
            return R"({"status":"failed","error":"connect_failed"})";
        }
    }

    try {
        LOG_INFO("163 fetch_email_headers - using vmime folder API with existing connection\n");

        vmime::shared_ptr<vmime::net::imap::IMAPStore> imapStore =
            vmime::dynamic_pointer_cast<vmime::net::imap::IMAPStore>(store_);
        if (!imapStore) {
            return R"({"status":"failed","error":"not_imap_store"})";
        }

        vmime::shared_ptr<vmime::net::imap::IMAPConnection> conn = imapStore->getConnection();
        if (!conn) {
            return R"({"status":"failed","error":"no_connection"})";
        }

        // Get folder object (constructor uses store's existing connection, does NOT create new one)
        vmime::shared_ptr<vmime::net::folder> f = store_->getDefaultFolder();
        
        // Map folder names to 163-specific paths
        std::string folderPath = folder;
        if (folderPath.find(' ') != std::string::npos && folderPath.front() != '"') {
            folderPath = "\"" + folderPath + "\"";
        }
        
        if (folder != "INBOX") {
            vmime::net::folder::path path;
            path.appendComponent(vmime::net::folder::path::component(folderPath));
            f = store_->getFolder(path);
        }
        auto imapFolder = vmime::dynamic_pointer_cast<vmime::net::imap::IMAPFolder>(f);

        // SELECT via sendCommand on existing connection (has ID command sent)
        vmime::shared_ptr<vmime::net::imap::IMAPCommand> selectCmd =
            vmime::net::imap::IMAPCommand::createCommand("SELECT " + folderPath);
        conn->sendCommand(selectCmd);
        vmime::shared_ptr<vmime::net::imap::IMAPParser::response> selectResp(conn->readResponse());
        
        LOG_INFO("163 fetch_email_headers - SELECT folder=%s sent (original=%s)\n", folderPath.c_str(), folder.c_str());

        bool selectOk = false;
        bool unsafeLogin = false;

        if (selectResp && !selectResp->isBad()) {
            selectOk = true;
        }

        // Update current selected folder tracking (instance-specific)
        if (selectOk) {
            current_selected_folder_ = folder;
            LOG_INFO("163 fetch_email_headers - updated current_selected_folder_=%s\n", folder.c_str());
        }

        if (selectResp) {
            std::string errLog = selectResp->getErrorLog();
            if (errLog.find("Unsafe Login") != std::string::npos ||
                errLog.find("kefu@188.com") != std::string::npos) {
                unsafeLogin = true;
            }
        }

        LOG_INFO("163 fetch_email_headers - selectOk=%d, unsafeLogin=%d\n", selectOk, unsafeLogin);

        if (unsafeLogin) {
            return R"({"status":"failed","error":"163_unsafe_login","message":"163 IMAP reported unsafe login. Please update your authorization code."})";
        }

        if (!selectOk) {
            return R"({"status":"failed","error":"select_failed"})";
        }

        // Build message set based on start_uid
        // IMAP UID starts from 1, never use 0
        std::string effective_start_uid = start_uid;
        if (start_uid.empty() || start_uid == "0") {
            effective_start_uid = "1";  // Start from UID 1 for full sync
        }
        
        vmime::net::messageSet msgs = vmime::net::messageSet::byUID(
            vmime::net::message::uid(effective_start_uid), 
            vmime::net::message::uid("*")
        );

        LOG_INFO("163 fetch_email_headers - message set built, start_uid=%s, effective_start_uid=%s\n", start_uid.c_str(), effective_start_uid.c_str());

        // Build fetch attributes
        vmime::net::fetchAttributes fetchAttrs;
        fetchAttrs.add(vmime::net::fetchAttributes::UID);
        fetchAttrs.add(vmime::net::fetchAttributes::ENVELOPE);
        fetchAttrs.add(vmime::net::fetchAttributes::STRUCTURE);
        fetchAttrs.add(vmime::net::fetchAttributes::PEEK);
        fetchAttrs.add("Subject");
        fetchAttrs.add("From");
        fetchAttrs.add("Sender");
        fetchAttrs.add("To");
        fetchAttrs.add("Date");
        fetchAttrs.add("Reply-To");
        fetchAttrs.add("In-Reply-To");
        fetchAttrs.add("Message-ID");
        fetchAttrs.add("X-Message-ID");
        fetchAttrs.add("X-Session-Chart");
        fetchAttrs.add(vmime::net::fetchAttributes::FLAGS);

        // Ensure UID is included
        vmime::net::fetchAttributes attribsWithUID(fetchAttrs);
        attribsWithUID.add(vmime::net::fetchAttributes::UID);

        // Send FETCH via IMAPUtils::buildFetchCommand on existing connection
        vmime::shared_ptr<vmime::net::imap::IMAPCommand> fetchCmd =
            vmime::net::imap::IMAPUtils::buildFetchCommand(conn, msgs, attribsWithUID);
        LOG_INFO("163 fetch_email_headers - sending FETCH via IMAPUtils\n");
        fetchCmd->send(conn);

        // Read FETCH response
        vmime::shared_ptr<vmime::net::imap::IMAPParser::response> fetchResp(conn->readResponse());
        LOG_INFO("163 fetch_email_headers - FETCH response received, isBad=%d\n", fetchResp ? fetchResp->isBad() : -1);

        if (!fetchResp || fetchResp->isBad()) {
            LOG_INFO("163 fetch_email_headers - FETCH response is bad or null\n");
            return R"({"status":"failed","error":"fetch_bad_response"})";
        }

        // Helper: recursively parse vmime xbody into JSON
        std::function<nlohmann::json(const vmime::net::imap::IMAPParser::xbody*)> parseXbody =
            [&](const vmime::net::imap::IMAPParser::xbody* xb) -> nlohmann::json {
            nlohmann::json j;
            if (!xb) return j;

            if (xb->body_type_mpart) {
                auto* mpart = xb->body_type_mpart.get();
                j["type"] = "multipart";
                if (mpart->media_subtype) {
                    j["subtype"] = mpart->media_subtype->value;
                }
                j["parts"] = nlohmann::json::array();
                for (auto& part : mpart->list) {
                    j["parts"].push_back(parseXbody(part.get()));
                }
            } else if (xb->body_type_1part) {
                auto* p1 = xb->body_type_1part.get();
                std::string mediaType;
                std::string mediaSubtype;

                if (p1->body_type_text) {
                    mediaType = "text";
                    if (p1->body_type_text->media_text && p1->body_type_text->media_text->media_subtype) {
                        mediaSubtype = p1->body_type_text->media_text->media_subtype->value;
                    }
                    if (p1->body_type_text->body_fields && p1->body_type_text->body_fields->body_fld_enc) {
                        j["encoding"] = p1->body_type_text->body_fields->body_fld_enc->value;
                    }
                    if (p1->body_type_text->body_fields && p1->body_type_text->body_fields->body_fld_octets) {
                        j["size"] = p1->body_type_text->body_fields->body_fld_octets->value;
                    }
                } else if (p1->body_type_msg) {
                    mediaType = "message";
                    mediaSubtype = "rfc822";
                } else if (p1->body_type_basic) {
                    auto* basic = p1->body_type_basic.get();
                    if (basic->media_basic && basic->media_basic->media_type) {
                        mediaType = basic->media_basic->media_type->value;
                    }
                    if (basic->media_basic && basic->media_basic->media_subtype) {
                        mediaSubtype = basic->media_basic->media_subtype->value;
                    }
                    if (basic->body_fields && basic->body_fields->body_fld_enc) {
                        j["encoding"] = basic->body_fields->body_fld_enc->value;
                    }
                    if (basic->body_fields && basic->body_fields->body_fld_octets) {
                        j["size"] = basic->body_fields->body_fld_octets->value;
                    }
                }

                j["type"] = mediaType;
                j["subtype"] = mediaSubtype;
            }

            return j;
        };

        // Parse response tree manually
        nlohmann::json emails_array = nlohmann::json::array();

        for (auto& item : fetchResp->continue_req_or_response_data) {
            if (!item || !item->response_data) continue;
            auto* messageData = item->response_data->message_data.get();
            if (!messageData || messageData->type != vmime::net::imap::IMAPParser::message_data::FETCH) continue;
            if (!messageData->msg_att) continue;

            nlohmann::json email_obj;
            std::string headerData;

            for (auto& attItem : messageData->msg_att->items) {
                if (!attItem) continue;

                if (attItem->type == vmime::net::imap::IMAPParser::msg_att_item::UID) {
                    if (attItem->uniqueid) {
                        std::string uidStr = std::to_string(attItem->uniqueid->value);
                        LOG_INFO("163 fetch_email_headers - found UID: %s, start_uid: %s\n", uidStr.c_str(), start_uid.c_str());
                        // Skip the endpoint UID (start_uid itself)
                        if (!start_uid.empty() && start_uid != "0" && start_uid != "1" && uidStr == start_uid) {
                            LOG_INFO("163 fetch_email_headers - skipping UID %s (equals start_uid)\n", uidStr.c_str());
                            email_obj.clear();  // mark for skip
                            break;
                        }
                        email_obj["uuid"] = uidStr;
                    } else {
                        LOG_INFO("163 fetch_email_headers - UID item found but uniqueid is null\n");
                    }
                } else if (attItem->type == vmime::net::imap::IMAPParser::msg_att_item::BODY_SECTION) {
                    if (attItem->nstring && !attItem->nstring->isNIL) {
                        headerData = attItem->nstring->value;
                    }
                } else if (attItem->type == vmime::net::imap::IMAPParser::msg_att_item::BODY_STRUCTURE) {
                    if (attItem->body) {
                        email_obj["bodystructure"] = parseXbody(attItem->body.get());
                    }
                } else if (attItem->type == vmime::net::imap::IMAPParser::msg_att_item::FLAGS) {
                    if (attItem->flag_list) {
                        nlohmann::json flagsArray = nlohmann::json::array();
                        for (auto& flag : attItem->flag_list->flags) {
                            if (flag) {
                                std::string flagStr;
                                switch (flag->type) {
                                    case vmime::net::imap::IMAPParser::flag::ANSWERED:
                                        flagStr = "\\Answered";
                                        break;
                                    case vmime::net::imap::IMAPParser::flag::FLAGGED:
                                        flagStr = "\\Flagged";
                                        break;
                                    case vmime::net::imap::IMAPParser::flag::DELETED:
                                        flagStr = "\\Deleted";
                                        break;
                                    case vmime::net::imap::IMAPParser::flag::SEEN:
                                        flagStr = "\\Seen";
                                        break;
                                    case vmime::net::imap::IMAPParser::flag::DRAFT:
                                        flagStr = "\\Draft";
                                        break;
                                    case vmime::net::imap::IMAPParser::flag::STAR:
                                        flagStr = "\\*";
                                        break;
                                    case vmime::net::imap::IMAPParser::flag::KEYWORD_OR_EXTENSION:
                                        if (flag->flag_keyword) {
                                            flagStr = flag->flag_keyword->value;
                                        }
                                        break;
                                    case vmime::net::imap::IMAPParser::flag::UNKNOWN:
                                        flagStr = flag->name;
                                        break;
                                }
                                if (!flagStr.empty()) {
                                    flagsArray.push_back(flagStr);
                                }
                            }
                        }
                        email_obj["flags"] = flagsArray;
                    }
                }
            }

            // Parse all headers using vmime to handle folding and case-insensitivity
            auto caseInsensitiveLess = [](const std::string& a, const std::string& b) {
                return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(),
                    [](char c1, char c2) { return ::tolower(c1) < ::tolower(c2); });
            };
            std::map<std::string, std::string, decltype(caseInsensitiveLess)> headerMap(caseInsensitiveLess);
            std::vector<std::string> receivedHeaders; // Collect all Received headers
            {
                vmime::parsingContext parseCtx;
                size_t pos = 0;
                const size_t end = headerData.size();
                while (pos < end) {
                    auto field = vmime::headerField::parseNext(parseCtx, headerData, pos, end, &pos);
                    if (!field) break;
                    std::string fName = field->getName();
                    std::string fValue;
                    auto val = field->getValue();
                    if (val) {
                        // Generate the value as string
                        std::string generated;
                        vmime::utility::outputStreamStringAdapter os(generated);
                        val->generate(vmime::generationContext::getDefaultContext(), os, 0);
                        os.flush();
                        fValue = generated;
                    }
                    // Collect Received headers separately (there can be multiple)
                    if (strcasecmp(fName.c_str(), "Received") == 0) {
                        receivedHeaders.push_back(fValue);
                    }
                    headerMap[fName] = fValue;
                }
            }

            auto getHeader = [&](const std::string& name) -> std::string {
                auto it = headerMap.find(name);
                if (it != headerMap.end()) {
                    return it->second;
                }
                return "";
            };

            // Decode MIME encoded-word headers using vmime
            auto decodeHeader = [](const std::string& raw) -> std::string {
                if (raw.empty()) return raw;
                try {
                    auto decoded = vmime::text::decodeAndUnfold(raw);
                    if (decoded) {
                        return decoded->getConvertedText(vmime::charset("utf-8"));
                    }
                    return raw;
                } catch (...) {
                    return raw;
                }
            };

            email_obj["from"] = decodeHeader(getHeader("From"));
            email_obj["sender"] = decodeHeader(getHeader("Sender"));
            email_obj["subject"] = decodeHeader(getHeader("Subject"));
            email_obj["date"] = getHeader("Date");
            email_obj["reply_to"] = decodeHeader(getHeader("Reply-To"));
            email_obj["in_reply_to"] = decodeHeader(getHeader("In-Reply-To"));
            std::string xMsgId = decodeHeader(getHeader("X-Message-ID"));
            std::string stdMsgId = decodeHeader(getHeader("Message-ID"));
            email_obj["message_id"] = !xMsgId.empty() ? xMsgId : stdMsgId;
            email_obj["x_message_id"] = xMsgId;
            email_obj["x_session_chart"] = decodeHeader(getHeader("X-Session-Chart"));
            email_obj["to_addr"] = decodeHeader(getHeader("To"));
            
            // Extract service receive time from the first (topmost) Received header
            // The format is: "from ... by ...; <date>"
            // The topmost Received header is the one added by the destination server
            std::string servicerecvtime;
            if (!receivedHeaders.empty()) {
                // First Received header = topmost = last hop = destination server
                const std::string& receivedHeader = receivedHeaders[0];
                // Extract the date after the last semicolon
                size_t semiPos = receivedHeader.rfind(';');
                if (semiPos != std::string::npos) {
                    std::string datePart = receivedHeader.substr(semiPos + 1);
                    // Trim whitespace
                    size_t start = datePart.find_first_not_of(" \t\r\n");
                    if (start != std::string::npos) {
                        servicerecvtime = datePart.substr(start);
                        // Trim trailing whitespace
                        size_t end = servicerecvtime.find_last_not_of(" \t\r\n");
                        if (end != std::string::npos) {
                            servicerecvtime = servicerecvtime.substr(0, end + 1);
                        }
                    }
                }
            }
            email_obj["servicerecvtime"] = servicerecvtime;
            
            // Log message_id and in_reply_to for debugging conversation threading
            std::string uuid_str = email_obj.contains("uuid") && !email_obj["uuid"].is_null() ? email_obj["uuid"].get<std::string>() : "";
            std::string msg_id_str = email_obj.contains("message_id") && !email_obj["message_id"].is_null() ? email_obj["message_id"].get<std::string>() : "";
            std::string reply_to_str = email_obj.contains("in_reply_to") && !email_obj["in_reply_to"].is_null() ? email_obj["in_reply_to"].get<std::string>() : "";
            LOG_INFO("163 fetch_email_headers - uuid=%s, message_id='%s', in_reply_to='%s'\n", 
                     uuid_str.c_str(), msg_id_str.c_str(), reply_to_str.c_str());

            if (!email_obj.is_null() && !email_obj.empty() && !email_obj["uuid"].is_null()) {
                emails_array.push_back(email_obj);
            }
        }

        std::cerr << "163 fetch_email_headers - parsed " << emails_array.size() << " emails" << std::endl;

        nlohmann::json response;
        response["status"] = "success";
        response["folder"] = folder;
        response["count"] = emails_array.size();
        response["emails"] = emails_array;

        // Notify UI about new emails if any were found
        if (emails_array.size() > 0 && email_handler_) {
            nlohmann::json notification;
            notification["account"] = email_;
            notification["count"] = emails_array.size();
            notification["emails"] = emails_array;
            notification["folder"] = folder;
            
            // Find the Email object for this instance to get configIndex
            // For now, use a placeholder - the actual configIndex should be passed in
            // This is similar to how Outlook does it
            email_handler_->notify(nullptr, 3, notification.dump()); // 3 = NOTIFICATION_MESSAGE_SUCCESS
            LOG_INFO("163 fetch_email_headers - notified UI about %zu new emails\n", emails_array.size());
        }

        return response.dump();
    } catch (const vmime::exception& e) {
        last_error_ = std::string("Failed to fetch email headers: ") + e.what();
        LOG_INFO("163 fetch_email_headers - vmime exception: %s\n", e.what());
        std::cerr << "163 fetch_email_headers - vmime exception: " << e.what() << std::endl;

        // If connection was reset, disconnect and retry once
        std::string what = e.what();
        if (what.find("ECONNRESET") != std::string::npos ||
            what.find("connection reset") != std::string::npos ||
            what.find("broken pipe") != std::string::npos) {
            std::cerr << "163 fetch_email_headers - connection lost, reconnecting..." << std::endl;
            if (store_) store_->disconnect();
            if (connect_()) {
                // Retry fetch by recursing once (connect_ re-establishes connection)
                // Avoid infinite recursion by not retrying on second failure
                try {
                    vmime::shared_ptr<vmime::net::imap::IMAPStore> imapStore2 =
                        vmime::dynamic_pointer_cast<vmime::net::imap::IMAPStore>(store_);
                    if (!imapStore2) return R"({"status":"failed","error":"not_imap_store"})";

                    vmime::shared_ptr<vmime::net::imap::IMAPConnection> conn2 = imapStore2->getConnection();
                    if (!conn2) return R"({"status":"failed","error":"no_connection"})";

                    // Re-select INBOX
                    vmime::shared_ptr<vmime::net::imap::IMAPCommand> selCmd2 =
                        vmime::net::imap::IMAPCommand::createCommand("SELECT INBOX");
                    conn2->sendCommand(selCmd2);
                    auto selResp2 = conn2->readResponse();
                    if (!selResp2 || selResp2->isBad()) {
                        return R"({"status":"failed","error":"select_failed"})";
                    }

                    // Re-send FETCH — but we need the same message set.
                    // For simplicity, just return success with empty list and let
                    // the next polling cycle handle it.
                    nlohmann::json retryResp;
                    retryResp["status"] = "success";
                    retryResp["count"] = 0;
                    retryResp["emails"] = nlohmann::json::array();
                    retryResp["stored"] = 0;
                    return retryResp.dump();
                } catch (const std::exception& e2) {
                    last_error_ = std::string("Failed to fetch after reconnect: ") + e2.what();
                    return std::string(R"({"status":"failed","error":"reconnect_fetch_failed"})");
                }
            }
        }
        return std::string(R"({"status":"failed","error":"vmime_exception:})") + e.what() + "\"}";
    } catch (const std::exception& e) {
        last_error_ = std::string("Failed to fetch email headers: ") + e.what();
        LOG_INFO("163 fetch_email_headers - std exception: %s\n", e.what());
        std::cerr << "163 fetch_email_headers - std exception: " << e.what() << std::endl;
        return std::string(R"({"status":"failed","error":"std_exception: )") + std::string(e.what()) + R"("})";
    }
}

bool EmailOpt163Impl::idle_wait(const std::string& folder, int timeout_seconds) {
    if (!store_ || !store_->isConnected()) {
        if (!connect_()) {
            last_error_ = "IDLE: connect failed";
            return false;
        }
    }

    try {
        auto imapStore = vmime::dynamic_pointer_cast<vmime::net::imap::IMAPStore>(store_);
        if (!imapStore) {
            last_error_ = "IDLE: not IMAP store";
            return false;
        }

        auto conn = imapStore->getConnection();
        if (!conn) {
            last_error_ = "IDLE: no connection";
            return false;
        }

        // SELECT the folder before entering IDLE
        std::cerr << "[IDLE] Selecting folder: " << folder << std::endl;
        vmime::shared_ptr<vmime::net::imap::IMAPCommand> selectCmd =
            vmime::net::imap::IMAPCommand::createCommand("SELECT " + folder);
        conn->sendCommand(selectCmd);
        vmime::shared_ptr<vmime::net::imap::IMAPParser::response> selectResp(conn->readResponse());

        bool selectOk = false;
        if (selectResp && !selectResp->isBad()) {
            selectOk = true;
        }

        std::cerr << "[IDLE] SELECT result: " << (selectOk ? "OK" : "FAILED") << std::endl;

        if (!selectOk) {
            last_error_ = "IDLE: SELECT failed for folder " + folder;
            return false;
        }

        // 1. Send IDLE command directly via raw socket (bypass vmime's sendCommand
        //    which uses tag prefix — IDLE doesn't use a tag in some server impls,
        //    but standard IMAP requires it. We send tagged IDLE.)
        //    Actually, let's use sendRaw to have full control.
        auto constSocket = conn->getSocket();
        if (!constSocket) {
            last_error_ = "IDLE: no socket";
            return false;
        }
        auto sok = vmime::const_pointer_cast<vmime::net::socket>(constSocket);
        auto timeoutHandler = sok->getTimeoutHandler();

        // Send IDLE command via vmime's sendCommand (handles tag generation)
        vmime::shared_ptr<vmime::net::imap::IMAPCommand> idleCmd =
            vmime::net::imap::IMAPCommand::createCommand("IDLE");
        conn->sendCommand(idleCmd);
        std::cerr << "[IDLE] IDLE command sent via sendCommand, waiting for continuation..." << std::endl;

        // 2. Read the continuation response ("+ idling" or "+") directly from socket
        //    Bypass vmime's readResponse() which may not parse continuation correctly.
        std::string rawBuffer;
        bool gotContinuation = false;

        // Read lines until we see "+" continuation
        while (!gotContinuation) {
            try {
                if (timeoutHandler) timeoutHandler->resetTimeOut();
                sok->waitForRead(5000);
                if (timeoutHandler) timeoutHandler->resetTimeOut();
                std::string chunk;
                sok->receive(chunk);
                if (!chunk.empty()) {
                    std::cerr << "[IDLE] continuation read: " << chunk.substr(0, 200) << std::endl;
                }
                if (chunk.empty()) continue;

                rawBuffer += chunk;

                size_t pos;
                while ((pos = rawBuffer.find('\n')) != std::string::npos) {
                    std::string line = rawBuffer.substr(0, pos);
                    rawBuffer.erase(0, pos + 1);
                    if (!line.empty() && line.back() == '\r') line.pop_back();

                    // Check for continuation "+"
                    if (!line.empty() && line[0] == '+') {
                        gotContinuation = true;
                    }
                }
            } catch (const vmime::exceptions::operation_timed_out&) {
                // vmime timeout handler fired — reset and retry
                if (timeoutHandler) timeoutHandler->resetTimeOut();
                continue;
            }
        }

        if (!gotContinuation) {
            last_error_ = "IDLE: no continuation response from server";
            return false;
        }

        bool gotNotification = false;

        // TLS socket's receive() is non-blocking (gnutls_record_recv returns E_AGAIN).
        // We must call waitForRead() first to block until data is available on the
        // underlying raw socket, then call receive() to read through the TLS layer.
        while (!gotNotification) {
            try {
                if (timeoutHandler) timeoutHandler->resetTimeOut();
                sok->waitForRead(5000);
                if (timeoutHandler) timeoutHandler->resetTimeOut();

                std::string chunk;
                sok->receive(chunk);

                if (chunk.empty()) {
                    continue;
                }

                rawBuffer += chunk;

                // Process complete lines in rawBuffer
                size_t pos;
                while ((pos = rawBuffer.find('\n')) != std::string::npos) {
                    std::string line = rawBuffer.substr(0, pos);
                    rawBuffer.erase(0, pos + 1);

                    if (!line.empty() && line.back() == '\r') {
                        line.pop_back();
                    }

                    if (line.find("EXISTS") != std::string::npos) {
                        gotNotification = true;
                    }
                    if (line.find("EXPUNGE") != std::string::npos) {
                        gotNotification = true;
                    }
                }
            } catch (const vmime::exceptions::operation_timed_out&) {
                // vmime timeout handler fired — reset and keep waiting
                if (timeoutHandler) timeoutHandler->resetTimeOut();
                continue;
            }
        }

        // 4. Send DONE to exit IDLE mode
        std::string doneCmd = "DONE\r\n";
        conn->sendRaw(vmime::utility::stringUtils::bytesFromString(doneCmd), doneCmd.size());

        // 5. Read the tagged response for DONE.
        //    After our raw socket reading in step 3, vmime's internal m_buffer is empty.
        //    The server sends the tagged OK response after receiving DONE, so it will
        //    be in the socket. However, if rawBuffer has leftover untagged lines that
        //    we didn't fully process, those are lost — acceptable since we only need
        //    the tagged OK to sync state.
        //    Edge case: if rawBuffer contains partial data that looks like a tagged
        //    response, we skip readResponse() to avoid blocking. This is unlikely
        //    since the server sends tagged response only after DONE.
        conn->readResponse();

        // 6. Force disconnect after IDLE to ensure clean connection state for fetch.
        //    Raw socket reads during IDLE can corrupt vmime's internal buffer state,
        //    causing subsequent SELECT/FETCH to time out on the same connection.
        try { store_->disconnect(); } catch (...) {}
        session_.reset();
        store_.reset();

        return gotNotification;
    } catch (const vmime::exceptions::operation_timed_out& e) {
        last_error_ = std::string("IDLE wait failed: ") + e.what();
        return false;
    } catch (const vmime::exception& e) {
        last_error_ = std::string("IDLE wait failed: ") + e.what();
        std::cerr << "[IDLE] vmime exception: " << e.what() << std::endl;
        return false;
    } catch (const std::exception& e) {
        last_error_ = std::string("IDLE wait failed: ") + e.what();
        std::cerr << "[IDLE] std exception: " << e.what() << std::endl;
        return false;
    }
}

bool EmailOpt163Impl::launch_browser(const std::string& url) {
    // Create JSON with URL parameter
    nlohmann::json json_obj;
    json_obj["url"] = url;
    std::string json_str = json_obj.dump();

    // Notify UI thread to launch browser
    // Pass nullptr for email pointer since we don't have it in this context
    // The UI layer will handle the actual browser launch
    email_handler_->notify(nullptr, 1, json_str);  // 1 = NOTIFICATION_MESSAGE_BROWSER_LAUNCH
    return true;
}

} // namespace EmailComm
