#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "email_opt_outlook_impl.h"
#include "email_core.h"
#include <nlohmann/json.hpp>
#include "email_opt_interface.h"
#include "email_handler.h"
#include "logger.h"
#include <vmime/vmime.hpp>
#include <vmime/platforms/posix/posixHandler.hpp>
#include <vmime/security/sasl/XOAuth2SASLMechanism.hpp>
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
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <openssl/sha.h>
#include <fstream>
#include <sstream>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <fcntl.h>
#include <algorithm>
#include <cctype>
#include <curl/curl.h>
#include <vmime/header.hpp>
#include <vmime/mediaType.hpp>
#include <vmime/net/socket.hpp>
#include <vmime/text.hpp>
#include <vmime/charset.hpp>
#include <vmime/charsetConverter.hpp>
#include <functional>
#include <map>
#include <vmime/security/sasl/XOAuth2SASLMechanism.hpp>
#include <vmime/security/sasl/XOAuth2SASLAuthenticator.hpp>
#include <vmime/security/cert/defaultCertificateVerifier.hpp>
#include <nlohmann/json.hpp>

// Custom certificate verifier that accepts all certificates
class TrustAllCertificateVerifier : public vmime::security::cert::defaultCertificateVerifier {
public:
    void verify(
        const vmime::shared_ptr<vmime::security::cert::certificateChain>& chain,
        const vmime::string& hostname
    ) override {
        // Accept all certificates without verification
        LOG_INFO("TrustAllCertificateVerifier: Accepting certificate for %s\n", hostname.c_str());
    }
};


namespace EmailComm {

EmailOptOutlookImpl::EmailOptOutlookImpl(std::shared_ptr<::oemailim::EmailHandler> email_handler)
    : email_handler_(email_handler),
      client_id_(DEFAULT_CLIENT_ID),
      email_(""),
      access_token_(""),
      refresh_token_(""),
      last_error_(""),
      is_valid_(false),
      imap_server_("outlook.office365.com"),
      imap_port_(993),
      smtp_server_("smtp.office365.com"),
      smtp_port_(587),
      data_dir_(""),
      account_type_("personal") {
}

EmailOptOutlookImpl::~EmailOptOutlookImpl() {
}

bool EmailOptOutlookImpl::connect() {
    return connect_();
}

bool EmailOptOutlookImpl::connect_() {
    // Check if already connected
    if (store_ && store_->isConnected()) {
        LOG_INFO("Outlook connect_ - already connected, returning true\n");
        return true;
    }

    LOG_INFO("Outlook connect_ - checking access token...\n");
    LOG_INFO("Outlook connect_ - email_: %s\n", email_.c_str());
    LOG_INFO("Outlook connect_ - access_token_ length: %zu\n", access_token_.length());
    is_valid_ = !access_token_.empty();
    if (!is_valid_) {
        LOG_INFO("Outlook connect_ - no access token, connection failed\n");
        return false;
    }

    if (email_.empty()) {
        LOG_INFO("Outlook connect_ - email is empty, connection failed\n");
        return false;
    }

    LOG_INFO("Outlook connect_ - establishing TCP connection to outlook.office365.com:993...\n");

    try {
        // Initialize vmime platform handler (only once)
        init_vmime_platform();

        // Create session using static method and save to member variable
        session_ = vmime::net::session::create();

        // Set connection timeout
        session_->getProperties()["connection.timeout"] = "30";
        session_->getProperties()["imap.timeout"] = "30";

        // Use official VMime XOAuth2SASLAuthenticator
        vmime::shared_ptr<vmime::security::sasl::XOAuth2SASLAuthenticator> xoauth2Auth =
            vmime::make_shared<vmime::security::sasl::XOAuth2SASLAuthenticator>(
                vmime::security::sasl::XOAuth2SASLAuthenticator::MODE_EXCLUSIVE
            );

        // Create IMAP store with imaps:// protocol and authenticator
        vmime::utility::url store_url("imaps", imap_server_, imap_port_);
        store_ = session_->getStore(store_url, xoauth2Auth);

        // Set authentication properties on the store
        store_->setProperty("options.need-authentication", true);
        store_->setProperty("auth.username", email_);
        store_->setProperty("auth.accesstoken", access_token_);

        // Set custom certificate verifier that accepts all certificates
        vmime::shared_ptr<TrustAllCertificateVerifier> verifier = vmime::make_shared<TrustAllCertificateVerifier>();
        store_->setCertificateVerifier(verifier);

        // Connect to the server
        store_->connect();

        LOG_INFO("Outlook connect_ - TCP connection established and login successful\n");
        is_valid_ = true;
        return true;

    } catch (const vmime::exception& e) {
        LOG_INFO("Outlook connect_ - vmime exception: %s\n", e.what());
        last_error_ = std::string("vmime exception: ") + e.what();
        is_valid_ = false;
        session_.reset();
        store_.reset();
        return false;
    } catch (const std::exception& e) {
        LOG_INFO("Outlook connect_ - std exception: %s\n", e.what());
        last_error_ = std::string("std exception: ") + e.what();
        is_valid_ = false;
        session_.reset();
        store_.reset();
        return false;
    }
}

bool EmailOptOutlookImpl::authority(int timeout_seconds) {
    LOG_INFO("Outlook authority: Starting authority with timeout %ds\n", timeout_seconds);

    // If we already have a refresh token, use it to get a new access token
    if (!refresh_token_.empty()) {
        LOG_INFO("Outlook authority: Using existing refresh token\n");
        return refresh_token();
    }

    LOG_INFO("Outlook authority: Starting full OAuth flow\n");

    // Otherwise, perform full OAuth flow to get refresh token
    // Generate PKCE code verifier and challenge
    LOG_INFO("Outlook authority: Generating PKCE code verifier\n");
    code_verifier_ = generate_code_verifier();
    code_challenge_ = generate_code_challenge(code_verifier_);
    state_ = generate_random_string(16);
    LOG_INFO("Outlook authority: PKCE generated\n");

    // Build authorization URL
    LOG_INFO("Outlook authority: Building authorization URL\n");
    std::string auth_url = get_authorization_url(DEFAULT_REDIRECT_URI, state_, code_challenge_);
    LOG_INFO("Outlook authority: Authorization URL: %s\n", auth_url.c_str());

    // Extract port from redirect_uri
    std::string redirect_uri = DEFAULT_REDIRECT_URI;
    size_t port_pos = redirect_uri.find_last_of(':');
    if (port_pos == std::string::npos) {
        last_error_ = "Invalid redirect URI format";
        LOG_INFO("Outlook authority: Invalid redirect URI format\n");
        return false;
    }

    std::string port_str = redirect_uri.substr(port_pos + 1);
    int port = std::stoi(port_str);
    LOG_INFO("Outlook authority: Port extracted: %d\n", port);

    // Create httplib server
    LOG_INFO("Outlook authority: Creating HTTP server on port %d\n", port);
    httplib::Server server;
    LOG_INFO("Outlook authority: HTTP server created\n");

    std::string auth_code;
    std::string callback_email;
    std::atomic<bool> received(false);
    std::mutex callback_mutex;

    // Add route for OAuth callback
    server.Get("/", [&auth_code, &callback_email, &received, &callback_mutex]
                   (const httplib::Request& req, httplib::Response& res) {
        LOG_INFO("OAuth callback received!\n");
        // Log all query parameters
        LOG_INFO("OAuth callback received with query parameters:\n");
        for (const auto& param : req.params) {
            LOG_INFO("  %s = %s\n", param.first.c_str(), param.second.c_str());
        }

        // Extract authorization code from query parameters
        auto code_it = req.params.find("code");
        if (code_it != req.params.end()) {
            std::string code = code_it->second;
            LOG_INFO("OAuth authorization code received: %s\n", code.c_str());

            // Try to extract email from callback URL parameters
            auto email_it = req.params.find("email");
            if (email_it != req.params.end()) {
                std::lock_guard<std::mutex> lock(callback_mutex);
                callback_email = email_it->second;
                LOG_INFO("OAuth callback email: %s\n", callback_email.c_str());
            }

            std::lock_guard<std::mutex> lock(callback_mutex);
            auth_code = code;
            received = true;

            res.set_content("<html><body><h1>Authorization successful!</h1><p>You can close this window now.</p></body></html>", "text/html");
            return;
        }

        auto error_it = req.params.find("error");
        if (error_it != req.params.end()) {
            LOG_INFO("OAuth error received: %s\n", error_it->second.c_str());
            received = true;
            res.set_content("<html><body><h1>Authorization failed.</h1><p>Error: " + error_it->second + "</p></body></html>", "text/html");
            return;
        }

        res.set_content("<html><body><h1>Waiting for authorization...</h1></body></html>", "text/html");
    });

    // Start server in background thread
    LOG_INFO("Outlook authority: Starting HTTP server on 127.0.0.1:%d\n", port);
    std::atomic<bool> server_failed(false);
    std::thread server_thread([&server, port, &server_failed]() {
        LOG_INFO("Server thread starting...\n");
        try {
            // httplib::Server::listen is blocking
            bool result = server.listen("127.0.0.1", port);
            LOG_INFO("Server thread ended, result: %d\n", result);
            if (!result) {
                server_failed = true;
            }
        } catch (const std::exception& e) {
            LOG_INFO("Server thread exception: %s", e.what());
            server_failed = true;
        }
    });
    
    // Wait a bit for server to start
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    // Check if server thread is still running
    if (server_thread.joinable()) {
        LOG_INFO("Server thread is running, detaching...\n");
        server_thread.detach();
    } else {
        LOG_INFO("Server thread already exited\n");
        server_failed = true;
    }
    
    // Give server more time to fully start
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    
    if (server_failed) {
        last_error_ = "Failed to start HTTP server on port " + std::to_string(port);
        LOG_INFO("Outlook authority: %s\n", last_error_.c_str());
        return false;
    }
    
    LOG_INFO("Outlook authority: HTTP server started successfully\n");

    // Launch browser
    LOG_INFO("Outlook authority: Launching browser\n");
    if (!launch_browser(auth_url)) {
        last_error_ = "Failed to launch browser";
        LOG_INFO("Outlook authority: Failed to launch browser\n");
        return false;
    }
    LOG_INFO("Outlook authority: Browser launched\n");

    // Wait for authorization code (with timeout)
    LOG_INFO("Outlook authority: Waiting for authorization code\n");
    int elapsed = 0;
    while (!received && elapsed < timeout_seconds) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        elapsed++;
    }
    LOG_INFO("Outlook authority: Wait completed, received=%d, elapsed=%ds\n", received.load(), elapsed);

