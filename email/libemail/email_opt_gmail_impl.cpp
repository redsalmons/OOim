#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "email_opt_gmail_impl.h"
#include "email_handler.h"
#include "email_core.h"
#include "email_core_common.h"
#include "db_connection.h"
#include "session_repo.h"
#include "logger.h"
#include <httplib.h>
#include <random>
#include <openssl/sha.h>
#include <openssl/evp.h>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <ctime>
#include <functional>
#include <thread>
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

#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

// Custom certificate verifier that accepts all certificates
class GmailTrustAllCertificateVerifier : public vmime::security::cert::defaultCertificateVerifier {
public:
    void verify(
        const vmime::shared_ptr<vmime::security::cert::certificateChain>& chain,
        const vmime::string& hostname) override {
        // Accept all certificates
    }
};

namespace EmailComm {

EmailOptGmailImpl::EmailOptGmailImpl(std::shared_ptr<::oemailim::EmailHandler> email_handler)
    : EmailOptInterface(email_handler)
    , client_id_(GMAIL_OAUTH_CLIENT_ID)
    , client_secret_(GMAIL_CLIENT_SECRET)
    , is_valid_(false) {
}

EmailOptGmailImpl::~EmailOptGmailImpl() {
}

bool EmailOptGmailImpl::connect() {
    return connect_();
}

bool EmailOptGmailImpl::connect_() {
    // Check if already connected
    if (store_ && store_->isConnected()) {
        std::cout << "Gmail connect_ - already connected, returning true" << std::endl;
        return true;
    }

    std::cout << "Gmail connect_ - checking access token..." << std::endl;
    is_valid_ = !access_token_.empty();
    if (!is_valid_) {
        std::cout << "Gmail connect_ - no access token, connection failed" << std::endl;
        return false;
    }

    std::cout << "Gmail connect_ - establishing TCP connection to imap.gmail.com:993..." << std::endl;

    try {
        // Initialize vmime platform handler (only once)
        init_vmime_platform();

        // Create session using static method and save to member variable
        session_ = vmime::net::session::create();

        // Set authentication properties for OAuth2
        session_->getProperties()["auth.username"] = email_;
        session_->getProperties()["auth.password"] = access_token_;
        session_->getProperties()["auth.access_token"] = access_token_;

        // Create IMAP store and save to member variable
        store_ = session_->getStore(
            vmime::utility::url("imap://imap.gmail.com:993")
        );

        // Connect to the server
        store_->connect();

        std::cout << "Gmail connect_ - TCP connection established and login successful" << std::endl;
        is_valid_ = true;
        return true;

    } catch (const vmime::exception& e) {
        std::cout << "Gmail connect_ - vmime exception: " << e.what() << std::endl;
        last_error_ = std::string("vmime exception: ") + e.what();
        is_valid_ = false;
        session_.reset();
        store_.reset();
        return false;
    } catch (const std::exception& e) {
        std::cout << "Gmail connect_ - std exception: " << e.what() << std::endl;
        last_error_ = std::string("std exception: ") + e.what();
        is_valid_ = false;
        session_.reset();
        store_.reset();
        return false;
    }
}

bool EmailOptGmailImpl::authority(int timeout_seconds) {
    // If we already have a refresh token, use it to get a new access token
    if (!refresh_token_.empty()) {
        return refresh_token();
    }

    // Otherwise, perform full OAuth flow to get refresh token
    // Generate PKCE code verifier and challenge
    code_verifier_ = generate_code_verifier();
    code_challenge_ = generate_code_challenge(code_verifier_);
    state_ = generate_random_string(16);

    // Build authorization URL
    std::string auth_url = get_authorization_url(DEFAULT_REDIRECT_URI, state_, code_challenge_);

    // Extract port from redirect_uri
    std::string redirect_uri = DEFAULT_REDIRECT_URI;
    size_t port_pos = redirect_uri.find_last_of(':');
    if (port_pos == std::string::npos) {
        last_error_ = "Invalid redirect URI format";
        return false;
    }

    std::string port_str = redirect_uri.substr(port_pos + 1);
    int port = std::stoi(port_str);

    // Create httplib server
    httplib::Server server;

    std::string auth_code;
    std::atomic<bool> received(false);
    std::mutex callback_mutex;

    // Add route for OAuth callback
    server.Get("/", [&auth_code, &received, &callback_mutex](const httplib::Request& req, httplib::Response& res) {
        // Log all query parameters
        std::cout << "OAuth callback received with query parameters:" << std::endl;
        for (const auto& param : req.params) {
            std::cout << "  " << param.first << " = " << param.second << std::endl;
        }

        // Extract authorization code from query parameters
        auto code_it = req.params.find("code");
        if (code_it != req.params.end()) {
            std::string code = code_it->second;
            std::cout << "OAuth authorization code received: " << code << std::endl;
            std::lock_guard<std::mutex> lock(callback_mutex);
            auth_code = code;
            received = true;

            res.set_content("<html><body><h1>Authentication Successful!</h1><p>You can close this window now.</p></body></html>", "text/html");
            return;
        }

        auto error_it = req.params.find("error");
        if (error_it != req.params.end()) {
            std::cout << "OAuth error received: " << error_it->second << std::endl;
            received = true;
            res.set_content("<html><body><h1>Authentication Failed.</h1><p>Error: " + error_it->second + "</p></body></html>", "text/html");
            return;
        }

        res.set_content("<html><body><h1>Waiting for authentication...</h1></body></html>", "text/html");
    });

    // Start server in background thread
    std::thread server_thread([&server, port]() {
        server.listen("127.0.0.1", port);
    });

    // Launch browser
    if (!launch_browser(auth_url)) {
        last_error_ = "Failed to launch browser";
        server.stop();
        server_thread.join();
        return false;
    }

    // Wait for authorization code (with timeout)
    int elapsed = 0;
    while (!received && elapsed < timeout_seconds) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        elapsed++;
    }

