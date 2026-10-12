#pragma once

#include <string>
#include <utility>
#include <vector>

// In-process HTTPS client (httplib + OpenSSL). Server certificates are
// verified against the platform trust store: macOS keychain roots, the
// Windows system store, or Android's /system/etc/security/cacerts.
namespace https {

struct Response {
    int status = 0;     // HTTP status; 0 when the request never completed
    std::string body;
    std::string error;  // transport error description when status == 0
};

using Headers = std::vector<std::pair<std::string, std::string>>;

// method: "GET" or "POST". url is absolute and sent verbatim (no re-encoding).
Response request(const std::string& method, const std::string& url,
                 const Headers& headers = {}, const std::string& body = "",
                 const std::string& content_type = "", int read_timeout_sec = 30);

} // namespace https
