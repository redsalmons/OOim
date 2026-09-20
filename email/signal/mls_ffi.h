// C ABI of the Rust MLS (RFC 9420 / OpenMLS) library in email/mls_ffi.
// Group (1:n) protocol only — independent from the 1:1 Double Ratchet code.
//
// Conventions:
//   account       NUL-terminated local account email
//   group_id_hex  NUL-terminated hex GroupId
//   (ptr,len)     binary buffers; output functions return bytes written (>0),
//                 or a negative error. If the output buffer is too small the return
//                 value is -(needed_size) and nothing is written.
//
// Error codes: -1 bad argument, -2 account not initialised, -3 group not found, -4 MLS error.

#ifndef OIM_MLS_FFI_H
#define OIM_MLS_FFI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int mls_init(const char* account);

int mls_generate_key_package(const char* account, uint8_t* out, int out_len);

int mls_create_group(const char* account, uint8_t* out_gid, int gid_len);

int mls_add_members(const char* account, const char* group_id_hex, const char* key_packages_json,
                    uint8_t* out_welcome, int welcome_len, int* out_welcome_written,
                    uint8_t* out_commit, int commit_len, int* out_commit_written);

int mls_join_group(const char* account,
                   const uint8_t* welcome, int welcome_len,
                   const uint8_t* ratchet_tree, int tree_len,
                   uint8_t* out_gid, int gid_len);

int mls_export_ratchet_tree(const char* account, const char* group_id_hex, uint8_t* out, int out_len);

int mls_encrypt(const char* account, const char* group_id_hex,
                const uint8_t* plaintext, int pt_len, uint8_t* out, int out_len);

int mls_decrypt(const char* account, const char* group_id_hex,
                const uint8_t* ciphertext, int ct_len, uint8_t* out, int out_len);

// Last FFI error message (e.g. the OpenMLS error behind a generic rc=-4).
int mls_last_error(uint8_t* out, int out_len);

int mls_process_commit(const char* account, const char* group_id_hex,
                       const uint8_t* commit, int commit_len);

int mls_remove_member(const char* account, const char* group_id_hex, int leaf_index,
                      uint8_t* out_commit, int commit_len);

int mls_get_epoch(const char* account, const char* group_id_hex);

int mls_get_members(const char* account, const char* group_id_hex, uint8_t* out, int out_len);

int mls_save_state(const char* account, uint8_t* out, int out_len);

int mls_load_state(const char* account, const uint8_t* blob, int blob_len);

#ifdef __cplusplus
}
#endif

#endif // OIM_MLS_FFI_H