    // Stop server
    server.stop();
    server_thread.join();

    if (!received) {
        last_error_ = "Authorization timeout";
        return false;
    }

    // Exchange code for tokens (both access_token and refresh_token)
    return exchange_code_for_token(auth_code, code_verifier_);
}

bool EmailOptGmailImpl::refresh_token() {
    if (refresh_token_.empty()) {
        last_error_ = "No refresh token available";
        return false;
    }

    // Use httplib client to refresh token
    httplib::SSLClient client("oauth2.googleapis.com", 443);

    std::string post_data = "client_id=" + client_id_ +
                           "&client_secret=" + client_secret_ +
                           "&refresh_token=" + refresh_token_ +
                           "&grant_type=refresh_token";

    httplib::Headers headers = {
        {"Content-Type", "application/x-www-form-urlencoded"}
    };

    auto res = client.Post("/token", headers, post_data, "application/x-www-form-urlencoded");

    if (!res || res->status != 200) {
        last_error_ = "Token refresh failed with status: " + std::to_string(res ? res->status : 0);
        return false;
    }

    // Parse JSON response to extract access token
    std::string access_token = parse_json_field(res->body, "access_token");
    if (access_token.empty()) {
        last_error_ = "Failed to parse access token from response";
        return false;
    }

    access_token_ = access_token;
    is_valid_ = true;

    return true;
}

void EmailOptGmailImpl::set_access_token(const std::string& token, const std::string& email) {
    access_token_ = token;
    if (!email.empty()) {
        email_ = email;
    }
    is_valid_ = !token.empty();
}

void EmailOptGmailImpl::set_refresh_token(const std::string& token) {
    refresh_token_ = token;
}

std::string EmailOptGmailImpl::generate_random_string(size_t length) {
    const std::string chars = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-._~";
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, chars.length() - 1);

    std::string result;
    result.reserve(length);
    for (size_t i = 0; i < length; ++i) {
        result += chars[dis(gen)];
    }
    return result;
}

std::string EmailOptGmailImpl::generate_code_verifier() {
    // Generate 32 random bytes and base64url encode them
    std::vector<uint8_t> random_bytes(32);
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 255);

    for (size_t i = 0; i < 32; ++i) {
        random_bytes[i] = static_cast<uint8_t>(dis(gen));
    }

    return base64_url_encode_bytes(random_bytes);
}