    // Stop server
    LOG_INFO("Outlook authority: Stopping HTTP server\n");
    server.stop();
    LOG_INFO("Outlook authority: HTTP server stopped\n");

    if (!received) {
        last_error_ = "Authorization timeout";
        LOG_INFO("Outlook authority: Authorization timeout\n");
        return false;
    }

    // If email was found in callback, use it directly
    if (!callback_email.empty()) {
        email_ = callback_email;
        LOG_INFO("Outlook authority: Email from callback: %s\n", email_.c_str());
    }

    // Exchange code for tokens (both access_token and refresh_token)
    LOG_INFO("Outlook authority: Exchanging code for tokens\n");
    bool result = exchange_code_for_token(auth_code, redirect_uri, code_verifier_);
    LOG_INFO("Outlook authority: Token exchange result: %d\n", result);
    return result;
}

bool EmailOptOutlookImpl::refresh_token() {
    LOG_INFO("Outlook refresh_token: Starting refresh\n");
    if (refresh_token_.empty()) {
        LOG_INFO("Outlook refresh_token: No refresh token available\n");
        last_error_ = "No refresh token available";
        return false;
    }
    LOG_INFO("Outlook refresh_token: refresh_token length: %zu\n", refresh_token_.length());

    try {
        std::string token_url = "https://login.microsoftonline.com/" + std::string(DEFAULT_TENANT_ID) + "/oauth2/v2.0/token";
        
        std::string post_data = "client_id=" + url_encode(client_id_) +
                               "&refresh_token=" + url_encode(refresh_token_) +
                               "&grant_type=refresh_token" +
                               "&scope=" + url_encode(OUTLOOK_SCOPE);
        
        // Use system curl command to perform HTTPS POST
        std::string command = "curl -s -X POST \"" + token_url + "\" " +
                             "-H \"Content-Type: application/x-www-form-urlencoded\" " +
                             "-d \"" + post_data + "\"";

        FILE* pipe = popen(command.c_str(), "r");
        if (!pipe) {
            last_error_ = "Failed to execute curl command";
            LOG_INFO("Outlook refresh_token: Failed to execute curl command\n");
            return false;
        }

        char buffer[4096];
        std::string response;
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            response += buffer;
        }
        pclose(pipe);

        LOG_INFO("Outlook refresh_token: Token response body received (length: %zu)\n", response.length());

        if (response.empty()) {
            last_error_ = "Empty response from server";
            LOG_INFO("Outlook refresh_token: Empty response from server\n");
            return false;
        }

        // Parse JSON response to extract access token
        std::string access_token = parse_json_field(response, "access_token");
        if (access_token.empty()) {
            LOG_INFO("Outlook refresh_token: Failed to parse access token from response\n");
            last_error_ = "Failed to parse access token from response";
            return false;
        }

        access_token_ = access_token;
        is_valid_ = true;
        LOG_INFO("Outlook refresh_token: Successfully refreshed access token (len=%zu)\n", access_token.length());

        // Log scope from the token response
        std::string response_scope = parse_json_field(response, "scope");
        if (!response_scope.empty()) {
            LOG_INFO("Outlook refresh_token: response scope: %s\n", response_scope.c_str());
        }
        // Log first 50 chars of access token for debugging
        LOG_INFO("Outlook refresh_token: access_token prefix: %.50s\n", access_token.c_str());

        // Update refresh token if a new one is provided
        std::string new_refresh_token = parse_json_field(response, "refresh_token");
        if (!new_refresh_token.empty()) {
            refresh_token_ = new_refresh_token;
        }

        return true;
    } catch (const std::exception& e) {
        last_error_ = std::string("Exception during token refresh: ") + e.what();
        LOG_INFO("Outlook refresh_token: %s\n", last_error_.c_str());
        return false;
    }
}

bool EmailOptOutlookImpl::refresh_graph_token() {
    LOG_INFO("Outlook refresh_graph_token: Starting refresh\n");
    if (refresh_token_.empty()) {
        LOG_INFO("Outlook refresh_graph_token: No refresh token available\n");
        last_error_ = "No refresh token available";
        return false;
    }

    try {
        std::string token_url = "https://login.microsoftonline.com/" + std::string(DEFAULT_TENANT_ID) + "/oauth2/v2.0/token";

        std::string post_data = "client_id=" + url_encode(client_id_) +
                               "&refresh_token=" + url_encode(refresh_token_) +
                               "&grant_type=refresh_token" +
                               "&scope=" + url_encode(GRAPH_SCOPE);

        LOG_INFO("Outlook refresh_graph_token: Using curl to POST to %s\n", token_url.c_str());

        std::string command = "curl -s -X POST \"" + token_url + "\" " +
                             "-H \"Content-Type: application/x-www-form-urlencoded\" " +
                             "-d \"" + post_data + "\"";

        FILE* pipe = popen(command.c_str(), "r");
        if (!pipe) {
            last_error_ = "Failed to execute curl command";
            LOG_INFO("Outlook refresh_graph_token: Failed to execute curl command\n");
            return false;
        }

        char buffer[4096];
        std::string response;
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            response += buffer;
        }
        pclose(pipe);

        LOG_INFO("Outlook refresh_graph_token: Token response body received (length: %zu)\n", response.length());

        if (response.empty()) {
            last_error_ = "Empty response from server";
            LOG_INFO("Outlook refresh_graph_token: Empty response from server\n");
            return false;
        }

        std::string access_token = parse_json_field(response, "access_token");
        if (access_token.empty()) {
            LOG_INFO("Outlook refresh_graph_token: Failed to parse access token from response\n");
            last_error_ = "Failed to parse access token from response";
            return false;
        }

        graph_access_token_ = access_token;
        LOG_INFO("Outlook refresh_graph_token: Successfully refreshed graph access token (len=%zu)\n", access_token.length());

        std::string new_refresh_token = parse_json_field(response, "refresh_token");
        if (!new_refresh_token.empty()) {
            refresh_token_ = new_refresh_token;
        }

        return true;
    } catch (const std::exception& e) {
        last_error_ = std::string("Exception during graph token refresh: ") + e.what();
        LOG_INFO("Outlook refresh_graph_token: %s\n", last_error_.c_str());
        return false;
    }
}

