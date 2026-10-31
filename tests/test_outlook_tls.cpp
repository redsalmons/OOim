// Standalone test for Outlook IMAP TLS connection via vmime.
// Build: see tests/CMakeLists.txt
// Usage: ./test_outlook_tls <email> <access_token>
//
// This program does NOT depend on the OIM email_core library. It only uses
// vmime directly to isolate TLS / SASL XOAUTH2 issues from the rest of the app.

#include <iostream>
#include <string>
#include <vmime/vmime.hpp>
#include <vmime/platforms/posix/posixHandler.hpp>
#include <vmime/security/sasl/XOAuth2SASLAuthenticator.hpp>

namespace {

// Accept all certificates (for testing only).
class TrustAllVerifier : public vmime::security::cert::defaultCertificateVerifier {
public:
    void verify(
        const vmime::shared_ptr<vmime::security::cert::certificateChain>&,
        const vmime::string& hostname) override {
        std::cout << "  [TLS] certificate accepted for " << hostname << "\n";
    }
};

void initVmime() {
    static bool done = false;
    if (!done) {
        vmime::platform::setHandler<vmime::platforms::posix::posixHandler>();
        done = true;
    }
}

// Test 1: pure TCP + TLS handshake, no IMAP auth.
bool testTlsHandshake(const std::string& host, int port) {
    std::cout << "\n=== Test 1: TLS handshake to " << host << ":" << port << " ===\n";
    try {
        initVmime();
        auto sess = vmime::net::session::create();
        vmime::utility::url url("imaps", host, port);
        auto store = sess->getStore(url);
        store->setCertificateVerifier(vmime::make_shared<TrustAllVerifier>());
        store->connect();
        std::cout << "  OK: connected + TLS handshake + IMAP greeting OK\n";
        store->disconnect();
        return true;
    } catch (const vmime::exception& e) {
        std::cout << "  FAIL (vmime): " << e.what() << "\n";
        return false;
    } catch (const std::exception& e) {
        std::cout << "  FAIL (std): " << e.what() << "\n";
        return false;
    }
}

// Test 2: full IMAP connect + XOAUTH2 login using defaultAuthenticator
// with prefixed session properties.
bool testXOAuth2Default(const std::string& email, const std::string& token,
                        const std::string& host, int port) {
    std::cout << "\n=== Test 2: XOAUTH2 login (defaultAuthenticator, prefixed props) ===\n";
    try {
        initVmime();
        auto sess = vmime::net::session::create();
        const std::string prefix = "store.imaps.";
        sess->getProperties()[prefix + "auth.username"] = email;
        sess->getProperties()[prefix + "auth.password"] = token;
        sess->getProperties()[prefix + "auth.accesstoken"] = token;

        vmime::utility::url url("imaps", host, port);
        auto store = sess->getStore(url);
        store->setCertificateVerifier(vmime::make_shared<TrustAllVerifier>());
        store->connect();
        std::cout << "  OK: connected + XOAUTH2 login OK\n";

        // Try listing INBOX
        auto folder = store->getDefaultFolder();
        folder->open(vmime::net::folder::MODE_READ_ONLY);
        std::cout << "  INBOX message count: " << folder->getMessageCount() << "\n";
        folder->close(false);
        store->disconnect();
        return true;
    } catch (const vmime::exception& e) {
        std::cout << "  FAIL (vmime): " << e.what() << "\n";
        return false;
    } catch (const std::exception& e) {
        std::cout << "  FAIL (std): " << e.what() << "\n";
        return false;
    }
}

// Test 3: full IMAP connect + XOAUTH2 login using a custom XOAuth2SASLAuthenticator
// that directly returns credentials (no property lookup).
using vmime::security::sasl::XOAuth2SASLAuthenticator;

class DirectXOAuth2 : public XOAuth2SASLAuthenticator {
public:
    DirectXOAuth2(const vmime::string& user, const vmime::string& token)
        : XOAuth2SASLAuthenticator(MODE_EXCLUSIVE),
          m_user(user), m_token(token) {}
    const vmime::string getUsername() const override { return m_user; }
    const vmime::string getAccessToken() const override { return m_token; }
    const vmime::string getPassword() const override { return m_token; }
    const vmime::string getHostname() const override { return vmime::string(); }
    const vmime::string getAnonymousToken() const override { return m_user; }
    const vmime::string getServiceName() const override { return vmime::string(); }
private:
    vmime::string m_user, m_token;
};

bool testXOAuth2Custom(const std::string& email, const std::string& token,
                       const std::string& host, int port) {
    std::cout << "\n=== Test 3: XOAUTH2 login (custom XOAuth2SASLAuthenticator) ===\n";
    try {
        initVmime();
        auto sess = vmime::net::session::create();
        vmime::shared_ptr<vmime::security::authenticator> auth =
            vmime::make_shared<DirectXOAuth2>(email, token);
        vmime::utility::url url("imaps", host, port);
        auto store = sess->getStore(url, auth);
        store->setCertificateVerifier(vmime::make_shared<TrustAllVerifier>());
        store->connect();
        std::cout << "  OK: connected + XOAUTH2 login OK\n";

        auto folder = store->getDefaultFolder();
        folder->open(vmime::net::folder::MODE_READ_ONLY);
        std::cout << "  INBOX message count: " << folder->getMessageCount() << "\n";
        folder->close(false);
        store->disconnect();
        return true;
    } catch (const vmime::exception& e) {
        std::cout << "  FAIL (vmime): " << e.what() << "\n";
        return false;
    } catch (const std::exception& e) {
        std::cout << "  FAIL (std): " << e.what() << "\n";
        return false;
    }
}

} // namespace

int main(int argc, char** argv) {
    std::string host = "outlook.office365.com";
    int port = 993;

    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " <email> [access_token]\n";
        std::cout << "  - With only email: runs TLS handshake test only\n";
        std::cout << "  - With email+token: runs TLS + XOAUTH2 login tests\n";
        return 1;
    }

    std::string email = argv[1];
    std::string token = (argc >= 3) ? argv[2] : "";

    std::cout << "Outlook IMAP TLS test\n";
    std::cout << "  host: " << host << ":" << port << "\n";
    std::cout << "  email: " << email << "\n";
    std::cout << "  token: " << (token.empty() ? "(none)" : "len=" + std::to_string(token.size())) << "\n";

    bool ok = true;
    ok &= testTlsHandshake(host, port);

    if (!token.empty()) {
        ok &= testXOAuth2Default(email, token, host, port);
        ok &= testXOAuth2Custom(email, token, host, port);
    }

    std::cout << "\n=== Summary: " << (ok ? "ALL PASSED" : "SOME FAILED") << " ===\n";
    return ok ? 0 : 1;
}