std::string EmailOptGmailImpl::generate_code_challenge(const std::string& verifier) {
    // SHA256 hash the verifier
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256_CTX sha256;
    SHA256_Init(&sha256);
    SHA256_Update(&sha256, verifier.c_str(), verifier.length());
    SHA256_Final(hash, &sha256);

    // Base64url encode the hash
    std::vector<uint8_t> hash_bytes(hash, hash + SHA256_DIGEST_LENGTH);
    return base64_url_encode_bytes(hash_bytes);
}

std::string EmailOptGmailImpl::base64_url_encode(const std::string& input) {
    std::vector<uint8_t> bytes(input.begin(), input.end());
    return base64_url_encode_bytes(bytes);
}

std::string EmailOptGmailImpl::base64_url_encode_bytes(const std::vector<uint8_t>& input) {
    static const std::string chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string result;

    for (size_t i = 0; i < input.size(); i += 3) {
        uint32_t triple = (input[i] << 16);
        if (i + 1 < input.size()) triple |= (input[i + 1] << 8);
        if (i + 2 < input.size()) triple |= input[i + 2];

        result += chars[(triple >> 18) & 0x3F];
        result += chars[(triple >> 12) & 0x3F];
        if (i + 1 < input.size()) result += chars[(triple >> 6) & 0x3F];
        if (i + 2 < input.size()) result += chars[triple & 0x3F];
    }

    return result;
}

std::string EmailOptGmailImpl::base64_url_decode(const std::string& input) {
    static const std::string chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::vector<uint8_t> result;

    int val = 0, valb = -8;
    for (unsigned char c : input) {
        if (c == '=') break;
        size_t pos = chars.find(c);
        if (pos == std::string::npos) continue;

        val = (val << 6) + pos;
        valb += 6;
        if (valb >= 0) {
            result.push_back((val >> valb) & 0xFF);
            valb -= 8;
        }
    }

    return std::string(result.begin(), result.end());
}

// void EmailOptGmailImpl::init_imap_client() {
//     // IMAP layer removed - function disabled
// }

bool EmailOptGmailImpl::select_folder(const std::string& folder_name) {
    if (!store_ || !store_->isConnected()) {
        if (!connect_()) {
            last_error_ = "Not connected";
            return false;
        }
    }
    try {
        vmime::shared_ptr<vmime::net::folder> f;
        if (folder_name == "INBOX") {
            f = store_->getDefaultFolder();
        } else {
            vmime::net::folder::path path;
            path.appendComponent(vmime::net::folder::path::component(folder_name));
            f = store_->getFolder(path);
        }
        f->open(vmime::net::folder::MODE_READ_ONLY);
        return true;
    } catch (const vmime::exception& e) {
        last_error_ = std::string("select_folder failed: ") + e.what();
        LOG_INFO("Gmail select_folder - vmime exception: %s\n", e.what());
        return false;
    }
}

std::vector<std::string> EmailOptGmailImpl::fetch_emails_since_uid(const std::string& folder, const std::string& start_uid) {
    std::vector<std::string> uids;
    if (!store_ || !store_->isConnected()) {
        if (!connect_()) {
            last_error_ = "Not connected";
            return uids;
        }
    }
    try {
        vmime::shared_ptr<vmime::net::folder> f;
        if (folder == "INBOX") {
            f = store_->getDefaultFolder();
        } else {
            vmime::net::folder::path path;
            path.appendComponent(vmime::net::folder::path::component(folder));
            f = store_->getFolder(path);
        }
        f->open(vmime::net::folder::MODE_READ_ONLY);

        std::string effective_start = start_uid.empty() ? "1" : start_uid;
        vmime::net::messageSet msgs = vmime::net::messageSet::byUID(
            vmime::net::message::uid(effective_start),
            vmime::net::message::uid("*")
        );

        std::vector<vmime::shared_ptr<vmime::net::message>> messages = f->getMessages(msgs);
        for (const auto& msg : messages) {
            uids.push_back(msg->getUID());
        }
    } catch (const vmime::exception& e) {
        last_error_ = std::string("fetch_emails_since_uid failed: ") + e.what();
        LOG_INFO("Gmail fetch_emails_since_uid - vmime exception: %s\n", e.what());
    }
    return uids;
}

