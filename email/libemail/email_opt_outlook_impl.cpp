#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "email_opt_outlook_impl.h"
#include "email_core.h"
#include "email_core_common.h"
#include "x_mailer.h"
#include "db_connection.h"
#include "session_repo.h"
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
#include <vmime/net/tls/TLSProperties.hpp>
#include <vmime/net/folder.hpp>
#include <vmime/net/message.hpp>
#include <vmime/net/fetchAttributes.hpp>
#include <vmime/net/messageSet.hpp>
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <openssl/sha.h>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cctype>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <iconv.h>
#include <fcntl.h>
#include <algorithm>
#include <cctype>
#include <curl/curl.h>
#include <vmime/header.hpp>
#include <vmime/message.hpp>
#include <vmime/utility/inputStreamStringAdapter.hpp>
#include <vmime/utility/outputStreamStringAdapter.hpp>
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

// Custom XOAuth2 SASL authenticator that directly provides username and access token,
// bypassing the property-lookup mechanism in defaultAuthenticator which can fail to
// find the "auth.accesstoken" property on the service/store.
class DirectXOAuth2Authenticator : public vmime::security::sasl::XOAuth2SASLAuthenticator {
public:
    DirectXOAuth2Authenticator(
        const vmime::string& username,
        const vmime::string& accessToken,
        Mode mode = MODE_EXCLUSIVE
    ) : vmime::security::sasl::XOAuth2SASLAuthenticator(mode),
        m_username(username),
        m_accessToken(accessToken) {}

    const vmime::string getUsername() const override {
        return m_username;
    }

    const vmime::string getAccessToken() const override {
        return m_accessToken;
    }

    const vmime::string getPassword() const override {
        return m_accessToken;
    }

    const vmime::string getHostname() const override {
        return vmime::string();
    }

    const vmime::string getAnonymousToken() const override {
        return m_username;
    }