void EmailOptOutlookImpl::set_access_token(const std::string& token, const std::string& email) {
    access_token_ = token;
    if (!email.empty()) {
        email_ = email;
    }
    is_valid_ = !token.empty();
}

void EmailOptOutlookImpl::set_refresh_token(const std::string& token) {
    refresh_token_ = token;
}

void EmailOptOutlookImpl::set_imap_server(const std::string& server, int port) {
    imap_server_ = server;
    imap_port_ = port;
    LOG_INFO("set_imap_server called: %s:%d\n", server.c_str(), port);
}

void EmailOptOutlookImpl::set_smtp_server(const std::string& server, int port) {
    smtp_server_ = server;
    smtp_port_ = port;
    LOG_INFO("set_smtp_server called: %s:%d\n", server.c_str(), port);
}

void EmailOptOutlookImpl::set_account_type(const std::string& type) {
    account_type_ = type;
}

std::string EmailOptOutlookImpl::get_account_type() const {
    return account_type_;
}

void EmailOptOutlookImpl::set_data_dir(const std::string& dir) {
    data_dir_ = dir;
}

std::string EmailOptOutlookImpl::get_data_dir() const {
    return data_dir_;
}

std::string EmailOptOutlookImpl::generate_random_string(size_t length) {
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

std::string EmailOptOutlookImpl::generate_code_verifier() {
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

std::string EmailOptOutlookImpl::generate_code_challenge(const std::string& verifier) {
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

std::string EmailOptOutlookImpl::base64_url_encode(const std::string& input) {
    std::vector<uint8_t> bytes(input.begin(), input.end());
    return base64_url_encode_bytes(bytes);
}

std::string EmailOptOutlookImpl::url_encode(const std::string& value) {
    static const char hex[] = "0123456789ABCDEF";
    std::ostringstream encoded;
    for (unsigned char c : value) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            encoded << static_cast<char>(c);
        } else {
            encoded << '%' << hex[(c >> 4) & 0xF] << hex[c & 0xF];
        }
    }
    return encoded.str();
}

std::string EmailOptOutlookImpl::base64_url_encode_bytes(const std::vector<uint8_t>& input) {
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

std::string EmailOptOutlookImpl::base64_encode_bytes(const std::vector<uint8_t>& input) {
    BIO* bio = BIO_new(BIO_s_mem());
    BIO* b64 = BIO_new(BIO_f_base64());
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    bio = BIO_push(b64, bio);
    BIO_write(bio, input.data(), static_cast<int>(input.size()));
    BIO_flush(bio);
    BUF_MEM* bufferPtr;
    BIO_get_mem_ptr(bio, &bufferPtr);
    std::string result(bufferPtr->data, bufferPtr->length);
    BIO_free_all(bio);
    return result;
}

std::string EmailOptOutlookImpl::wrap_base64_lines(const std::string& b64, size_t line_len) {
    std::string result;
    for (size_t i = 0; i < b64.size(); i += line_len) {
        if (!result.empty()) result += "\r\n";
        result += b64.substr(i, line_len);
    }
    return result;
}

std::string EmailOptOutlookImpl::base64_url_decode(const std::string& input) {
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

// void EmailOptOutlookImpl::init_imap_client() {
//     // IMAP layer removed - function disabled
// }

bool EmailOptOutlookImpl::select_folder(const std::string& folder_name) {
    if (!store_ || !store_->isConnected()) {
        if (!connect_()) {
            last_error_ = "select_folder: connect failed";
            return false;
        }
    }

    try {
        auto imapStore = vmime::dynamic_pointer_cast<vmime::net::imap::IMAPStore>(store_);
        if (!imapStore) {
            last_error_ = "select_folder: not IMAP store";
            return false;
        }

        vmime::net::folder::path fpath;
        fpath.appendComponent(vmime::net::folder::path::component(folder_name));
        auto folder = imapStore->getFolder(fpath);
        folder->open(vmime::net::folder::MODE_READ_WRITE);
        return true;
    } catch (const vmime::exception& e) {
        last_error_ = std::string("select_folder: ") + e.what();
        return false;
    }
}

std::vector<std::string> EmailOptOutlookImpl::fetch_emails_since_uid(const std::string& folder, const std::string& start_uid) {
    if (!store_ || !store_->isConnected()) {
        if (!connect_()) {
            last_error_ = "fetch_emails_since_uid: connect failed";
            return {};
        }
    }

    try {
        auto imapStore = vmime::dynamic_pointer_cast<vmime::net::imap::IMAPStore>(store_);
        if (!imapStore) {
            last_error_ = "fetch_emails_since_uid: not IMAP store";
            return {};
        }

        vmime::net::folder::path fpath;
        fpath.appendComponent(vmime::net::folder::path::component(folder));
        auto imapFolder = imapStore->getFolder(fpath);
        imapFolder->open(vmime::net::folder::MODE_READ_ONLY);

        std::vector<vmime::shared_ptr<vmime::net::message>> msgs;

        if (start_uid.empty()) {
            // Fetch all messages
            msgs = imapFolder->getMessages(vmime::net::messageSet::byNumber(1, -1));
        } else {
            // Fetch messages since start_uid
            msgs = imapFolder->getMessages(vmime::net::messageSet::byUID(
                vmime::net::message::uid(start_uid), vmime::net::message::uid("*")));
        }

        std::vector<std::string> results;
        for (const auto& msg : msgs) {
            results.push_back(static_cast<vmime::string>(msg->getUID()));
        }

        imapFolder->close(false);
        return results;
    } catch (const vmime::exception& e) {
        last_error_ = std::string("fetch_emails_since_uid: ") + e.what();
        return {};
    }
}

std::string EmailOptOutlookImpl::get_email(const std::string& folder, const std::string& uid) {
    if (!store_ || !store_->isConnected()) {
        LOG_INFO("Outlook get_email - not connected, calling connect_()\n");
        if (!connect_()) {
            last_error_ = "get_email: connect failed";
            return "";
        }
    }

    try {
        vmime::shared_ptr<vmime::net::folder> folder_obj = store_->getFolder(vmime::utility::path(folder));
        folder_obj->open(vmime::net::folder::MODE_READ_ONLY);

        vmime::net::messageSet allMessages = vmime::net::messageSet::byNumber(1, folder_obj->getMessageCount());
        std::vector<vmime::shared_ptr<vmime::net::message>> messages = folder_obj->getMessages(allMessages);
        for (const auto& msg : messages) {
            std::string msg_uid = msg->getUID();
            if (msg_uid == uid) {
                std::ostringstream oss;
                vmime::utility::outputStreamAdapter osa(oss);
                msg->extract(osa);
                folder_obj->close(false);
                LOG_INFO("Outlook get_email - successfully fetched email with uid: %s\n", uid.c_str());
                return oss.str();
            }
        }

        folder_obj->close(false);
        last_error_ = "get_email: email not found for uid " + uid;
        LOG_INFO("Outlook get_email - email not found for uid: %s\n", uid.c_str());
        return "";
    } catch (const vmime::exception& e) {
        last_error_ = std::string("get_email: vmime exception: ") + e.what();
        LOG_INFO("Outlook get_email - vmime exception: %s\n", e.what());
        return "";
    }
}

// Upload context for libcurl callback
struct upload_status {
    const char* data;
    size_t bytes_read;
    size_t total_size;
};

// XOAUTH2 authentication context
struct xoauth2_context {
    std::string user;
    std::string token;
};

// Callback function to provide email content to libcurl
static size_t payload_source(char *ptr, size_t size, size_t nmemb, void *userp) {
    struct upload_status *upload_ctx = (struct upload_status *)userp;
    
    size_t max_buffer = size * nmemb;
    if (max_buffer < 1 || upload_ctx->bytes_read >= upload_ctx->total_size) {
        return 0; // EOF: data finished
    }
    
    size_t copy_len = upload_ctx->total_size - upload_ctx->bytes_read;
    if (copy_len > max_buffer) {
        copy_len = max_buffer;
    }
    
    memcpy(ptr, upload_ctx->data + upload_ctx->bytes_read, copy_len);
    upload_ctx->bytes_read += copy_len;
    
    return copy_len;
}

// SASL callback for XOAUTH2 authentication
static CURLcode xoauth2_auth(void *clientp, const char *prompt, char *buffer, size_t buflen) {
    struct xoauth2_context *ctx = (struct xoauth2_context *)clientp;
    
    // Build XOAUTH2 string: user={email}\x01auth=Bearer {token}\x01\x01
    std::string xoauth2_str = "user=" + ctx->user + "\x01auth=Bearer " + ctx->token + "\x01\x01";
    
    if (xoauth2_str.length() >= buflen) {
        return CURLE_OUT_OF_MEMORY;
    }
    
    strcpy(buffer, xoauth2_str.c_str());
    return CURLE_OK;
}

bool EmailOptOutlookImpl::send_email(const std::string& folder, const std::string& content) {
    // Check if we have access token, if not try to refresh
    if (access_token_.empty()) {
        LOG_INFO("Outlook send_email: no access token available, trying to refresh\n");
        if (!refresh_token()) {
            last_error_ = "send_email: failed to refresh token: " + last_error_;
            LOG_INFO("Outlook send_email: %s\n", last_error_.c_str());
            return false;
        }
    }
    
    if (access_token_.empty()) {
        last_error_ = "send_email: no access token available after refresh";
        LOG_INFO("Outlook send_email: %s\n", last_error_.c_str());
        return false;
    }
    
    // Parse content as JSON: { "recipient": "...", "subject": "...", "body": "...", "in_reply_to": "..." }
    std::string recipient_str, subject_str, body_str, in_reply_to_str, message_id_str, session_id_str;
    try {
        auto j = nlohmann::json::parse(content);
        recipient_str = j.value("recipient", "");
        subject_str = j.value("subject", "");
        body_str = j.value("body", "");
        in_reply_to_str = j.value("in_reply_to", "");
        message_id_str = j.value("message_id", "");
        session_id_str = j.value("session_id", "");
    } catch (const std::exception& e) {
        last_error_ = std::string("send_email: invalid JSON content: ") + e.what();
        LOG_INFO("Outlook send_email: %s\n", last_error_.c_str());
        return false;
    }

    if (recipient_str.empty() || subject_str.empty()) {
        last_error_ = "send_email: missing recipient or subject";
        LOG_INFO("Outlook send_email: %s\n", last_error_.c_str());
        return false;
    }

    LOG_INFO("Outlook send_email: account_type=%s, to=%s, subject=%s\n",
            account_type_.c_str(), recipient_str.c_str(), subject_str.c_str());

    // Choose sending method based on account type
    LOG_INFO("Outlook send_email: session_id=%s\n", session_id_str.c_str());

    if (account_type_ == "personal") {
        return send_email_via_graph_api(recipient_str, subject_str, body_str, in_reply_to_str, message_id_str, session_id_str);
    } else {
        return send_email_via_vmime_smtp(recipient_str, subject_str, body_str, in_reply_to_str, message_id_str, session_id_str);
    }
}

bool EmailOptOutlookImpl::send_email_via_graph_api(const std::string& recipient, const std::string& subject, 
                                                     const std::string& body, const std::string& in_reply_to, 
                                                     const std::string& message_id, const std::string& session_id) {
    LOG_INFO("Outlook send_email_via_graph_api: sending via Microsoft Graph API\n");
    
    // Refresh Graph token with proper scope
    if (!refresh_graph_token()) {
        last_error_ = "send_email_via_graph_api: failed to refresh graph token: " + last_error_;
        LOG_INFO("Outlook send_email_via_graph_api: %s\n", last_error_.c_str());
        return false;
    }
    
    if (graph_access_token_.empty()) {
        last_error_ = "send_email_via_graph_api: no graph access token available";
        LOG_INFO("Outlook send_email_via_graph_api: %s\n", last_error_.c_str());
        return false;
    }
    
    // Build RFC 822 email message
    std::string msg_id;
    if (!message_id.empty()) {
        msg_id = message_id;
        if (msg_id.front() != '<') msg_id = "<" + msg_id + ">";
    } else {
        msg_id = "<" + generate_random_string(24) + "@outlook.com>";
    }
    
    std::string irt = in_reply_to;
    if (!irt.empty() && irt.front() != '<') {
        irt = "<" + irt + ">";
    }
    
    std::string email_msg;
    email_msg += "From: " + email_ + "\r\n";
    email_msg += "To: " + recipient + "\r\n";
    email_msg += "Subject: " + subject + "\r\n";
    email_msg += "Message-ID: " + msg_id + "\r\n";
    if (!irt.empty()) {
        email_msg += "In-Reply-To: " + irt + "\r\n";
        email_msg += "References: " + irt + "\r\n";
    }
    email_msg += "MIME-Version: 1.0\r\n";
    email_msg += "Content-Type: text/html; charset=utf-8\r\n";
    email_msg += "Content-Transfer-Encoding: 8bit\r\n";
    email_msg += "\r\n";
    email_msg += body;
    
    // Base64 encode the email message
    std::string email_b64 = base64_encode_bytes(std::vector<uint8_t>(email_msg.begin(), email_msg.end()));
    email_b64 = wrap_base64_lines(email_b64);
    
    // Build Graph API request body
    std::string json_body = R"({
        "message": {
            "subject": ")" + subject + R"(",
            "body": {
                "contentType": "HTML",
                "content": ")" + body + R"("
            },
            "toRecipients": [)";
    
    // Handle multiple recipients
    size_t start = 0, end;
    bool first = true;
    while ((end = recipient.find(',', start)) != std::string::npos) {
        std::string addr = recipient.substr(start, end - start);
        size_t b = addr.find_first_not_of(" \t");
        size_t e2 = addr.find_last_not_of(" \t");
        if (b != std::string::npos) {
            if (!first) json_body += ",";
            json_body += R"({"emailAddress":{"address":")" + addr.substr(b, e2 - b + 1) + R"("}})";
            first = false;
        }
        start = end + 1;
    }
    {
        std::string addr = recipient.substr(start);
        size_t b = addr.find_first_not_of(" \t");
        size_t e2 = addr.find_last_not_of(" \t");
        if (b != std::string::npos) {
            if (!first) json_body += ",";
            json_body += R"({"emailAddress":{"address":")" + addr.substr(b, e2 - b + 1) + R"("}})";
        }
    }
    
    json_body += R"(],
            "internetMessageHeaders": [
                {
                    "name": "X-Message-ID",
                    "value": ")" + msg_id + R"("
                }"; 
    
    if (!irt.empty()) {
        json_body += R"(,
                {
                    "name": "In-Reply-To",
                    "value": ")" + irt + R"("
                },
                {
                    "name": "References",
                    "value": ")" + irt + R"("
                }";
    }
    
    json_body += R"(
            ]
        }
    })";
    
    LOG_INFO("Outlook send_email_via_graph_api: JSON body: %s\n", json_body.c_str());
    
    // Single sendMail call with injected Message-ID via extended property
    // The server will use our Message-ID instead of generating its own
    std::string graph_url = "https://graph.microsoft.com/v1.0/me/sendMail";
    std::string command = "curl -s -X POST \"" + graph_url + "\" " +
                         "-H \"Authorization: Bearer " + graph_access_token_ + "\" " +
                         "-H \"Content-Type: application/json\" " +
                         "-d '" + json_body + "'";
    
    LOG_INFO("Outlook send_email_via_graph_api: sending via Graph API sendMail (injected msg_id=%s)\n", msg_id.c_str());
    
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        last_error_ = "send_email_via_graph_api: failed to execute curl command";
        LOG_INFO("Outlook send_email_via_graph_api: %s\n", last_error_.c_str());
        return false;
    }
    
    char buffer[4096];
    std::string response;
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        response += buffer;
    }
    int exit_code = pclose(pipe);
    
    if (exit_code != 0) {
        last_error_ = "send_email_via_graph_api: curl command failed with exit code " + std::to_string(exit_code);
        LOG_INFO("Outlook send_email_via_graph_api: %s\n", last_error_.c_str());
        LOG_INFO("Outlook send_email_via_graph_api: response: %s\n", response.c_str());
        return false;
    }
    
    LOG_INFO("Outlook send_email_via_graph_api: email sent successfully via Graph API\n");
    
    // Use local msg_id directly — server was forced to use it via extended property
    // IMAP sync will match this record by message_id
    LOG_INFO("Outlook send_email_via_graph_api: using msg_id=%s\n", msg_id.c_str());
    
    // Insert sent email into database and session
    try {
    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);
    char date_str[64];
    strftime(date_str, sizeof(date_str), "%a, %d %b %Y %H:%M:%S %z", tm_info);
    
    char json_buffer[8192] = {0};
    LOG_INFO("Outlook send_email_via_graph_api: calling email_insert_sent_email, data_dir='%s', msg_id='%s'\n", data_dir_.c_str(), msg_id.c_str());
    int insert_result = email_insert_sent_email(
        email_.c_str(),
        email_.c_str(),
        email_.c_str(),
        recipient.c_str(),
        subject.c_str(),
        date_str,
        msg_id.c_str(),
        irt.c_str(),
        body.c_str(),
        data_dir_.c_str(),
        json_buffer,
        sizeof(json_buffer)
    );

    LOG_INFO("Outlook send_email_via_graph_api: insert_result=%d, json_buffer='%s'\n", insert_result, json_buffer);

    if (insert_result == 0) {
        LOG_INFO("Outlook send_email_via_graph_api: inserted sent email to database\n");

        // Parse response to get email id
        try {
            nlohmann::json response = nlohmann::json::parse(json_buffer);
            if (response.contains("uuid")) {
                std::string email_id = response["uuid"].get<std::string>();
                
                // Use session_id passed from Dart (current conversation's session_id)
                LOG_INFO("Outlook send_email_via_graph_api: using session_id=%s\n", session_id.c_str());
                
                // Add email to session
                char session_buffer[8192];
                int session_result = email_add_email_to_session(
                    session_id.c_str(),
                    email_id.c_str(),
                    email_.c_str(),
                    session_buffer,
                    sizeof(session_buffer)
                );
                
                if (session_result == 0) {
                    LOG_INFO("Outlook send_email_via_graph_api: added email to session %s\n", session_id.c_str());
                    
                    // Notify UI that email was sent
                    if (email_handler_) {
                        nlohmann::json notify_json;
                        notify_json["session_id"] = session_id;
                        notify_json["email_id"] = email_id;
                        notify_json["message_id"] = msg_id;
                        email_handler_->notify(nullptr, NOTIFICATION_MESSAGE_EMAIL_SENT, notify_json.dump());
                    }
                } else {
                    LOG_INFO("Outlook send_email_via_graph_api: failed to add email to session\n");
                }
            }
        } catch (const std::exception& e) {
            LOG_INFO("Outlook send_email_via_graph_api: failed to parse insert response: %s\n", e.what());
        }
    } else {
        LOG_INFO("Outlook send_email_via_graph_api: failed to insert sent email to database\n");
    }
    } catch (const std::exception& e) {
        LOG_INFO("Outlook send_email_via_graph_api: exception during insert/session: %s\n", e.what());
    } catch (...) {
        LOG_INFO("Outlook send_email_via_graph_api: unknown exception during insert/session\n");
    }
    
    return true;
}