std::string EmailOptGmailImpl::get_email(const std::string& folder, const std::string& uid) {
    if (!store_ || !store_->isConnected()) {
        if (!connect_()) {
            last_error_ = "Not connected";
            return "";
        }
    }
    try {
        vmime::shared_ptr<vmime::net::folder> f;
        if (folder == "INBOX") {
            f = store_->getDefaultFolder();
        } else {
            vmime::net::folder::path path;
            path.appendComponent(vmime::net::folder::path::component(folder));
            f = store_->getFolder(path);
        }
        f->open(vmime::net::folder::MODE_READ_ONLY);

        vmime::net::messageSet msgs = vmime::net::messageSet::byUID(vmime::net::message::uid(uid));
        std::vector<vmime::shared_ptr<vmime::net::message>> messages = f->getMessages(msgs);
        if (messages.empty()) {
            last_error_ = "Message not found";
            return "";
        }

        vmime::shared_ptr<vmime::net::message> msg = messages[0];
        vmime::shared_ptr<vmime::message> parsedMsg = msg->getParsedMessage();

        std::string result;
        vmime::utility::outputStreamStringAdapter os(result);
        parsedMsg->generate(vmime::generationContext::getDefaultContext(), os);
        os.flush();

        return result;
    } catch (const vmime::exception& e) {
        last_error_ = std::string("get_email failed: ") + e.what();
        LOG_INFO("Gmail get_email - vmime exception: %s\n", e.what());
        return "";
    }
}