    const vmime::string getServiceName() const override {
        return vmime::string();
    }

private:
    vmime::string m_username;
    vmime::string m_accessToken;
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
      account_type_("personal"),
      access_token_expiry_(std::chrono::system_clock::from_time_t(0)),
      graph_token_expiry_(std::chrono::system_clock::from_time_t(0)) {
}

EmailOptOutlookImpl::~EmailOptOutlookImpl() {
}

bool EmailOptOutlookImpl::connect() {
    return connect_();
}

bool EmailOptOutlookImpl::connect_() {
    // 1. Ensure a valid access token before touching the network.
    if (!ensure_authenticated()) {
        LOG_INFO("Outlook connect_ - ensure_authenticated failed: %s\n", last_error_.c_str());
        return false;
    }

    // Lock to prevent concurrent connect_() calls from multiple threads
    std::lock_guard<std::mutex> lock(connect_mutex_);

    // Check if already connected
    if (store_ && store_->isConnected()) {
        LOG_INFO("Outlook connect_ - already connected, returning true\n");
        return true;
    }

    LOG_INFO("Outlook connect_ - email_: %s\n", email_.c_str());
    LOG_INFO("Outlook connect_ - access_token_ length: %zu\n", access_token_.length());

    if (email_.empty()) {
        LOG_INFO("Outlook connect_ - email is empty, connection failed\n");
        return false;
    }

    // Clean up any previous connection objects before creating new ones
    store_.reset();
    session_.reset();

    LOG_INFO("Outlook connect_ - establishing TCP connection to %s:%d...\n", imap_server_.c_str(), imap_port_);

    try {
        // Initialize vmime platform handler (only once)
        init_vmime_platform();

        // Create session using static method and save to member variable
        session_ = vmime::net::session::create();

        // Set connection timeout
        session_->getProperties()["connection.timeout"] = "30";
        session_->getProperties()["imap.timeout"] = "30";

        // GnuTLS default cipher suite includes %SSL3_RECORD_VERSION, which
        // causes Outlook IMAPS connections to reset. Set a plain "NORMAL"
        // priority string to let GnuTLS and the server negotiate naturally.
        vmime::shared_ptr<vmime::net::tls::TLSProperties> tlsProps =
            vmime::make_shared<vmime::net::tls::TLSProperties>();
        tlsProps->setCipherSuite("NORMAL");
        session_->setTLSProperties(tlsProps);

        // Set authentication properties on the SESSION.
        // vmime's defaultAuthenticator reads properties with the service prefix:
        //   "store.imaps.auth.username", "store.imaps.auth.password", "store.imaps.auth.accesstoken"
        // (prefix is "store.imaps." for imaps://, "store.imap." for imap://).
        const std::string prefix = "store.imaps.";
        session_->getProperties()[prefix + "auth.username"] = email_;
        session_->getProperties()[prefix + "auth.password"] = access_token_;
        session_->getProperties()[prefix + "auth.accesstoken"] = access_token_;

        LOG_INFO("Outlook connect_ - session properties set: username=%s, accesstoken_len=%zu\n",
                  email_.c_str(), access_token_.length());

        // Create IMAP store — use "imaps://" for implicit TLS on port 993.
        vmime::utility::url store_url("imaps", imap_server_, imap_port_);
        store_ = session_->getStore(store_url);

        LOG_INFO("Outlook connect_ - store created (imaps://, default authenticator)\n");

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

bool EmailOptOutlookImpl::needs_token_refresh() const {
    if (access_token_.empty()) {
        return true;
    }
    // Treat token as stale 60 seconds before actual expiry to avoid race conditions
    auto now = std::chrono::system_clock::now();
    return now >= access_token_expiry_ - std::chrono::seconds(60);
}

void EmailOptOutlookImpl::set_token_expiry_from_response(const std::string& response) {
    std::string expires_str = parse_json_field(response, "expires_in");
    int expires_in = 3600; // Default to 1 hour if not provided
    if (!expires_str.empty()) {
        try {
            expires_in = std::stoi(expires_str);
        } catch (const std::exception& e) {
            LOG_INFO("Outlook set_token_expiry: failed to parse expires_in '%s', using default 3600\n", expires_str.c_str());
        }
    }
    access_token_expiry_ = std::chrono::system_clock::now() + std::chrono::seconds(expires_in);
    LOG_INFO("Outlook set_token_expiry: token expires in %d seconds\n", expires_in);
}

bool EmailOptOutlookImpl::ensure_authenticated() {
    std::lock_guard<std::mutex> lock(auth_mutex_);

    LOG_INFO("Outlook ensure_authenticated: checking access token...\n");

    if (!needs_token_refresh()) {
        LOG_INFO("Outlook ensure_authenticated: existing access token still valid\n");
        is_valid_ = true;
        return true;
    }

    LOG_INFO("Outlook ensure_authenticated: access token missing or expired\n");

    // Try refresh token first. Authority (interactive browser) is intentionally
    // NOT called here; it must be triggered explicitly by the UI via authority().
    if (!refresh_token_.empty()) {
        LOG_INFO("Outlook ensure_authenticated: attempting refresh_token()\n");
        if (refresh_token()) {
            LOG_INFO("Outlook ensure_authenticated: refresh_token() succeeded\n");
            return true;
        }
        LOG_INFO("Outlook ensure_authenticated: refresh_token() failed: %s\n", last_error_.c_str());
    } else {
        LOG_INFO("Outlook ensure_authenticated: no refresh token available\n");
    }

    LOG_INFO("Outlook ensure_authenticated: cannot obtain token without interactive authorization\n");
    last_error_ = "Access token expired and refresh failed; authorization required";
    return false;
}

bool EmailOptOutlookImpl::authority(int timeout_seconds) {
    std::lock_guard<std::mutex> lock(auth_mutex_);

    LOG_INFO("Outlook authority: Starting full OAuth flow with timeout %ds\n", timeout_seconds);

    // Avoid popping a browser window if we already have a usable token.
    // 1. Valid access token? Return immediately.
    // 2. Valid refresh token? Try a silent refresh first.
    // 3. Only if both fail, open the interactive OAuth consent page.
    if (!needs_token_refresh()) {
        LOG_INFO("Outlook authority: access token still valid, no OAuth needed\n");
        return true;
    }

    if (!refresh_token_.empty()) {
        LOG_INFO("Outlook authority: attempting silent refresh before OAuth\n");
        if (refresh_token()) {
            LOG_INFO("Outlook authority: silent refresh succeeded, no OAuth needed\n");
            return true;
        }
        LOG_INFO("Outlook authority: silent refresh failed: %s\n", last_error_.c_str());
    }

    LOG_INFO("Outlook authority: no valid tokens, opening browser for OAuth\n");

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
    if (!access_token_.empty() && !needs_token_refresh()) {
        LOG_INFO("Outlook refresh_token: access token still valid, skipping network refresh\n");
        return true;
    }
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
        set_token_expiry_from_response(response);
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
            LOG_INFO("Outlook refresh_token: new refresh token stored (len=%zu)\n", refresh_token_.length());
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
    if (!graph_access_token_.empty() &&
        std::chrono::system_clock::now() < graph_token_expiry_ - std::chrono::seconds(60)) {
        LOG_INFO("Outlook refresh_graph_token: graph access token still valid, skipping network refresh\n");
        return true;
    }
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
        set_token_expiry_from_response(response);
        graph_token_expiry_ = access_token_expiry_;  // temporarily share expiry
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

std::string EmailOptOutlookImpl::graph_state_path() const {
    if (data_dir_.empty()) return "";
    if (email_.empty()) return data_dir_ + "/outlook_graph_state.json";
    return data_dir_ + "/" + email_ + "_graph_state.json";
}

void EmailOptOutlookImpl::load_graph_state() {
    if (email_.empty()) return;

    sqlite3* db = email_core_get_db();
    if (!db) {
        LOG_INFO("Outlook load_graph_state: db not open\n");
        return;
    }

    std::lock_guard<std::mutex> lock(email_core_get_db_mutex());
    const char* sql = "SELECT identify FROM code WHERE account = ? AND session_uuid = 'outlook_graph_state' ORDER BY id DESC LIMIT 1;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, email_.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* s = (const char*)sqlite3_column_text(stmt, 0);
            if (s) {
                last_graph_delta_link_ = s;
                LOG_INFO("Outlook load_graph_state: loaded delta link for %s, len=%zu\n", email_.c_str(), last_graph_delta_link_.length());
            }
        }
        sqlite3_finalize(stmt);
    }
}

void EmailOptOutlookImpl::save_graph_state() {
    if (email_.empty()) return;

    sqlite3* db = email_core_get_db();
    if (!db) {
        LOG_INFO("Outlook save_graph_state: db not open\n");
        return;
    }

    std::lock_guard<std::mutex> lock(email_core_get_db_mutex());
    const char* check = "SELECT id FROM code WHERE account = ? AND session_uuid = 'outlook_graph_state' LIMIT 1;";
    sqlite3_stmt* check_stmt = nullptr;
    bool exists = false;
    if (sqlite3_prepare_v2(db, check, -1, &check_stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(check_stmt, 1, email_.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(check_stmt) == SQLITE_ROW) {
            exists = true;
        }
        sqlite3_finalize(check_stmt);
    }

    if (exists) {
        const char* sql = "UPDATE code SET identify = ? WHERE account = ? AND session_uuid = 'outlook_graph_state';";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, last_graph_delta_link_.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, email_.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    } else {
        const char* sql = "INSERT INTO code (account, identify, session_uuid) VALUES (?, ?, 'outlook_graph_state');";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, email_.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, last_graph_delta_link_.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }
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

std::string EmailOptOutlookImpl::json_escape_string(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 8);
    for (char c : value) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
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
    // Use Microsoft Graph API to fetch the full MIME message by id
    if (!ensure_graph_token()) {
        LOG_INFO("Outlook get_email: graph token not available\n");
        return "";
    }
    std::string mime = graph_get_message_mime(uid);
    LOG_INFO("Outlook get_email: mime length=%zu for id=%s\n", mime.length(), uid.c_str());
    return mime;
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
    std::string x_message_id_str, x_session_chart_str;
    int encrypt_method_val = 0;
    std::string members_str;
    try {
        auto j = nlohmann::json::parse(content);
        recipient_str = j.value("recipient", "");
        subject_str = j.value("subject", "");
        body_str = j.value("body", "");
        in_reply_to_str = j.value("in_reply_to", "");
        message_id_str = j.value("message_id", "");
        session_id_str = j.value("session_id", "");
        x_message_id_str = j.value("x_message_id", "");
        x_session_chart_str = j.value("x_session_chart", "");
        encrypt_method_val = j.value("encrypt_method", 0);
        members_str = j.value("members", "");
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

    // Resolve session ID early for potential body encryption
    // For NEW_SESSION: defer session creation to after email_insert_sent_email (need rowid)
    std::string sid = session_id_str;

    // For non-new types, find session via in_reply_to
    if (sid.empty() && !in_reply_to_str.empty()) {
        static SessionRepo s_sessionRepo;
        sid = s_sessionRepo.querySessionByInReplyTo(in_reply_to_str, email_);
        LOG_INFO("Outlook send_email_via_graph_api: x_mailer=%s, found session_id=%s via in_reply_to=%s\n",
                 x_session_chart_str.c_str(), sid.c_str(), in_reply_to_str.c_str());
    }

    LOG_INFO("Outlook send_email_via_graph_api: using session_id=%s\n", sid.c_str());

    // Legacy encryption disabled — all encryption handled by Signal protocol
    bool needsEncryption = false;

    // Choose sending method based on account type
    LOG_INFO("Outlook send_email: session_id=%s\n", session_id_str.c_str());

    if (account_type_ == "personal") {
        return send_email_via_graph_api(recipient_str, subject_str, body_str, in_reply_to_str, message_id_str, session_id_str, x_message_id_str, x_session_chart_str, encrypt_method_val, members_str);
    } else {
        return send_email_via_vmime_smtp(recipient_str, subject_str, body_str, in_reply_to_str, message_id_str, session_id_str, x_message_id_str, x_session_chart_str, encrypt_method_val, members_str);
    }
}

bool EmailOptOutlookImpl::send_email_via_graph_api(const std::string& recipient, const std::string& subject, 
                                                     const std::string& body, const std::string& in_reply_to, 
                                                     const std::string& message_id, const std::string& session_id,
                                                     const std::string& x_message_id, const std::string& x_session_chart,
                                                     int encrypt_method, const std::string& members) {
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
    if (!x_session_chart.empty()) {
        email_msg += "X-Mailer: " + x_session_chart + "\r\n";
    }
    email_msg += "MIME-Version: 1.0\r\n";
    email_msg += "Content-Type: text/html; charset=utf-8\r\n";
    email_msg += "Content-Transfer-Encoding: 8bit\r\n";
    email_msg += "\r\n";
    email_msg += body;
    
    // Base64 encode the email message
    std::string email_b64 = base64_encode_bytes(std::vector<uint8_t>(email_msg.begin(), email_msg.end()));
    email_b64 = wrap_base64_lines(email_b64);
    
    // Build Graph API request body using nlohmann::json (avoids raw-string-literal pitfalls)
    nlohmann::json jRecipients = nlohmann::json::array();
    {
        size_t start = 0, end;
        while ((end = recipient.find(',', start)) != std::string::npos) {
            std::string addr = recipient.substr(start, end - start);
            size_t b = addr.find_first_not_of(" \t");
            size_t e2 = addr.find_last_not_of(" \t");
            if (b != std::string::npos) {
                jRecipients.push_back({{"emailAddress", {{"address", addr.substr(b, e2 - b + 1)}}}});
            }
            start = end + 1;
        }
        std::string addr = recipient.substr(start);
        size_t b = addr.find_first_not_of(" \t");
        size_t e2 = addr.find_last_not_of(" \t");
        if (b != std::string::npos) {
            jRecipients.push_back({{"emailAddress", {{"address", addr.substr(b, e2 - b + 1)}}}});
        }
    }

    nlohmann::json jHeaders = nlohmann::json::array();
    if (!x_session_chart.empty()) {
        jHeaders.push_back({{"name", "X-Mailer"}, {"value", x_session_chart}});
    }
    // Note: In-Reply-To and References are NOT set here. Graph API's
    // internetMessageHeaders only accepts custom headers starting with 'x-'/'X-'.
    // Per project rules, message association is via body's x-reply-to, not headers.

    nlohmann::json jBody;
    jBody["message"] = {
        {"subject", subject},
        {"body", {{"contentType", "HTML"}, {"content", body}}},
        {"toRecipients", jRecipients},
        {"internetMessageHeaders", jHeaders}
    };

    std::string json_body = jBody.dump();
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

    // Graph API returns 202 Accepted with empty body on success.
    // On error it returns a JSON body with an "error" field — check for it.
    if (!response.empty()) {
        try {
            auto respJson = nlohmann::json::parse(response);
            if (respJson.contains("error")) {
                last_error_ = "send_email_via_graph_api: Graph API error: " + response;
                LOG_INFO("Outlook send_email_via_graph_api: %s\n", last_error_.c_str());
                return false;
            }
        } catch (...) {
            // Not JSON — could still be an error page; log it
            LOG_INFO("Outlook send_email_via_graph_api: unexpected response: %s\n", response.c_str());
        }
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
        sizeof(json_buffer),
        x_session_chart.c_str()
    );

    LOG_INFO("Outlook send_email_via_graph_api: insert_result=%d, json_buffer='%s'\n", insert_result, json_buffer);

    if (insert_result == 0) {
        LOG_INFO("Outlook send_email_via_graph_api: inserted sent email to database\n");

        // Parse response to get email id
        try {
            nlohmann::json response = nlohmann::json::parse(json_buffer);
            if (response.contains("uuid")) {
                std::string email_id = response["uuid"].get<std::string>();
                
                // For exchange or reply, find session via in_reply_to
                std::string sid = session_id;
                if (sid.empty() && !in_reply_to.empty()) {
                    static SessionRepo s_sessionRepo;
                    sid = s_sessionRepo.querySessionByInReplyTo(in_reply_to, email_);
                    LOG_INFO("Outlook send_email_via_graph_api: found session_id=%s via in_reply_to=%s\n",
                             sid.c_str(), in_reply_to.c_str());
                }

                // For SESSION_INIT, create session locally
                if (x_session_chart == XMailer::SESSION_INIT && sid.empty()) {
                    int64_t rowid = std::stoll(email_id);
                    char create_json[4096];
                    int create_rc = email_create_session(
                        email_.c_str(), subject.c_str(),
                        members.empty() ? email_.c_str() : members.c_str(),
                        msg_id.c_str(), encrypt_method, rowid,
                        create_json, sizeof(create_json));
                    if (create_rc == 0) {
                        try {
                            auto resp = nlohmann::json::parse(create_json);
                            if (resp.value("status", "") == "success") {
                                sid = resp.value("session_id", "");
                            }
                        } catch (...) {}
                    }
                    LOG_INFO("Outlook send_email_via_graph_api: SESSION_INIT created session_id=%s (rowid=%s)\n", sid.c_str(), email_id.c_str());
                }

                LOG_INFO("Outlook send_email_via_graph_api: using session_id=%s\n", sid.c_str());
                
                // Add email to session
                char session_buffer[8192];
                int encMethod = (x_session_chart == XMailer::RATCHET_MSG || x_session_chart == XMailer::ATTACH_META || x_session_chart == XMailer::ATTACH_CHUNK) ? 1 : 0;
                int session_result = email_add_email_to_session(
                    sid.c_str(),
                    email_id.c_str(),
                    email_.c_str(),
                    encMethod,
                    session_buffer,
                    sizeof(session_buffer)
                );
                
                if (session_result == 0) {
                    LOG_INFO("Outlook send_email_via_graph_api: added email to session %s\n", sid.c_str());
                    
                    // Notify UI that email was sent
                    if (email_handler_) {
                        nlohmann::json notify_json;
                        notify_json["session_id"] = sid;
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
                                                      const std::string& message_id, const std::string& session_id,
                                                      const std::string& x_message_id, const std::string& x_session_chart,
                                                      int encrypt_method, const std::string& members) {
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
        
        // Add custom header: X-Mailer
        if (!x_session_chart.empty()) {
            vmime::shared_ptr<vmime::headerField> xMailerField =
                vmime::headerFieldFactory::getInstance()->create("X-Mailer", x_session_chart);
            msg->getHeader()->appendField(xMailerField);
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
            sizeof(json_buffer),
            x_session_chart.c_str()
        );

        if (insert_result == 0) {
            LOG_INFO("Outlook send_email_via_vmime_smtp: inserted sent email to database\n");
            
            // Parse response to get email id
            try {
                nlohmann::json response = nlohmann::json::parse(json_buffer);
                if (response.contains("uuid")) {
                    std::string email_id = response["uuid"].get<std::string>();
                    
                    // For SESSION_INIT, create session locally
                    std::string sid = session_id;
                    if (x_session_chart == XMailer::SESSION_INIT && sid.empty()) {
                        int64_t rowid = std::stoll(email_id);
                        char create_json[4096];
                        int create_rc = email_create_session(
                            email_.c_str(), subject.c_str(),
                            members.empty() ? email_.c_str() : members.c_str(),
                            msg_id.c_str(), encrypt_method, rowid,
                            create_json, sizeof(create_json));
                        if (create_rc == 0) {
                            try {
                                auto resp = nlohmann::json::parse(create_json);
                                if (resp.value("status", "") == "success") {
                                    sid = resp.value("session_id", "");
                                }
                            } catch (...) {}
                        }
                        LOG_INFO("Outlook send_email_via_vmime_smtp: SESSION_INIT created session_id=%s (rowid=%s)\n", sid.c_str(), email_id.c_str());
                    }

                    // For non-new types, find session via in_reply_to
                    if (sid.empty() && !in_reply_to.empty()) {
                        static SessionRepo s_sessionRepo2;
                        sid = s_sessionRepo2.querySessionByInReplyTo(in_reply_to, email_);
                        LOG_INFO("Outlook send_email_via_vmime_smtp: found session_id=%s via in_reply_to=%s\n",
                                 sid.c_str(), in_reply_to.c_str());
                    }

                    LOG_INFO("Outlook send_email_via_vmime_smtp: using session_id=%s\n", sid.c_str());
                    
                    // Add email to session
                    char session_buffer[8192];
                    int encMethod = (x_session_chart == XMailer::RATCHET_MSG || x_session_chart == XMailer::ATTACH_META || x_session_chart == XMailer::ATTACH_CHUNK) ? 1 : 0;
                    int session_result = email_add_email_to_session(
                        sid.c_str(),
                        email_id.c_str(),
                        email_.c_str(),
                        encMethod,
                        session_buffer,
                        sizeof(session_buffer)
                    );
                    
                    if (session_result == 0) {
                        LOG_INFO("Outlook send_email_via_vmime_smtp: added email to session %s\n", sid.c_str());
                        
                        // Notify UI that email was sent
                        if (email_handler_) {
                            nlohmann::json notify_json;
                            notify_json["session_id"] = sid;
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
    // Use Microsoft Graph API to fetch pending messages, then parse MIME with vmime
    LOG_INFO("Outlook fetch_email_headers - using Graph API for folder %s", folder.c_str());

    if (!ensure_graph_token()) {
        return R"({"status":"failed","error":"graph_token_failed"})";
    }

    nlohmann::json response;
    response["status"] = "success";
    response["folder"] = folder;
    response["emails"] = nlohmann::json::array();

    std::vector<std::string> ids;
    {
        std::lock_guard<std::mutex> lock(graph_delta_mutex_);
        if (!pending_message_ids_.empty()) {
            ids = std::move(pending_message_ids_);
            pending_message_ids_.clear();
        }
    }
    if (ids.empty()) {
        ids = graph_delta_query(folder);
    }

    for (const auto& id : ids) {
        LOG_INFO("Outlook fetch_email_headers - fetching MIME for id=%s", id.c_str());
        std::string mime = graph_get_message_mime(id);
        if (mime.empty()) {
            LOG_INFO("Outlook fetch_email_headers - empty MIME for id=%s", id.c_str());
            continue;
        }
        LOG_INFO("Outlook fetch_email_headers - MIME length=%zu", mime.length());

        std::string emailJsonStr = parse_mime_to_json(mime, id);
        try {
            nlohmann::json emailJson = nlohmann::json::parse(emailJsonStr);
            response["emails"].push_back(emailJson);
        } catch (const std::exception& e) {
            LOG_INFO("Outlook fetch_email_headers - failed to parse email JSON: %s", e.what());
        }
    }

    LOG_INFO("Outlook fetch_email_headers - parsed %zu emails", response["emails"].size());
    return response.dump();
}


bool EmailOptOutlookImpl::idle_wait(const std::string& folder, int timeout_seconds) {
    // Use Microsoft Graph delta query for Outlook watch
    LOG_INFO("Outlook [WATCH] Using Graph API delta query for folder %s (timeout=%d)\n", folder.c_str(), timeout_seconds);

    if (!ensure_graph_token()) {
        LOG_INFO("Outlook [WATCH] failed to ensure graph token: %s\n", last_error_.c_str());
        return false;
    }

    auto start = std::chrono::steady_clock::now();
    while (true) {
        auto ids = graph_delta_query(folder);
        {
            std::lock_guard<std::mutex> lock(graph_delta_mutex_);
            if (!ids.empty()) {
                pending_message_ids_.insert(pending_message_ids_.end(), ids.begin(), ids.end());
            }
            if (!pending_message_ids_.empty()) {
                LOG_INFO("Outlook [WATCH] found %zu new message(s)\n", pending_message_ids_.size());
                return true;
            }
        }

        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - start).count();
        if (elapsed >= timeout_seconds) {
            LOG_INFO("Outlook [WATCH] timeout, no new messages\n");
            return false;
        }

        std::this_thread::sleep_for(std::chrono::seconds(2));
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
        set_token_expiry_from_response(response);

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

bool EmailOptOutlookImpl::ensure_graph_token() {
    if (graph_access_token_.empty() || graph_token_expiry_ <= std::chrono::system_clock::now()) {
        return refresh_graph_token();
    }
    return true;
}

std::string EmailOptOutlookImpl::graph_request(const std::string& url, const std::string& method, const std::string& body) {
    LOG_INFO("Outlook graph_request: %s %s\n", method.c_str(), url.c_str());
    std::string header = "Authorization: Bearer " + graph_access_token_;
    std::string escaped_url;
    for (char c : url) {
        if (c == '$' || c == '\\' || c == '"' || c == '`') {
            escaped_url += '\\';
        }
        escaped_url += c;
    }
    std::string cmd = "curl -s --connect-timeout 10 --max-time 30 -X " + method + " \"" + escaped_url + "\" " +
                      "-H \"" + header + "\" " +
                      "-H \"Accept: application/json\"";
    if (!body.empty()) {
        cmd += " -H \"Content-Type: application/json\" -d '\"" + body + "\"'";
    }

    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        LOG_INFO("Outlook graph_request: failed to run curl\n");
        return "";
    }
    char buffer[8192];
    std::string response;
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        response += buffer;
    }
    pclose(pipe);
    LOG_INFO("Outlook graph_request: response length=%zu\n", response.length());
    return response;
}

std::vector<std::string> EmailOptOutlookImpl::graph_delta_query(const std::string& folder) {
    std::lock_guard<std::mutex> lock(graph_delta_mutex_);
    std::vector<std::string> new_ids;

    if (last_graph_delta_link_.empty()) {
        // Try to restore persisted cursor; otherwise start an initial full sync
        load_graph_state();
    }

    if (last_graph_delta_link_.empty()) {
        // No persisted cursor: start from the beginning using delta query
        last_graph_delta_link_ = "https://graph.microsoft.com/v1.0/me/mailFolders/inbox/messages/delta?$top=200";
    }

    // Loop through all pages of the delta sync
    while (true) {
        std::string response = graph_request(last_graph_delta_link_);
        if (response.empty()) {
            LOG_INFO("Outlook graph_delta_query: empty response\n");
            break;
        }

        LOG_INFO("Outlook graph_delta_query: response length=%zu\n", response.length());

        nlohmann::json j;
        try {
            j = nlohmann::json::parse(response);
        } catch (const std::exception& e) {
            LOG_INFO("Outlook graph_delta_query: failed to parse response: %s\n", e.what());
            break;
        }

        if (!j.contains("value")) {
            break;
        }

        for (const auto& item : j["value"].items()) {
            const auto& v = item.value();
            if (v.contains("@removed")) {
                continue;
            }
            if (v.contains("id")) {
                std::string id = v["id"].get<std::string>();
                LOG_INFO("Outlook graph_delta_query: id=%s\n", id.c_str());
                new_ids.push_back(id);
            }
        }

        // Update cursor: continue with nextLink, finish when deltaLink is returned
        if (j.contains("@odata.nextLink")) {
            last_graph_delta_link_ = j["@odata.nextLink"].get<std::string>();
            continue;  // fetch next page
        }
        if (j.contains("@odata.deltaLink")) {
            last_graph_delta_link_ = j["@odata.deltaLink"].get<std::string>();
        }
        break;
    }

    save_graph_state();
    return new_ids;
}

static std::string sanitize_utf8(const std::string& input) {
    std::string output;
    output.reserve(input.size());
    size_t i = 0;
    while (i < input.size()) {
        unsigned char c = static_cast<unsigned char>(input[i]);
        if (c < 0x80) {
            output += c;
            ++i;
        } else if (c >= 0xC2 && c <= 0xDF) {
            if (i + 1 < input.size() && (static_cast<unsigned char>(input[i + 1]) & 0xC0) == 0x80) {
                output += c;
                output += input[i + 1];
                i += 2;
            } else {
                output += '?';
                ++i;
            }
        } else if (c >= 0xE0 && c <= 0xEF) {
            if (i + 2 < input.size() &&
                (static_cast<unsigned char>(input[i + 1]) & 0xC0) == 0x80 &&
                (static_cast<unsigned char>(input[i + 2]) & 0xC0) == 0x80) {
                unsigned char c1 = input[i + 1];
                unsigned char c2 = input[i + 2];
                bool ok = true;
                if (c == 0xE0 && c1 < 0xA0) ok = false;
                if (c == 0xED && c1 >= 0xA0) ok = false;
                uint32_t cp = ((c & 0x0F) << 12) | ((c1 & 0x3F) << 6) | (c2 & 0x3F);
                if (cp > 0x10FFFF) ok = false;
                if (ok) {
                    output += c;
                    output += c1;
                    output += c2;
                    i += 3;
                } else {
                    output += '?';
                    ++i;
                }
            } else {
                output += '?';
                ++i;
            }
        } else if (c >= 0xF0 && c <= 0xF4) {
            if (i + 3 < input.size() &&
                (static_cast<unsigned char>(input[i + 1]) & 0xC0) == 0x80 &&
                (static_cast<unsigned char>(input[i + 2]) & 0xC0) == 0x80 &&
                (static_cast<unsigned char>(input[i + 3]) & 0xC0) == 0x80) {
                unsigned char c1 = input[i + 1];
                unsigned char c2 = input[i + 2];
                unsigned char c3 = input[i + 3];
                bool ok = true;
                if (c == 0xF0 && c1 < 0x90) ok = false;
                if (c == 0xF4 && c1 > 0x8F) ok = false;
                uint32_t cp = ((c & 0x07) << 18) | ((c1 & 0x3F) << 12) | ((c2 & 0x3F) << 6) | (c3 & 0x3F);
                if (cp > 0x10FFFF) ok = false;
                if (ok) {
                    output += c;
                    output += c1;
                    output += c2;
                    output += c3;
                    i += 4;
                } else {
                    output += '?';
                    ++i;
                }
            } else {
                output += '?';
                ++i;
            }
        } else {
            output += '?';
            ++i;
        }
    }
    return output;
}

static std::string extract_charset(const std::string& contentType) {
    auto pos = contentType.find("charset");
    if (pos == std::string::npos) return "UTF-8";
    pos = contentType.find("=", pos);
    if (pos == std::string::npos || pos + 1 >= contentType.size()) return "UTF-8";
    ++pos;
    while (pos < contentType.size() && (contentType[pos] == ' ' || contentType[pos] == '\t')) ++pos;
    if (pos >= contentType.size()) return "UTF-8";
    if (contentType[pos] == '"' || contentType[pos] == '\'') {
        char quote = contentType[pos++];
        auto end = contentType.find(quote, pos);
        if (end == std::string::npos) return "UTF-8";
        return contentType.substr(pos, end - pos);
    }
    auto end = contentType.find_first_of(" ;\t\r\n", pos);
    return contentType.substr(pos, end - pos);
}

static std::string iconv_to_utf8(const std::string& raw, const std::string& charset) {
    if (raw.empty()) return "";
    std::vector<std::string> names;
    names.push_back(charset);
    // Common aliases / fallbacks for Chinese and legacy encodings
    names.push_back("GBK");
    names.push_back("gbk");
    names.push_back("GB18030");
    names.push_back("gb18030");
    names.push_back("GB2312");
    names.push_back("gb2312");
    names.push_back("CP936");
    names.push_back("cp936");
    names.push_back("BIG5");
    names.push_back("big5");
    names.push_back("ISO-8859-1");
    names.push_back("iso-8859-1");
    names.push_back("WINDOWS-1252");
    names.push_back("windows-1252");

    for (const auto& name : names) {
        iconv_t cd = iconv_open("UTF-8", name.c_str());
        if (cd == (iconv_t)-1) continue;

        std::string out;
        size_t out_size = raw.size() * 4 + 16;
        out.resize(out_size);
        char* outbuf = &out[0];
        size_t outleft = out_size;

        char* inbuf = const_cast<char*>(raw.data());
        size_t inleft = raw.size();

        size_t r = iconv(cd, &inbuf, &inleft, &outbuf, &outleft);
        iconv_close(cd);

        if (r != (size_t)-1) {
            out.resize(out_size - outleft);
            return out;
        }
    }
    return "";
}

static std::string convert_to_utf8(const std::string& raw, const std::string& charset) {
    if (charset.empty() || charset == "UTF-8" || charset == "utf-8") return sanitize_utf8(raw);
    std::string out = iconv_to_utf8(raw, charset);
    if (!out.empty()) return sanitize_utf8(out);
    try {
        vmime::charset::convert(raw, out, vmime::charset(charset), vmime::charset("UTF-8"));
        return sanitize_utf8(out);
    } catch (const std::exception& e) {
        LOG_INFO("Outlook convert_to_utf8: conversion failed for charset %s: %s\n", charset.c_str(), e.what());
        return sanitize_utf8(raw);
    }
}

static std::string b64_decode_word(const std::string& in) {
    static const std::string b64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    int group[4] = {0, 0, 0, 0};
    int gcount = 0;
    for (char cc : in) {
        unsigned char c = static_cast<unsigned char>(cc);
        if (c == ' ' || c == '\r' || c == '\n' || c == '\t') continue;
        if (c == '=') {
            group[gcount++] = -1;
        } else {
            size_t pos = b64.find(c);
            if (pos == std::string::npos) continue;
            group[gcount++] = static_cast<int>(pos);
        }
        if (gcount == 4) {
            if (group[0] == -1 || group[1] == -1) {
                // invalid, skip
            } else if (group[2] == -1) {
                unsigned char b0 = (group[0] << 2) | (group[1] >> 4);
                out.push_back(static_cast<char>(b0));
            } else if (group[3] == -1) {
                unsigned char b0 = (group[0] << 2) | (group[1] >> 4);
                unsigned char b1 = (group[1] << 4) | (group[2] >> 2);
                out.push_back(static_cast<char>(b0));
                out.push_back(static_cast<char>(b1));
            } else {
                unsigned char b0 = (group[0] << 2) | (group[1] >> 4);
                unsigned char b1 = (group[1] << 4) | (group[2] >> 2);
                unsigned char b2 = (group[2] << 6) | group[3];
                out.push_back(static_cast<char>(b0));
                out.push_back(static_cast<char>(b1));
                out.push_back(static_cast<char>(b2));
            }
            gcount = 0;
        }
    }
    return out;
}

static std::string qp_decode_word(const std::string& in) {
    std::string out;
    for (size_t i = 0; i < in.size(); ++i) {
        char c = in[i];
        if (c == '_') {
            out.push_back(' ');
        } else if (c == '=') {
            if (i + 2 < in.size() && std::isxdigit(static_cast<unsigned char>(in[i + 1])) && std::isxdigit(static_cast<unsigned char>(in[i + 2]))) {
                unsigned int b;
                std::sscanf(in.substr(i + 1, 2).c_str(), "%02x", &b);
                out.push_back(static_cast<char>(b));
                i += 2;
            } else if (i + 1 < in.size() && (in[i + 1] == '\r' || in[i + 1] == '\n' || in[i + 1] == '\0')) {
                // soft line break / null, skip
                ++i;
            } else {
                out.push_back(c);
            }
        } else {
            out.push_back(c);
        }
    }
    return out;
}

static std::string decode_header(const std::string& text) {
    std::string result;
    size_t i = 0;
    while (i < text.size()) {
        if (i + 1 < text.size() && text[i] == '=' && text[i + 1] == '?') {
            size_t q1 = text.find('?', i + 2);
            if (q1 == std::string::npos) { result += text[i++]; continue; }
            std::string charset = text.substr(i + 2, q1 - (i + 2));
            size_t q2 = text.find('?', q1 + 1);
            if (q2 == std::string::npos) { result += text[i++]; continue; }
            std::string encoding = text.substr(q1 + 1, q2 - (q1 + 1));
            size_t q3 = text.find("?=", q2 + 1);
            if (q3 == std::string::npos) { result += text[i++]; continue; }
            std::string encoded = text.substr(q2 + 1, q3 - (q2 + 1));

            std::string decoded;
            if (encoding == "B" || encoding == "b") {
                decoded = b64_decode_word(encoded);
            } else if (encoding == "Q" || encoding == "q") {
                decoded = qp_decode_word(encoded);
            } else {
                decoded = encoded;
            }

            std::string utf8 = iconv_to_utf8(decoded, charset);
            if (!utf8.empty()) {
                result += utf8;
            } else {
                result += "=?" + charset + "?" + encoding + "?" + encoded + "?=";
            }

            i = q3 + 2;
            // RFC 2047: whitespace between adjacent encoded-words is ignored
            if (i < text.size() && text[i] == ' ' &&
                i + 2 < text.size() && text[i + 1] == '=' && text[i + 2] == '?') {
                ++i;
            }
        } else {
            result += text[i++];
        }
    }
    return sanitize_utf8(result);
}

static std::string decode_qp_body(const std::string& in) {
    std::string out;
    for (size_t i = 0; i < in.size(); ++i) {
        char c = in[i];
        if (c == '=') {
            if (i + 2 < in.size() && std::isxdigit(static_cast<unsigned char>(in[i + 1])) && std::isxdigit(static_cast<unsigned char>(in[i + 2]))) {
                unsigned int b;
                std::sscanf(in.substr(i + 1, 2).c_str(), "%02x", &b);
                out.push_back(static_cast<char>(b));
                i += 2;
            } else if (i + 1 < in.size() && (in[i + 1] == '\r' || in[i + 1] == '\n' || in[i + 1] == '\0')) {
                // soft line break
                ++i;
            } else {
                out.push_back(c);
            }
        } else {
            out.push_back(c);
        }
    }
    return out;
}

static std::string extract_mime_header_value(const std::string& mime, const std::string& name) {
    std::istringstream in(mime);
    std::string line;
    std::string value;
    bool in_header = false;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) break;
        if (line[0] == ' ' || line[0] == '\t') {
            if (in_header) {
                while (!line.empty() && (line[0] == ' ' || line[0] == '\t')) line = line.substr(1);
                value += " " + line;
            }
            continue;
        }
        size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string hname = line.substr(0, colon);
        if (hname.size() == name.size() &&
            std::equal(hname.begin(), hname.end(), name.begin(),
                [](char a, char b){ return std::tolower(a) == std::tolower(b); })) {
            in_header = true;
            value = line.substr(colon + 1);
            while (!value.empty() && (value[0] == ' ' || value[0] == '\t')) value = value.substr(1);
        } else {
            in_header = false;
        }
    }
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t' || value.back() == '\r' || value.back() == '\n')) value.pop_back();
    return value;
}

static std::string extract_mime_body(const std::string& mime) {
    std::istringstream in(mime);
    std::string line;
    // skip headers
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) break;
    }
    std::ostringstream body;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        body << line << "\n";
    }
    return body.str();
}

static std::string url_encode_path(const std::string& value) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;
    for (char c : value) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << c;
        } else {
            escaped << std::uppercase;
            escaped << '%' << std::setw(2) << int(static_cast<unsigned char>(c));
            escaped << std::nouppercase;
        }
    }
    return escaped.str();
}

std::string EmailOptOutlookImpl::graph_get_message_mime(const std::string& id) {
    std::string url = "https://graph.microsoft.com/v1.0/me/messages/" + url_encode_path(id) + "/$value";
    std::string escaped_url;
    for (char c : url) {
        if (c == '$' || c == '\\' || c == '"' || c == '`') {
            escaped_url += '\\';
        }
        escaped_url += c;
    }
    std::string header = "Authorization: Bearer " + graph_access_token_;
    std::string cmd = "curl -s --connect-timeout 10 --max-time 30 -X GET \"" + escaped_url + "\" " +
                      "-H \"" + header + "\" " +
                      "-H \"Accept: text/plain\"";

    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        LOG_INFO("Outlook graph_get_message_mime: failed to run curl\n");
        return "";
    }
    char buffer[65536];
    std::string mime;
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        mime += buffer;
    }
    pclose(pipe);
    return mime;
}

std::string EmailOptOutlookImpl::parse_mime_to_json(const std::string& mime, const std::string& id) {
    nlohmann::json email;
    email["uuid"] = id;
    email["message_id"] = "";
    email["in_reply_to"] = "";
    email["subject"] = "";
    email["from"] = "";
    email["to_addr"] = "";
    email["date"] = "";
    email["x_session_chart"] = "";
    email["headers"] = "";
    email["body"] = "";

    try {
        vmime::shared_ptr<vmime::message> msg = vmime::make_shared<vmime::message>();
        vmime::shared_ptr<vmime::utility::inputStream> input =
            vmime::make_shared<vmime::utility::inputStreamStringAdapter>(mime);
        msg->parse(input, mime.length());

        auto header = msg->getHeader();
        if (header) {
            auto subjectField = header->findField("Subject");
            if (subjectField) {
                email["subject"] = decode_header(subjectField->getValue()->generate());
            }
            auto fromField = header->findField("From");
            if (fromField) {
                email["from"] = decode_header(fromField->getValue()->generate());
            }
            auto toField = header->findField("To");
            if (toField) {
                email["to_addr"] = decode_header(toField->getValue()->generate());
            }
            auto dateField = header->findField("Date");
            if (dateField) {
                email["date"] = sanitize_utf8(dateField->getValue()->generate());
            }
            auto msgIdField = header->findField("Message-Id");
            if (msgIdField) {
                email["message_id"] = sanitize_utf8(msgIdField->getValue()->generate());
            }
            auto inReplyToField = header->findField("In-Reply-To");
            if (inReplyToField) {
                email["in_reply_to"] = sanitize_utf8(inReplyToField->getValue()->generate());
            }
            auto xMailerField = header->findField("X-Mailer");
            if (xMailerField) {
                email["x_session_chart"] = sanitize_utf8(xMailerField->getValue()->generate());
            }

            LOG_INFO("Outlook parse_mime_to_json: id=%s from_raw=[%s] subject_raw=[%s]\n",
                id.c_str(),
                (fromField && fromField->getValue() ? fromField->getValue()->generate().c_str() : "N/A"),
                (subjectField && subjectField->getValue() ? subjectField->getValue()->generate().c_str() : "N/A"));
        }

        std::string mainContentType;
        if (header && header->ContentType()) {
            mainContentType = header->ContentType()->getValue()->generate();
        }

        // Extract text body from vmime
        auto body = msg->getBody();
        if (body) {
            auto contents = body->getContents();
            if (contents) {
                std::ostringstream body_os;
                vmime::utility::outputStreamAdapter body_out(body_os);
                contents->extract(body_out);
                email["body"] = convert_to_utf8(body_os.str(), extract_charset(mainContentType));
            }

            // If multipart, also look for text/plain parts
            for (size_t i = 0; i < body->getPartCount(); ++i) {
                auto part = body->getPartAt(i);
                if (!part) continue;
                auto partBody = part->getBody();
                if (!partBody) continue;
                auto partContents = partBody->getContents();
                if (!partContents) continue;
                auto contentType = part->getHeader()->ContentType()->getValue()->generate();
                if (contentType.find("text/plain") != std::string::npos) {
                    std::ostringstream body_os;
                    vmime::utility::outputStreamAdapter body_out(body_os);
                    partContents->extract(body_out);
                    email["body"] = convert_to_utf8(body_os.str(), extract_charset(contentType));
                    break;
                }
            }
        }

        // Raw headers as string
        std::ostringstream headers_os;
        vmime::utility::outputStreamAdapter headers_out(headers_os);
        header->generate(headers_out);
        email["headers"] = sanitize_utf8(headers_os.str());
    } catch (const vmime::exception& e) {
        LOG_INFO("Outlook parse_mime_to_json: vmime exception: %s\n", e.what());
    } catch (const std::exception& e) {
        LOG_INFO("Outlook parse_mime_to_json: std exception: %s\n", e.what());
    }

    // Fallback manual extraction if vmime did not populate fields (common for raw RFC 5322 messages)
    auto get_str = [](const nlohmann::json& j) -> std::string {
        try { return j.get<std::string>(); } catch (...) { return ""; }
    };
    if (get_str(email["from"]).empty()) {
        email["from"] = decode_header(extract_mime_header_value(mime, "From"));
    }
    if (get_str(email["subject"]).empty()) {
        email["subject"] = decode_header(extract_mime_header_value(mime, "Subject"));
    }
    if (get_str(email["to_addr"]).empty()) {
        email["to_addr"] = decode_header(extract_mime_header_value(mime, "To"));
    }
    if (get_str(email["date"]).empty()) {
        email["date"] = sanitize_utf8(extract_mime_header_value(mime, "Date"));
    }
    if (get_str(email["message_id"]).empty()) {
        email["message_id"] = sanitize_utf8(extract_mime_header_value(mime, "Message-Id"));
    }
    if (get_str(email["in_reply_to"]).empty()) {
        email["in_reply_to"] = sanitize_utf8(extract_mime_header_value(mime, "In-Reply-To"));
    }
    if (get_str(email["x_session_chart"]).empty()) {
        email["x_session_chart"] = sanitize_utf8(extract_mime_header_value(mime, "X-Mailer"));
    }
    if (get_str(email["body"]).empty()) {
        std::string rawBody = extract_mime_body(mime);
        std::string cte = extract_mime_header_value(mime, "Content-Transfer-Encoding");
        std::string ctype = extract_mime_header_value(mime, "Content-Type");
        std::transform(cte.begin(), cte.end(), cte.begin(), [](unsigned char c){ return std::tolower(c); });
        std::string decoded;
        if (cte.find("quoted-printable") != std::string::npos) {
            decoded = decode_qp_body(rawBody);
        } else if (cte.find("base64") != std::string::npos) {
            decoded = b64_decode_word(rawBody);
        } else {
            decoded = rawBody;
        }
        email["body"] = convert_to_utf8(decoded, extract_charset(ctype));
    }

    LOG_INFO("Outlook parse_mime_to_json: id=%s final from=[%s] subject=[%s]\n",
        id.c_str(),
        get_str(email["from"]).c_str(),
        get_str(email["subject"]).c_str());

    email["bodystructure"] = get_str(email["body"]);

    try {
        return email.dump();
    } catch (const std::exception& e) {
        LOG_INFO("Outlook parse_mime_to_json: json dump failed: %s\n", e.what());
        return "{}";
    }
}

} // namespace EmailComm