bool EmailOptOutlookImpl::send_email_via_vmime_smtp(const std::string& recipient, const std::string& subject, 
                                                      const std::string& body, const std::string& in_reply_to, 
                                                      const std::string& message_id, const std::string& session_id) {
    LOG_INFO("Outlook send_email_via_vmime_smtp: sending via vmime SMTP with XOAUTH2\n");
    
    try {
        // Initialize vmime platform if not already done
        static bool vmime_initialized = false;
        if (!vmime_initialized) {
            vmime::platform::setHandler<vmime::platforms::posix::posixHandler>();
            vmime_initialized = true;
        }
        
        // Create session
        vmime::shared_ptr<vmime::net::session> session = vmime::net::session::create();
        
        // Set session properties
        vmime::propertySet& props = session->getProperties();
        props["connection.timeout"] = "30";
        props["smtp.timeout"] = "30";
        props["transport.smtp.options.need-authentication"] = "true";
        props["transport.smtp.auth.username"] = email_;
        props["transport.smtp.auth.accesstoken"] = access_token_;
        props["ssl.validate-certificates"] = "false";
        props["ssl.check-server-identity"] = "false";
        
        // Build SMTP URL
        std::string smtp_server = smtp_server_.empty() ? "smtp.office365.com" : smtp_server_;
        int smtp_port = smtp_port_ > 0 ? smtp_port_ : 587;
        std::string url_str = "smtp://" + smtp_server + ":" + std::to_string(smtp_port);
        vmime::utility::url url(url_str);
        
        LOG_INFO("Outlook send_email_via_vmime_smtp: SMTP URL: %s\n", url_str.c_str());
        
        // Get transport
        vmime::shared_ptr<vmime::net::transport> tr = session->getTransport(url);
        
        // Set certificate verifier
        tr->setCertificateVerifier(vmime::make_shared<TrustAllCertificateVerifier>());
        
        // Build message using messageBuilder
        vmime::messageBuilder builder;
        builder.setExpeditor(vmime::mailbox(email_));
        
        // Set recipients (support comma-separated list)
        vmime::addressList toList;
        std::string recipientsStr = recipient;
        size_t start = 0, end;
        while ((end = recipientsStr.find(',', start)) != std::string::npos) {
            std::string addr = recipientsStr.substr(start, end - start);
            size_t b = addr.find_first_not_of(" \t");
            size_t e = addr.find_last_not_of(" \t");
            if (b != std::string::npos) {
                addr = addr.substr(b, e - b + 1);
                toList.appendAddress(vmime::make_shared<vmime::mailbox>(addr));
            }
            start = end + 1;
        }
        {
            std::string addr = recipientsStr.substr(start);
            size_t b = addr.find_first_not_of(" \t");
            size_t e = addr.find_last_not_of(" \t");
            if (b != std::string::npos) {
                addr = addr.substr(b, e - b + 1);
                toList.appendAddress(vmime::make_shared<vmime::mailbox>(addr));
            }
        }
        builder.setRecipients(toList);
        
        // Set subject
        builder.setSubject(vmime::text(subject, vmime::charset("UTF-8")));
        
        // Set body
        builder.getTextPart()->setCharset(vmime::charset("UTF-8"));
        builder.getTextPart()->setText(vmime::make_shared<vmime::stringContentHandler>(body));
        
        // Build the message
        vmime::shared_ptr<vmime::message> msg = builder.construct();
        
        // Set Message-ID
        std::string msg_id;
        if (!message_id.empty()) {
            msg_id = message_id;
            if (msg_id.front() != '<') msg_id = "<" + msg_id + ">";
        } else {
            msg_id = "<" + generate_random_string(24) + "@outlook.com>";
        }
        msg->getHeader()->MessageId()->setValue(msg_id);
        
        // Set In-Reply-To and References if replying
        if (!in_reply_to.empty()) {
            std::string irt = in_reply_to;
            if (irt.front() != '<') irt = "<" + irt + ">";
            msg->getHeader()->InReplyTo()->setValue(irt);
            msg->getHeader()->References()->setValue(irt);
        }
        
        // Send the message
        tr->send(msg);
        
        LOG_INFO("Outlook send_email_via_vmime_smtp: email sent successfully via vmime SMTP\n");
        
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
            body.c_str(),
            data_dir_.c_str(),
            json_buffer,
            sizeof(json_buffer)
        );
        
        if (insert_result == 0) {
            LOG_INFO("Outlook send_email_via_vmime_smtp: inserted sent email to database\n");
            
            // Parse response to get email id
            try {
                nlohmann::json response = nlohmann::json::parse(json_buffer);
                if (response.contains("uuid")) {
                    std::string email_id = response["uuid"].get<std::string>();
                    
                    // Use session_id passed from Dart (current conversation's session_id)
                    LOG_INFO("Outlook send_email_via_vmime_smtp: using session_id=%s\n", session_id.c_str());
                    
                    // Add email to session
                    char session_buffer[8192];
                    int session_result = email_add_email_to_session(
                        session_id.c_str(),
                        email_id.c_str(),
                        email_.c_str(),
                        session_buffer,
                        sizeof(session_buffer)
                    );
                    
                    if (session_result == 0) {
                        LOG_INFO("Outlook send_email_via_vmime_smtp: added email to session %s\n", session_id.c_str());
                        
                        // Notify UI that email was sent
                        if (email_handler_) {
                            nlohmann::json notify_json;
                            notify_json["session_id"] = session_id;
                            notify_json["email_id"] = email_id;
                            notify_json["message_id"] = msg_id;
                            email_handler_->notify(nullptr, NOTIFICATION_MESSAGE_EMAIL_SENT, notify_json.dump());
                        }
                    } else {
                        LOG_INFO("Outlook send_email_via_vmime_smtp: failed to add email to session\n");
                    }
                }
            } catch (const std::exception& e) {
                LOG_INFO("Outlook send_email_via_vmime_smtp: failed to parse insert response: %s\n", e.what());
            }
        } else {
            LOG_INFO("Outlook send_email_via_vmime_smtp: failed to insert sent email to database\n");
        }
        
        return true;
        
    } catch (const vmime::exception& e) {
        last_error_ = std::string("send_email_via_vmime_smtp: vmime exception: ") + e.what();
        LOG_INFO("Outlook send_email_via_vmime_smtp: %s\n", last_error_.c_str());
        return false;
    } catch (const std::exception& e) {
        last_error_ = std::string("send_email_via_vmime_smtp: std exception: ") + e.what();
        LOG_INFO("Outlook send_email_via_vmime_smtp: %s\n", last_error_.c_str());
        return false;
    }
}