bool EmailOptGmailImpl::send_email(const std::string& folder, const std::string& content) {
    LOG_INFO("Gmail send_email - starting\n");

    if (access_token_.empty()) {
        if (!refresh_token()) {
            last_error_ = "No access token and refresh failed";
            LOG_INFO("Gmail send_email - no access token, refresh failed\n");
            return false;
        }
    }

    try {
        nlohmann::json json_content = nlohmann::json::parse(content);
        std::string recipient   = json_content.value("recipient", "");
        std::string subject     = json_content.value("subject", "");
        std::string body        = json_content.value("body", "");
        std::string in_reply_to = json_content.value("in_reply_to", "");
        std::string message_id  = json_content.value("message_id", "");
        std::string session_id  = json_content.value("session_id", "");
        std::string x_message_id = json_content.value("x_message_id", "");
        std::string x_session_chart = json_content.value("x_session_chart", "");

        LOG_INFO("Gmail send_email - parsed: recipient='%s', subject='%s', in_reply_to='%s'\n",
                 recipient.c_str(), subject.c_str(), in_reply_to.c_str());

        if (recipient.empty()) {
            last_error_ = "No recipient in JSON";
            return false;
        }

        init_vmime_platform();

        vmime::shared_ptr<vmime::net::session> session = vmime::net::session::create();
        vmime::propertySet& props = session->getProperties();
        props["connection.timeout"] = "30";
        props["smtp.timeout"] = "30";
        props["transport.smtp.options.need-authentication"] = "true";
        props["transport.smtp.auth.username"] = email_;
        props["transport.smtp.auth.accesstoken"] = access_token_;
        props["ssl.validate-certificates"] = "false";
        props["ssl.check-server-identity"] = "false";

        std::string smtp_server = smtp_server_.empty() ? "smtp.gmail.com" : smtp_server_;
        int smtp_port = smtp_port_ > 0 ? smtp_port_ : 587;
        std::string url_str = "smtp://" + smtp_server + ":" + std::to_string(smtp_port);
        vmime::utility::url url(url_str);

        LOG_INFO("Gmail send_email - SMTP URL: %s\n", url_str.c_str());

        vmime::shared_ptr<vmime::net::transport> tr = session->getTransport(url);
        tr->setCertificateVerifier(vmime::make_shared<GmailTrustAllCertificateVerifier>());

        // Build message
        vmime::messageBuilder builder;
        builder.setExpeditor(vmime::mailbox(email_));

        vmime::addressList toList;
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

        // Resolve session ID early for potential body encryption
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
            LOG_INFO("Gmail send_email: x_session_chart=new, created session_id=%s\n", sid.c_str());
        }

        // For exchange or reply, find session via in_reply_to
        if (sid.empty() && !in_reply_to.empty()) {
            static SessionRepo s_sessionRepo;
            sid = s_sessionRepo.querySessionByInReplyTo(in_reply_to, email_);
            LOG_INFO("Gmail send_email: x_session_chart=%s, found session_id=%s via in_reply_to=%s\n",
                     x_session_chart.c_str(), sid.c_str(), in_reply_to.c_str());
        }

        LOG_INFO("Gmail send_email: using session_id=%s\n", sid.c_str());

        // For x_session_chart=data, encrypt the body
        std::string bodyToSend = body;
        if (x_session_chart == "data") {
            std::vector<char> encBody(2 * 1024 * 1024);
            int encRc = email_prepare_data_body(body.c_str(), recipient.c_str(), email_.c_str(), sid.c_str(), encBody.data(), (int)encBody.size());
            if (encRc == 0) {
                bodyToSend = encBody.data();
                LOG_INFO("Gmail send_email: encrypted data body, len=%zu\n", bodyToSend.size());
            } else {
                LOG_INFO("Gmail send_email: email_prepare_data_body failed, rc=%d, sending plaintext\n", encRc);
            }
        }

        builder.getTextPart()->setText(vmime::make_shared<vmime::stringContentHandler>(bodyToSend));
        vmime::shared_ptr<vmime::message> msg = builder.construct();

        // Set Message-ID
        std::string msg_id;
        if (!message_id.empty()) {
            msg_id = message_id;
            if (msg_id.front() != '<') msg_id = "<" + msg_id + ">";
        } else {
            msg_id = "<" + generate_random_string(24) + "@gmail.com>";
        }
        msg->getHeader()->MessageId()->setValue(msg_id);

        // Set In-Reply-To and References
        if (!in_reply_to.empty() && in_reply_to != "<>" && in_reply_to != "<<> <>") {
            std::string irt = in_reply_to;
            if (irt.front() != '<') irt = "<" + irt + ">";
            if (irt != "<>" && irt != "< >") {
                msg->getHeader()->InReplyTo()->setValue(irt);
                msg->getHeader()->References()->setValue(irt);
                LOG_INFO("Gmail send_email - set In-Reply-To and References: %s\n", irt.c_str());
            }
        }

        // Add custom headers
        if (!x_message_id.empty()) {
            vmime::shared_ptr<vmime::headerField> xMsgIdField =
                vmime::headerFieldFactory::getInstance()->create("X-Message-ID", x_message_id);
            msg->getHeader()->appendField(xMsgIdField);
        }
        if (!x_session_chart.empty()) {
            vmime::shared_ptr<vmime::headerField> xChartField =
                vmime::headerFieldFactory::getInstance()->create("X-Session-Chart", x_session_chart);
            msg->getHeader()->appendField(xChartField);
            LOG_INFO("Gmail send_email - set X-Session-Chart: %s\n", x_session_chart.c_str());
        }

        // Send
        tr->send(msg);
        LOG_INFO("Gmail send_email - email sent successfully\n");

        // Insert sent email into database and session
        time_t now = time(NULL);
        struct tm* tm_info = localtime(&now);
        char date_str[64];
        strftime(date_str, sizeof(date_str), "%a, %d %b %Y %H:%M:%S %z", tm_info);

        char json_buffer[8192];
        int insert_result = email_insert_sent_email(
            email_.c_str(), email_.c_str(), email_.c_str(),
            recipient.c_str(), subject.c_str(), date_str,
            msg_id.c_str(), in_reply_to.c_str(), bodyToSend.c_str(),
            data_dir_.c_str(), json_buffer, sizeof(json_buffer)
        );

        if (insert_result == 0) {
            LOG_INFO("Gmail send_email: inserted sent email to database\n");
            try {
                nlohmann::json ins_response = nlohmann::json::parse(json_buffer);
                if (ins_response.contains("uuid")) {
                    std::string email_id = ins_response["uuid"].get<std::string>();

                    char session_buffer[8192];
                    int encMethod = (x_session_chart == "data") ? 1 : 0;
                    int session_result = email_add_email_to_session(
                        sid.c_str(), email_id.c_str(), email_.c_str(),
                        encMethod, session_buffer, sizeof(session_buffer)
                    );

                    if (session_result == 0) {
                        LOG_INFO("Gmail send_email: added email to session %s\n", sid.c_str());
                    } else {
                        LOG_INFO("Gmail send_email: failed to add email to session\n");
                    }
                }
            } catch (const std::exception& e) {
                LOG_INFO("Gmail send_email: failed to parse insert response: %s\n", e.what());
            }
        } else {
            LOG_INFO("Gmail send_email: failed to insert sent email to database\n");
        }

        return true;

    } catch (const vmime::exceptions::authentication_error& e) {
        last_error_ = std::string("Authentication Error: ") + e.what();
        LOG_INFO("Gmail send_email - auth error: %s\n", e.what());
        return false;
    } catch (const vmime::exception& e) {
        last_error_ = std::string("VMime exception: ") + e.what();
        LOG_INFO("Gmail send_email - vmime exception: %s\n", e.what());
        return false;
    } catch (const std::exception& e) {
        last_error_ = std::string("Exception: ") + e.what();
        LOG_INFO("Gmail send_email - exception: %s\n", e.what());
        return false;
    }
}

