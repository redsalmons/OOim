#ifndef OIM_BACKUP_FFI_H
#define OIM_BACKUP_FFI_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Paths: NUL-terminated UTF-8. Password: UTF-8 bytes, never logged.
// Status: 0 success, -1 invalid arguments, -2 authentication failure,
// -3 invalid format, -4 file I/O, -5 size limit, -6 crypto/panic, -7 short password.
// These functions handle the encrypted container, NOT database activation.
int32_t oim_backup_seal_file(const char* destination, const uint8_t* payload,
                            size_t payload_len, const uint8_t* password, size_t password_len);
// On failure output is NULL/0. On success the caller must free with the function
// below, supplying exactly the original pointer and length.
int32_t oim_backup_open_file(const char* source, const uint8_t* password,
                            size_t password_len, uint8_t** out, size_t* out_len);
void oim_backup_free_buffer(uint8_t* buffer, size_t len);

#ifdef __cplusplus
}
#endif
#endif
