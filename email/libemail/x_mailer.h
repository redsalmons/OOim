#ifndef LIBEMAIL_X_MAILER_H
#define LIBEMAIL_X_MAILER_H

#include <string>

// X-Mailer header values for message type classification
namespace XMailer {

// Header name
constexpr const char* HEADER = "X-Mailer";

// Message type values
constexpr const char* NEW_SESSION  = "0.1.0";  // New session creation
constexpr const char* EXCHANGE     = "0.1.1";  // Key exchange
constexpr const char* TEXT         = "0.1.2";  // Encrypted text message
constexpr const char* FILE_META    = "0.1.3";  // File metadata (visible in UI)
constexpr const char* FILE_CHUNK   = "0.1.4";  // File chunk (hidden from UI)

// Whitelist of all valid X-Mailer values
inline bool isValid(const std::string& value) {
    return value == NEW_SESSION ||
           value == EXCHANGE ||
           value == TEXT ||
           value == FILE_META ||
           value == FILE_CHUNK;
}

} // namespace XMailer

#endif // LIBEMAIL_X_MAILER_H