std::string EmailOptGmailImpl::fetch_email_headers(const std::string& folder, const std::string& start_uid) {
    LOG_INFO("Gmail fetch_email_headers - folder=%s, start_uid=%s\n", folder.c_str(), start_uid.c_str());

    if (!store_ || !store_->isConnected()) {
        LOG_INFO("Gmail fetch_email_headers - not connected, calling connect_()\n");
        if (!connect_()) {
            return R"({"status":"failed","error":"connect_failed"})";
        }
    }

    try {
        vmime::shared_ptr<vmime::net::imap::IMAPStore> imapStore =
            vmime::dynamic_pointer_cast<vmime::net::imap::IMAPStore>(store_);
        if (!imapStore) {
            return R"({"status":"failed","error":"not_imap_store"})";
        }

        vmime::shared_ptr<vmime::net::imap::IMAPConnection> conn = imapStore->getConnection();
        if (!conn) {
            return R"({"status":"failed","error":"no_connection"})";
        }

        // Map folder name
        std::string folderPath = folder;
        if (folderPath.find(' ') != std::string::npos && folderPath.front() != '"') {
            folderPath = "\"" + folderPath + "\"";
        }

        // SELECT folder
        vmime::shared_ptr<vmime::net::imap::IMAPCommand> selectCmd =
            vmime::net::imap::IMAPCommand::createCommand("SELECT " + folderPath);
        conn->sendCommand(selectCmd);
        vmime::shared_ptr<vmime::net::imap::IMAPParser::response> selectResp(conn->readResponse());

        LOG_INFO("Gmail fetch_email_headers - SELECT folder=%s\n", folderPath.c_str());

        if (!selectResp || selectResp->isBad()) {
            return R"({"status":"failed","error":"select_failed"})";
        }

        // Build message set
        std::string effective_start_uid = start_uid;
        if (start_uid.empty() || start_uid == "0") {
            effective_start_uid = "1";
        }

        vmime::net::messageSet msgs = vmime::net::messageSet::byUID(
            vmime::net::message::uid(effective_start_uid),
            vmime::net::message::uid("*")
        );

        LOG_INFO("Gmail fetch_email_headers - message set built, start_uid=%s, effective_start_uid=%s\n",
                 start_uid.c_str(), effective_start_uid.c_str());

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

        vmime::net::fetchAttributes attribsWithUID(fetchAttrs);
        attribsWithUID.add(vmime::net::fetchAttributes::UID);

        // Send FETCH
        vmime::shared_ptr<vmime::net::imap::IMAPCommand> fetchCmd =
            vmime::net::imap::IMAPUtils::buildFetchCommand(conn, msgs, attribsWithUID);
        LOG_INFO("Gmail fetch_email_headers - sending FETCH\n");
        fetchCmd->send(conn);

        vmime::shared_ptr<vmime::net::imap::IMAPParser::response> fetchResp(conn->readResponse());
        LOG_INFO("Gmail fetch_email_headers - FETCH response received, isBad=%d\n", fetchResp ? fetchResp->isBad() : -1);

        if (!fetchResp || fetchResp->isBad()) {
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

        // Parse response
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
                        if (!start_uid.empty() && start_uid != "0" && start_uid != "1" && uidStr == start_uid) {
                            email_obj.clear();
                            break;
                        }
                        email_obj["uuid"] = uidStr;
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
                                    case vmime::net::imap::IMAPParser::flag::ANSWERED: flagStr = "\\Answered"; break;
                                    case vmime::net::imap::IMAPParser::flag::FLAGGED: flagStr = "\\Flagged"; break;
                                    case vmime::net::imap::IMAPParser::flag::DELETED: flagStr = "\\Deleted"; break;
                                    case vmime::net::imap::IMAPParser::flag::SEEN: flagStr = "\\Seen"; break;
                                    case vmime::net::imap::IMAPParser::flag::DRAFT: flagStr = "\\Draft"; break;
                                    case vmime::net::imap::IMAPParser::flag::STAR: flagStr = "\\*"; break;
                                    case vmime::net::imap::IMAPParser::flag::KEYWORD_OR_EXTENSION:
                                        if (flag->flag_keyword) flagStr = flag->flag_keyword->value;
                                        break;
                                    case vmime::net::imap::IMAPParser::flag::UNKNOWN: flagStr = flag->name; break;
                                }
                                if (!flagStr.empty()) flagsArray.push_back(flagStr);
                            }
                        }
                        email_obj["flags"] = flagsArray;
                    }
                }
            }

            // Parse headers
            auto caseInsensitiveLess = [](const std::string& a, const std::string& b) {
                return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(),
                    [](char c1, char c2) { return ::tolower(c1) < ::tolower(c2); });
            };
            std::map<std::string, std::string, decltype(caseInsensitiveLess)> headerMap(caseInsensitiveLess);
            std::vector<std::string> receivedHeaders;
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
                        std::string generated;
                        vmime::utility::outputStreamStringAdapter os(generated);
                        val->generate(vmime::generationContext::getDefaultContext(), os, 0);
                        os.flush();
                        fValue = generated;
                    }
                    if (strcasecmp(fName.c_str(), "Received") == 0) {
                        receivedHeaders.push_back(fValue);
                    }
                    headerMap[fName] = fValue;
                }
            }

            auto getHeader = [&](const std::string& name) -> std::string {
                auto it = headerMap.find(name);
                return (it != headerMap.end()) ? it->second : "";
            };

            auto decodeHeader = [](const std::string& raw) -> std::string {
                if (raw.empty()) return raw;
                try {
                    auto decoded = vmime::text::decodeAndUnfold(raw);
                    if (decoded) return decoded->getConvertedText(vmime::charset("utf-8"));
                    return raw;
                } catch (...) { return raw; }
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

            // Extract service receive time from Received header
            std::string servicerecvtime;
            if (!receivedHeaders.empty()) {
                const std::string& receivedHeader = receivedHeaders[0];
                size_t semiPos = receivedHeader.rfind(';');
                if (semiPos != std::string::npos) {
                    std::string datePart = receivedHeader.substr(semiPos + 1);
                    size_t s = datePart.find_first_not_of(" \t\r\n");
                    if (s != std::string::npos) {
                        servicerecvtime = datePart.substr(s);
                        size_t e = servicerecvtime.find_last_not_of(" \t\r\n");
                        if (e != std::string::npos) servicerecvtime = servicerecvtime.substr(0, e + 1);
                    }
                }
            }
            email_obj["servicerecvtime"] = servicerecvtime;

            if (!email_obj.is_null() && !email_obj.empty() && !email_obj["uuid"].is_null()) {
                emails_array.push_back(email_obj);
            }
        }

        LOG_INFO("Gmail fetch_email_headers - parsed %zu emails\n", emails_array.size());

        nlohmann::json response;
        response["status"] = "success";
        response["folder"] = folder;
        response["count"] = emails_array.size();
        response["emails"] = emails_array;

        if (emails_array.size() > 0 && email_handler_) {
            nlohmann::json notification;
            notification["account"] = email_;
            notification["count"] = emails_array.size();
            notification["emails"] = emails_array;
            notification["folder"] = folder;
            email_handler_->notify(nullptr, 3, notification.dump());
            LOG_INFO("Gmail fetch_email_headers - notified UI about %zu new emails\n", emails_array.size());
        }

        return response.dump();

    } catch (const vmime::exception& e) {
        last_error_ = std::string("Failed to fetch email headers: ") + e.what();
        LOG_INFO("Gmail fetch_email_headers - vmime exception: %s\n", e.what());

        std::string what = e.what();
        if (what.find("ECONNRESET") != std::string::npos ||
            what.find("connection reset") != std::string::npos ||
            what.find("broken pipe") != std::string::npos) {
            if (store_) store_->disconnect();
            if (connect_()) {
                return fetch_email_headers(folder, start_uid);
            }
        }
        return R"({"status":"failed","error":"vmime_exception"})";
    } catch (const std::exception& e) {
        last_error_ = std::string("Failed to fetch email headers: ") + e.what();
        LOG_INFO("Gmail fetch_email_headers - exception: %s\n", e.what());
        return R"({"status":"failed","error":"exception"})";
    }
}

