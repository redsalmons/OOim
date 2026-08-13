#include "email_opt_gmail_impl.h"
#include "email_handler.h"
#include <cpphttp.hpp>
#include <random>
#include <openssl/sha.h>
#include <openssl/evp.h>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <vmime/vmime.hpp>
#include <vmime/platforms/posix/posixHandler.hpp>

#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

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

    // Create cpp-http async server
    cpphttp::HttpServer server(port);

    std::string auth_code;
    std::atomic<bool> received(false);
    std::mutex callback_mutex;

    // Add route for OAuth callback
    server.Get("/", [&auth_code, &received, &callback_mutex](const cpphttp::HttpRequest& req) {
        // Log all query parameters
        std::cout << "OAuth callback received with query parameters:" << std::endl;
        for (const auto& param : req.query_params) {
            std::cout << "  " << param.first << " = " << param.second << std::endl;
        }

        // Extract authorization code from query parameters
        auto code_it = req.query_params.find("code");
        if (code_it != req.query_params.end()) {
            std::string code = code_it->second;
            std::cout << "OAuth authorization code received: " << code << std::endl;
            std::lock_guard<std::mutex> lock(callback_mutex);
            auth_code = code;
            received = true;

            return cpphttp::HttpResponse::Html(
                "<html><body><h1>Authentication Successful!</h1><p>You can close this window now.</p></body></html>");
        }

        auto error_it = req.query_params.find("error");
        if (error_it != req.query_params.end()) {
            std::cout << "OAuth error received: " << error_it->second << std::endl;
            received = true;
            return cpphttp::HttpResponse::Html(
                "<html><body><h1>Authentication Failed.</h1><p>Error: " + error_it->second + "</p></body></html>");
        }

        return cpphttp::HttpResponse::Html("<html><body><h1>Waiting for authentication...</h1></body></html>");
    });

    // Start server (runs in background thread)
    server.Start();

    // Launch browser
    if (!launch_browser(auth_url)) {
        last_error_ = "Failed to launch browser";
        server.Stop();
        return false;
    }

    // Wait for authorization code (with timeout)
    int elapsed = 0;
    while (!received && elapsed < timeout_seconds) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        elapsed++;
    }

    // Stop server
    server.Stop();

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

    // Use cpp-http async client to refresh token
    cpphttp::HttpClient client("oauth2.googleapis.com", 443);

    std::string url = "https://oauth2.googleapis.com/token";

    std::string post_data = "client_id=" + client_id_ +
                           "&client_secret=" + client_secret_ +
                           "&refresh_token=" + refresh_token_ +
                           "&grant_type=refresh_token";

    auto future = client.PostAsync(url, post_data);
    auto response = future.get();

    if (response.status_code != 200) {
        last_error_ = "Token refresh failed with status: " + std::to_string(response.status_code);
        return false;
    }

    // Parse JSON response to extract access token
    std::string access_token = parse_json_field(response.body, "access_token");
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
    last_error_ = "IMAP operations not implemented (IMAP layer removed)";
    return false;
}

std::vector<std::string> EmailOptGmailImpl::fetch_emails_since_uid(const std::string& folder, const std::string& start_uid) {
    last_error_ = "IMAP operations not implemented (IMAP layer removed)";
    return {};
}

std::string EmailOptGmailImpl::get_email(const std::string& folder, const std::string& uid) {
    last_error_ = "IMAP operations not implemented (IMAP layer removed)";
    return "";
}

bool EmailOptGmailImpl::send_email(const std::string& folder, const std::string& content) {
    last_error_ = "IMAP operations not implemented (IMAP layer removed)";
    return false;
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
    // Use cpp-http async client to exchange code for token
    cpphttp::HttpClient client("oauth2.googleapis.com", 443);

    std::string url = "https://oauth2.googleapis.com/token";

    std::string post_data = "client_id=" + client_id_ +
                           "&client_secret=" + client_secret_ +
                           "&code=" + code +
                           "&grant_type=authorization_code" +
                           "&code_verifier=" + code_verifier;

    auto future = client.PostAsync(url, post_data);
    auto response = future.get();

    if (response.status_code != 200) {
        last_error_ = "Token exchange failed with status: " + std::to_string(response.status_code);
        return false;
    }

    // Parse JSON response
    std::string access_token = parse_json_field(response.body, "access_token");
    std::string refresh_token = parse_json_field(response.body, "refresh_token");

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