std::string EmailOptOutlookImpl::fetch_email_headers(const std::string& folder, const std::string& start_uid) {
    // Ensure we are connected using connect_()
    if (!store_ || !store_->isConnected()) {
        LOG_INFO("Outlook fetch_email_headers - not connected, calling connect_()");
        if (!connect_()) {
            return R"({"status":"failed","error":"connect_failed"})";
        }
    }

    try {
        LOG_INFO("Outlook fetch_email_headers - using vmime folder API with existing connection");

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
        if (folder != "INBOX") {
            vmime::net::folder::path path;
            path.appendComponent(vmime::net::folder::path::component(folder));
            f = store_->getFolder(path);
        }
        auto imapFolder = vmime::dynamic_pointer_cast<vmime::net::imap::IMAPFolder>(f);

        // SELECT via sendCommand on existing connection
        vmime::shared_ptr<vmime::net::imap::IMAPCommand> selectCmd =
            vmime::net::imap::IMAPCommand::createCommand("SELECT " + folder);
        conn->sendCommand(selectCmd);
        vmime::shared_ptr<vmime::net::imap::IMAPParser::response> selectResp(conn->readResponse());

        bool selectOk = false;

        if (selectResp && !selectResp->isBad()) {
            selectOk = true;
        }

        LOG_INFO("Outlook fetch_email_headers - selectOk=%d", selectOk);

        if (!selectOk) {
            return R"({"status":"failed","error":"select_failed"})";
        }

        // Build message set - fetch all messages, will filter by UID later
        // This is more reliable than byUID range which may not work correctly
        vmime::net::messageSet msgs = vmime::net::messageSet::byNumber(1, -1);  // all messages

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
        fetchAttrs.add(vmime::net::fetchAttributes::FLAGS);

        // Ensure UID is included
        vmime::net::fetchAttributes attribsWithUID(fetchAttrs);
        attribsWithUID.add(vmime::net::fetchAttributes::UID);

        // Send FETCH via IMAPUtils::buildFetchCommand on existing connection
        vmime::shared_ptr<vmime::net::imap::IMAPCommand> fetchCmd =
            vmime::net::imap::IMAPUtils::buildFetchCommand(conn, msgs, attribsWithUID);
        LOG_INFO("Outlook fetch_email_headers - sending FETCH via IMAPUtils");
        fetchCmd->send(conn);

        // Read FETCH response
        vmime::shared_ptr<vmime::net::imap::IMAPParser::response> fetchResp(conn->readResponse());
        LOG_INFO("Outlook fetch_email_headers - FETCH response received");

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

        // Parse FETCH response into JSON
        nlohmann::json response;
        response["status"] = "success";
        response["folder"] = folder;
        response["emails"] = nlohmann::json::array();

        // Parse start_uid for filtering
        int start_uid_int = 0;
        try {
            start_uid_int = std::stoi(start_uid);
        } catch (...) {
            start_uid_int = 0;
        }

        for (auto& item : fetchResp->continue_req_or_response_data) {
            if (!item || !item->response_data) continue;
            auto* msgData = item->response_data->message_data.get();
            if (!msgData || msgData->type != vmime::net::imap::IMAPParser::message_data::FETCH) continue;
            if (!msgData->msg_att) continue;

            nlohmann::json emailJson;
            std::string headerData;

            for (auto& attItem : msgData->msg_att->items) {
                if (!attItem) continue;

                if (attItem->type == vmime::net::imap::IMAPParser::msg_att_item::UID) {
                    if (attItem->uniqueid) {
                        emailJson["uuid"] = std::to_string(attItem->uniqueid->value);
                    }
                } else if (attItem->type == vmime::net::imap::IMAPParser::msg_att_item::BODY_SECTION) {
                    if (attItem->nstring && !attItem->nstring->isNIL) {
                        headerData = attItem->nstring->value;
                    }
                } else if (attItem->type == vmime::net::imap::IMAPParser::msg_att_item::BODY_STRUCTURE) {
                    if (attItem->body) {
                        emailJson["bodystructure"] = parseXbody(attItem->body.get());
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
                        emailJson["flags"] = flagsArray;
                    }
                }
            }

            // Filter by UID - only include emails with UUID >= start_uid
            if (emailJson.contains("uuid")) {
                int current_uid = 0;
                try {
                    current_uid = std::stoi(emailJson["uuid"].get<std::string>());
                } catch (...) {
                    current_uid = 0;
                }
                if (current_uid <= start_uid_int) {
                    continue;  // Skip this email, it's already stored (uid <= max stored uid)
                }
            }

            // Parse all headers using vmime to handle folding and case-insensitivity
            auto caseInsensitiveLess = [](const std::string& a, const std::string& b) {
                return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(),
                    [](char c1, char c2) { return ::tolower(c1) < ::tolower(c2); });
            };
            std::map<std::string, std::string, decltype(caseInsensitiveLess)> headerMap(caseInsensitiveLess);
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
                    headerMap[fName] = fValue;
                }
            }

            auto getHeader = [&](const std::string& name) -> std::string {
                auto it = headerMap.find(name);
                if (it != headerMap.end()) return it->second;
                return "";
            };

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

            emailJson["from"] = decodeHeader(getHeader("From"));
            emailJson["subject"] = decodeHeader(getHeader("Subject"));
            emailJson["date"] = getHeader("Date");
            emailJson["reply_to"] = decodeHeader(getHeader("Reply-To"));
            emailJson["in_reply_to"] = decodeHeader(getHeader("In-Reply-To"));
            std::string xMsgId = decodeHeader(getHeader("X-Message-ID"));
            std::string stdMsgId = decodeHeader(getHeader("Message-ID"));
            emailJson["message_id"] = !xMsgId.empty() ? xMsgId : stdMsgId;
            emailJson["x_message_id"] = xMsgId;
            emailJson["x_session_id"] = decodeHeader(getHeader("X-Session-ID"));
            emailJson["to_addr"] = decodeHeader(getHeader("To"));

            response["emails"].push_back(emailJson);
        }

        LOG_INFO("Outlook fetch_email_headers - parsed %zu emails", response["emails"].size());
        return response.dump();
    } catch (const vmime::exception& e) {
        last_error_ = std::string("Failed to fetch email headers: ") + e.what();
        LOG_INFO("Outlook fetch_email_headers - vmime exception: %s", e.what());
        return std::string(R"({"status":"failed","error":"vmime_exception:})") + e.what() + "\"}";
    } catch (const std::exception& e) {
        last_error_ = std::string("Failed to fetch email headers: ") + e.what();
        LOG_INFO("Outlook fetch_email_headers - std exception: %s", e.what());
        return std::string(R"({"status":"failed","error":"std_exception"})");
    }
}

