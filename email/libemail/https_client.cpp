#include "https_client.h"

#include <httplib.h>

#if defined(__ANDROID__)
#include <dirent.h>
#include <fstream>
#include <iterator>
#endif

namespace https {

#if defined(__ANDROID__)
// Android keeps one PEM file per trusted CA in this directory; OpenSSL's
// CApath lookup can't use it directly (file names use the legacy subject
// hash), so the files are concatenated into one in-memory bundle.
static const std::string& android_ca_bundle() {
    static const std::string bundle = [] {
        const std::string dir_path = "/system/etc/security/cacerts/";
        std::string pem;
        if (DIR* dir = opendir(dir_path.c_str())) {
            while (dirent* entry = readdir(dir)) {
                if (entry->d_name[0] == '.') continue;
                std::ifstream file(dir_path + entry->d_name);
                pem.append(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
                pem += '\n';
            }
            closedir(dir);
        }
        return pem;
    }();
    return bundle;
}
#endif

Response request(const std::string& method, const std::string& url,
                 const Headers& headers, const std::string& body,
                 const std::string& content_type, int read_timeout_sec) {
    const size_t scheme_end = url.find("://");
    if (scheme_end == std::string::npos) return {0, "", "invalid url: " + url};
    const size_t path_start = url.find('/', scheme_end + 3);
    const std::string origin = url.substr(0, path_start);
    const std::string path = path_start == std::string::npos ? "/" : url.substr(path_start);

    httplib::Client client(origin);
    if (!client.is_valid()) return {0, "", "unsupported url: " + origin};
    client.set_connection_timeout(10);
    client.set_read_timeout(read_timeout_sec);
    client.set_url_encode(false);
#if defined(__ANDROID__)
    const std::string& ca = android_ca_bundle();
    client.load_ca_cert_store(ca.data(), ca.size());
#endif

    if (method != "GET" && method != "POST") return {0, "", "unsupported method: " + method};
    const httplib::Headers request_headers(headers.begin(), headers.end());
    httplib::Result result = method == "GET"
        ? client.Get(path, request_headers)
        : client.Post(path, request_headers, body, content_type);
    if (!result) return {0, "", httplib::to_string(result.error())};
    return {result->status, std::move(result->body), ""};
}

} // namespace https
