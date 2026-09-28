#ifndef LIBEMAIL_MESSAGE_ID_H
#define LIBEMAIL_MESSAGE_ID_H

#include <string>

// Single generator for every x-message-id produced by this process.
// Format: "<{epoch_ms}.{64-bit random hex}@{domain}>" where domain is taken
// from the account address (fallback "oim"). Thread-safe; collision-free
// across calls in the same second/millisecond.
std::string generate_x_message_id(const std::string& account);

#ifdef __cplusplus
extern "C" {
#endif

// C wrapper. Returns 0 on success, -1 on bad args, -2 if the buffer is too small.
int email_generate_x_message_id(const char* account, char* out, int outSize);

#ifdef __cplusplus
}
#endif

#endif // LIBEMAIL_MESSAGE_ID_H