bool EmailOptOutlookImpl::idle_wait(const std::string& folder, int timeout_seconds) {
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
        LOG_INFO("Outlook [IDLE] Selecting folder: %s", folder.c_str());
        vmime::shared_ptr<vmime::net::imap::IMAPCommand> selectCmd =
            vmime::net::imap::IMAPCommand::createCommand("SELECT " + folder);
        conn->sendCommand(selectCmd);
        vmime::shared_ptr<vmime::net::imap::IMAPParser::response> selectResp(conn->readResponse());

        bool selectOk = false;
        if (selectResp && !selectResp->isBad()) {
            selectOk = true;
        }

        LOG_INFO("Outlook [IDLE] SELECT result: %s", selectOk ? "OK" : "FAILED");

        if (!selectOk) {
            last_error_ = "IDLE: SELECT failed for folder " + folder;
            return false;
        }

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
        LOG_INFO("Outlook [IDLE] IDLE command sent, waiting for continuation...");

        // Read the continuation response ("+ idling" or "+") directly from socket
        std::string rawBuffer;
        bool gotContinuation = false;

        while (!gotContinuation) {
            try {
                if (timeoutHandler) timeoutHandler->resetTimeOut();
                sok->waitForRead(5000);
                if (timeoutHandler) timeoutHandler->resetTimeOut();
                std::string chunk;
                sok->receive(chunk);
                if (chunk.empty()) continue;

                rawBuffer += chunk;

                size_t pos;
                while ((pos = rawBuffer.find('\n')) != std::string::npos) {
                    std::string line = rawBuffer.substr(0, pos);
                    rawBuffer.erase(0, pos + 1);
                    if (!line.empty() && line.back() == '\r') line.pop_back();

                    if (!line.empty() && line[0] == '+') {
                        gotContinuation = true;
                    }
                }
            } catch (const vmime::exceptions::operation_timed_out&) {
                if (timeoutHandler) timeoutHandler->resetTimeOut();
                continue;
            }
        }

        if (!gotContinuation) {
            last_error_ = "IDLE: no continuation response from server";
            return false;
        }

        LOG_INFO("Outlook [IDLE] Waiting for new emails...");
        bool gotNotification = false;

        // TLS socket's receive() is non-blocking. We must call waitForRead() first
        while (!gotNotification) {
            try {
                if (timeoutHandler) timeoutHandler->resetTimeOut();
                sok->waitForRead(5000);
                if (timeoutHandler) timeoutHandler->resetTimeOut();

                std::string chunk;
                sok->receive(chunk);

                if (chunk.empty()) continue;

                rawBuffer += chunk;

                size_t pos;
                while ((pos = rawBuffer.find('\n')) != std::string::npos) {
                    std::string line = rawBuffer.substr(0, pos);
                    rawBuffer.erase(0, pos + 1);

                    if (!line.empty() && line.back() == '\r') {
                        line.pop_back();
                    }

                    if (line.find("EXISTS") != std::string::npos) {
                        LOG_INFO("Outlook [IDLE] New email detected: %s", line.c_str());
                        gotNotification = true;
                    }
                    if (line.find("EXPUNGE") != std::string::npos) {
                        gotNotification = true;
                    }
                }
            } catch (const vmime::exceptions::operation_timed_out&) {
                if (timeoutHandler) timeoutHandler->resetTimeOut();
                continue;
            }
        }

        // Send DONE to exit IDLE mode
        LOG_INFO("Outlook [IDLE] Exiting IDLE mode...");
        std::string doneCmd = "DONE\r\n";
        conn->sendRaw(vmime::utility::stringUtils::bytesFromString(doneCmd), doneCmd.size());

        // Read the tagged response for DONE
        conn->readResponse();

        return gotNotification;
    } catch (const vmime::exceptions::operation_timed_out& e) {
        last_error_ = std::string("IDLE wait failed: ") + e.what();
        return false;
    } catch (const vmime::exception& e) {
        last_error_ = std::string("IDLE wait failed: ") + e.what();
        LOG_INFO("Outlook [IDLE] vmime exception: %s", e.what());
        return false;
    } catch (const std::exception& e) {
        last_error_ = std::string("IDLE wait failed: ") + e.what();
        LOG_INFO("Outlook [IDLE] std exception: %s", e.what());
        return false;
    }
}