bool EmailOptGmailImpl::launch_browser(const std::string& url) {
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

std::string EmailOptGmailImpl::get_authorization_url(const std::string& redirect_uri,
                                                   const std::string& state,
                                                   const std::string& code_challenge) const {
    std::string url = std::string("https://accounts.google.com/o/oauth2/v2/auth?") +
                     "client_id=" + client_id_ +
                     "&response_type=code" +
                     "&redirect_uri=" + redirect_uri +
                     "&scope=" + std::string(DEFAULT_SCOPE) +
                     "&state=" + state +
                     "&code_challenge=" + code_challenge +
                     "&code_challenge_method=S256";
    return url;
}

bool EmailOptGmailImpl::exchange_code_for_token(const std::string& code,
                                               const std::string& code_verifier) {
    // Use httplib client to exchange code for token
    httplib::SSLClient client("oauth2.googleapis.com", 443);

    std::string post_data = "client_id=" + client_id_ +
                           "&client_secret=" + client_secret_ +
                           "&code=" + code +
                           "&grant_type=authorization_code" +
                           "&code_verifier=" + code_verifier;

    httplib::Headers headers = {
        {"Content-Type", "application/x-www-form-urlencoded"}
    };

    auto res = client.Post("/token", headers, post_data, "application/x-www-form-urlencoded");

    if (!res || res->status != 200) {
        last_error_ = "Token exchange failed with status: " + std::to_string(res ? res->status : 0);
        return false;
    }

    // Parse JSON response
    std::string access_token = parse_json_field(res->body, "access_token");
    std::string refresh_token = parse_json_field(res->body, "refresh_token");

    if (access_token.empty()) {
        last_error_ = "Failed to parse access token from response";
        return false;
    }

    access_token_ = access_token;
    refresh_token_ = refresh_token;
    is_valid_ = true;

    return true;
}

std::string EmailOptGmailImpl::parse_json_field(const std::string& json, const std::string& field) {
    // Simple JSON field extraction
    std::string search = "\"" + field + "\":";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return "";

    pos += search.length();

    // Skip whitespace
    while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n')) {
        pos++;
    }

    if (pos >= json.length()) return "";

    // Check if value is a string
    if (json[pos] == '"') {
        pos++; // Skip opening quote
        size_t end = json.find('"', pos);
        if (end == std::string::npos) return "";
        return json.substr(pos, end - pos);
    }

    // Value is not a string, read until comma or closing brace
    size_t end = json.find_first_of(",}", pos);
    if (end == std::string::npos) return "";
    return json.substr(pos, end - pos);
}

} // namespace EmailComm