std::string EmailOptOutlookImpl::find_sent_folder() {
    if (!store_ || !store_->isConnected()) {
        if (!connect_()) {
            LOG_INFO("find_sent_folder: connect failed\n");
            return "Sent";
        }
    }

    try {
        auto root = store_->getRootFolder();
        auto folders = root->getFolders(true);

        LOG_INFO("find_sent_folder: discovered %zu folders\n", folders.size());

        for (const auto& f : folders) {
            std::string name = f->getName().getBuffer();
            auto attrs = f->getAttributes();
            int specialUse = attrs.getSpecialUse();

            LOG_INFO("  folder: '%s', specialUse=%d\n", name.c_str(), specialUse);

            if (specialUse == vmime::net::folderAttributes::SPECIALUSE_SENT) {
                LOG_INFO("find_sent_folder: found Sent folder via SPECIAL-USE: '%s'\n", name.c_str());
                return name;
            }
        }

        // Fallback: match by name (case-insensitive)
        for (const auto& f : folders) {
            std::string name = f->getName().getBuffer();
            std::string lower = name;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            if (lower == "sent" || lower == "sent items" || lower == "sent mail") {
                LOG_INFO("find_sent_folder: found Sent folder by name: '%s'\n", name.c_str());
                return name;
            }
        }

        LOG_INFO("find_sent_folder: no Sent folder found, defaulting to 'Sent'\n");
        return "Sent";
    } catch (const vmime::exception& e) {
        LOG_INFO("find_sent_folder: vmime exception: %s\n", e.what());
        return "Sent";
    } catch (const std::exception& e) {
        LOG_INFO("find_sent_folder: std exception: %s\n", e.what());
        return "Sent";
    }
}

bool EmailOptOutlookImpl::launch_browser(const std::string& url) {
    LOG_INFO("=== launch_browser called ===\n");
    LOG_INFO("URL: %s\n", url.c_str());
    
    // Use system command to open browser directly
    std::string command = "open \"" + url + "\"";
    LOG_INFO("Executing command: %s\n", command.c_str());
    int result = system(command.c_str());
    LOG_INFO("Command result: %d\n", result);
    
    LOG_INFO("=== launch_browser completed ===\n");
    return result == 0;
}

std::string EmailOptOutlookImpl::get_authorization_url(const std::string& redirect_uri,
                                                      const std::string& state,
                                                      const std::string& code_challenge) const {
    std::string url = "https://login.microsoftonline.com/" + std::string(DEFAULT_TENANT_ID) +
                     "/oauth2/v2.0/authorize?" +
                     "client_id=" + client_id_ +
                     "&response_type=code" +
                     "&redirect_uri=" + redirect_uri +
                     "&scope=" + DEFAULT_SCOPE +
                     "&state=" + state +
                     "&code_challenge=" + code_challenge +
                     "&code_challenge_method=S256";
    return url;
}

bool EmailOptOutlookImpl::exchange_code_for_token(const std::string& code,
                                                  const std::string& redirect_uri,
                                                  const std::string& code_verifier) {
    LOG_INFO("Outlook authority: Starting token exchange\n");

    try {
        std::string token_url = "https://login.microsoftonline.com/" + std::string(DEFAULT_TENANT_ID) + "/oauth2/v2.0/token";
        
        std::string post_data = "client_id=" + url_encode(client_id_) +
                               "&code=" + url_encode(code) +
                               "&redirect_uri=" + url_encode(redirect_uri) +
                               "&grant_type=authorization_code" +
                               "&code_verifier=" + url_encode(code_verifier) +
                               "&scope=" + url_encode(OUTLOOK_SCOPE);
        LOG_INFO("Outlook authority: Post data length: %zu\n", post_data.length());

        // Use system curl command to perform HTTPS POST
        std::string command = "curl -s -X POST \"" + token_url + "\" " +
                             "-H \"Content-Type: application/x-www-form-urlencoded\" " +
                             "-d \"" + post_data + "\"";

        LOG_INFO("Outlook authority: Executing curl command...\n");
        FILE* pipe = popen(command.c_str(), "r");
        if (!pipe) {
            last_error_ = "Failed to execute curl command";
            LOG_INFO("Outlook authority: Failed to execute curl command\n");
            return false;
        }

        char buffer[4096];
        std::string response;
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            response += buffer;
        }
        pclose(pipe);

        LOG_INFO("Outlook authority: Token response body: %s\n", response.c_str());

        if (response.empty()) {
            last_error_ = "Empty response from curl";
            LOG_INFO("Outlook authority: Empty response from curl\n");
            return false;
        }

        // Parse JSON response
        std::string access_token = parse_json_field(response, "access_token");
        std::string refresh_token = parse_json_field(response, "refresh_token");
        std::string id_token = parse_json_field(response, "id_token");

        if (access_token.empty()) {
            last_error_ = "Failed to parse access token from response";
            LOG_INFO("Outlook authority: Failed to parse access token\n");
            return false;
        }

        access_token_ = access_token;
        refresh_token_ = refresh_token;
        is_valid_ = true;

        // Parse email from id_token (JWT)
        if (!id_token.empty()) {
            LOG_INFO("Outlook authority: Parsing email from id_token\n");
            LOG_INFO("Outlook authority: id_token length: %zu\n", id_token.length());
            LOG_INFO("Outlook authority: id_token (first 100 chars): %s\n", id_token.substr(0, 100).c_str());
            
            // Simple JWT parsing - get payload part (second part)
            size_t dot_pos = id_token.find('.');
            if (dot_pos != std::string::npos) {
                LOG_INFO("Outlook authority: Found first dot at position: %zu\n", dot_pos);
                size_t second_dot = id_token.find('.', dot_pos + 1);
                if (second_dot != std::string::npos) {
                    LOG_INFO("Outlook authority: Found second dot at position: %zu\n", second_dot);
                    std::string payload = id_token.substr(dot_pos + 1, second_dot - dot_pos - 1);
                    LOG_INFO("Outlook authority: Payload length: %zu\n", payload.length());
                    LOG_INFO("Outlook authority: Payload (first 100 chars): %s\n", payload.substr(0, 100).c_str());
                    
                    // Simple base64 decode (replace URL-safe chars)
                    for (auto& c : payload) {
                        if (c == '-') c = '+';
                        if (c == '_') c = '/';
                    }
                    // Add padding if needed
                    while (payload.length() % 4) payload += '=';
                    
                    LOG_INFO("Outlook authority: Decoding base64...\n");
                    // Decode base64 using OpenSSL
                    BIO* bio = BIO_new_mem_buf(payload.c_str(), payload.length());
                    BIO* b64 = BIO_new(BIO_f_base64());
                    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
                    bio = BIO_push(b64, bio);
                    
                    char decoded[1024];
                    int len = BIO_read(bio, decoded, sizeof(decoded));
                    BIO_free_all(bio);
                    
                    LOG_INFO("Outlook authority: Decoded length: %d\n", len);
                    if (len > 0) {
                        decoded[len] = '\0';
                        std::string payload_str(decoded, len);
                        LOG_INFO("Outlook authority: Decoded payload: %s\n", payload_str.c_str());
                        // Extract email from JSON payload
                        size_t email_pos = payload_str.find("\"email\":");
                        LOG_INFO("Outlook authority: Email field position: %zu\n", email_pos);
                        if (email_pos != std::string::npos) {
                            size_t email_start = payload_str.find("\"", email_pos + 8);
                            if (email_start != std::string::npos) {
                                size_t email_end = payload_str.find("\"", email_start + 1);
                                if (email_end != std::string::npos) {
                                    email_ = payload_str.substr(email_start + 1, email_end - email_start - 1);
                                    LOG_INFO("Outlook authority: Extracted email: %s\n", email_.c_str());
                                } else {
                                    LOG_INFO("Outlook authority: Failed to find email end quote\n");
                                }
                            } else {
                                LOG_INFO("Outlook authority: Failed to find email start quote\n");
                            }
                        } else {
                            LOG_INFO("Outlook authority: Failed to find email field in payload\n");
                        }
                    } else {
                        LOG_INFO("Outlook authority: Base64 decode failed, len <= 0\n");
                    }
                } else {
                    LOG_INFO("Outlook authority: Failed to find second dot in id_token\n");
                }
            } else {
                LOG_INFO("Outlook authority: Failed to find first dot in id_token\n");
            }
        } else {
            LOG_INFO("Outlook authority: id_token is empty\n");
        }

        LOG_INFO("Outlook authority: Token exchange successful\n");
        return true;
    } catch (const std::exception& e) {
        last_error_ = std::string("Token exchange exception: ") + e.what();
        LOG_INFO("Outlook authority: Token exchange exception: %s\n", e.what());
        return false;
    }
}

std::string EmailOptOutlookImpl::parse_json_field(const std::string& json, const std::string& field) {
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
