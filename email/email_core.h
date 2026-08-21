#ifndef EMAIL_CORE_H
#define EMAIL_CORE_H

#ifdef __cplusplus
extern "C" {
#endif

// Unified logging - write message to the shared log file
void email_log_write(const char* message);

// Initialize logger - call once at app startup
int email_logger_init(const char* logDir);

// Email structure
typedef struct {
    char* sender;
    char* recipient;
    char* subject;
    char* body;
    char* timestamp;
} Email;

// Email handler structure
typedef struct {
    Email* emails;
    int count;
    int capacity;
} EmailHandler;

// Core functions
EmailHandler* email_handler_create(int initial_capacity);
void email_handler_destroy(EmailHandler* handler);

// Initialize libemail system with app support directory
int email_core_initialize(const char* appSupportDir);

int email_add(EmailHandler* handler, const char* sender, const char* recipient,
              const char* subject, const char* body);
int email_remove(EmailHandler* handler, int index);
Email* email_get(EmailHandler* handler, int index);
int email_count(EmailHandler* handler);

// Email operations
int email_send(const char* recipient, const char* subject, const char* body);
int email_send_with_headers(const char* recipient, const char* subject, const char* body, const char* in_reply_to);
int email_send_via_smtp(const char* smtp_server, int smtp_port,
                        const char* sender_email, const char* auth_code,
                        const char* recipient, const char* subject,
                        const char* body, const char* in_reply_to);
int email_receive(EmailHandler* handler);
int email_search(EmailHandler* handler, const char* query, Email** results, int* result_count);

// Utility functions
void email_free(Email* email);
const char* get_version(void);
int initialize_email_system(void);
void shutdown_email_system(void);

// Account configuration structure
typedef struct {
    char* type;          // "163.com" / "gmail.com" / "outlook.com"
    char* email;
    char* auth_code;
    char* smtp_server;
    int smtp_port;
    char* imap_server;
    int imap_port;
    char* account_type;  // "personal" / "enterprise"
    int authorized;       // 0 = false, 1 = true
    char* id;             // Unique identifier
    int uid;              // Default 0
    char* phrase;         // Encrypted phrase (base64)
    long long folder_size; // Machine folder size in bytes
} EmailAccountConfig;

// Save account configurations (and local data path) to a config file.
// Returns 0 on success, negative on error.
int email_config_save(const char* path, const char* local_data_path,
                       const EmailAccountConfig* accounts, int count);

// Load account configurations from a config file.
// On success, *accounts is malloc'd (caller must free with email_config_free),
// *count is set, and *local_data_path is malloc'd (caller must free with free()).
// Returns 0 on success, negative on error (e.g. file not found).
int email_config_load(const char* path, char** local_data_path,
                       EmailAccountConfig** accounts, int* count);

// Check whether a config file exists at path. Returns 1 if exists, 0 otherwise.
int email_config_exists(const char* path);

// Free an array of EmailAccountConfig allocated by email_config_load.
void email_config_free(EmailAccountConfig* accounts, int count);

// Save emails from handler to a file. Returns 0 on success, negative on error.
int email_save(EmailHandler* handler, const char* path);

// Load emails from a file into handler. Returns 0 on success, negative on error.
int email_load(EmailHandler* handler, const char* path);

// Chat contact structure
typedef struct {
    char* name;
    char* last_message;
    char* time;
    int unread;
} ChatContact;

// Chat message structure
typedef struct {
    char* sender;
    char* content;
    char* time;
    int is_me;  // 0 = false, 1 = true
} ChatMessage;

// Save chat contacts to a file. Returns 0 on success, negative on error.
int chat_contacts_save(const char* path, const ChatContact* contacts, int count);

// Load chat contacts from a file. Returns 0 on success, negative on error.
int chat_contacts_load(const char* path, ChatContact** contacts, int* count);

// Free chat contacts array allocated by chat_contacts_load.
void chat_contacts_free(ChatContact* contacts, int count);

// Save chat messages to a file. Returns 0 on success, negative on error.
int chat_messages_save(const char* path, const ChatMessage* messages, int count);

// Load chat messages from a file. Returns 0 on success, negative on error.
int chat_messages_load(const char* path, ChatMessage** messages, int* count);

// Free chat messages array allocated by chat_messages_load.
void chat_messages_free(ChatMessage* messages, int count);

// SQLite database handle (opaque pointer)
typedef struct sqlite3 sqlite3;

// Initialize SQLite database for email storage. Returns 0 on success, negative on error.
int email_db_init(const char* path);

// Close SQLite database.
void email_db_close(void);

// Insert email into database. Returns 0 on success, negative on error.
int email_db_insert(const char* email, const char* sender, const char* recipient,
                    const char* subject, const char* body, const char* timestamp);

// Query emails from database by email address. Returns 0 on success, negative on error.
// On success, caller must free the returned handler.
int email_db_query(const char* email, EmailHandler* handler);

// IMAP connection handle (opaque pointer)
typedef struct IMAPConnection IMAPConnection;

// Create IMAP connection. Returns NULL on error.
IMAPConnection* imap_create(const char* server, int port, const char* email, const char* auth_code);

// Destroy IMAP connection.
void imap_destroy(IMAPConnection* conn);

// Fetch INBOX emails using PEEK. Returns 0 on success, negative on error.
// Fetched emails are stored in the database.
int imap_fetch_inbox(IMAPConnection* conn);

// Set email credentials on a config index (email address and auth code)
// Returns 0 on success, negative on error.
int email_set_credentials(int configIndex, const char* email, const char* authCode);

// Connect to email server and authenticate. Returns 0 on success, negative on error.
int email_connect(int configIndex);

// List emails in a folder. Returns JSON string in outJson.
// Returns 0 on success, negative on error.
int email_list(int configIndex, const char* folder, char* outJson, int outSize);

// Get email content by UID. Returns JSON string in outJson.
// Returns 0 on success, negative on error.
int email_get_content(int configIndex, const char* folder, const char* uid, char* outJson, int outSize);

// Fetch email headers (uuid, sender, from, subject, date, bodystructure) from IMAP
// and store them in the localemail SQLite table.
// Returns 0 on success, negative on error. outJson contains the fetched emails JSON.
int email_fetch_and_store(int configIndex, const char* folder, const char* startUid,
                          const char* account, char* outJson, int outSize);

// Insert sent email into database. Returns 0 on success, negative on error.
int email_insert_sent_email(const char* account, const char* sender, const char* from_addr, const char* to_addr, const char* subject, const char* date, const char* message_id, const char* in_reply_to, const char* body, const char* storageDir, char* outJson, int outSize);

// Query session_id by message_id. Returns 0 on success, negative on error.
int email_query_session_by_message_id(const char* messageId, const char* account, char* outSessionId, int outSize);

// Add an email to an existing session by uuid and session_id. Returns 0 on success, negative on error.
int email_add_email_to_session(const char* sessionId, const char* uuid, const char* account, int encrypt_method, char* outJson, int outSize);

// Query localemail table for stored emails by account.
// Returns 0 on success, negative on error. outJson contains the emails JSON.
int email_query_localemail(const char* account, char* outJson, int outSize);

// Query thread root emails (first email of each conversation thread) by account.
// Returns 0 on success, negative on error. outJson contains the thread root emails JSON.
int email_query_thread_roots(const char* account, char* outJson, int outSize);

// Get max UID from localemail table for a specific account.
// Returns 0 on success, negative on error. outUid contains the max UID (or empty if none).
int email_get_max_uid(const char* account, const char* folder, char* outUid, int outSize);

// oemailim C wrappers (for Dart FFI)
int oemailim_system_open(const char* dataDir, const char* configDir, const char* logDir);
int oemailim_open_new_email(const char* email_id);
int oemailim_go(int configIndex);
int oemailim_authority(int configIndex);
void oemailim_system_close(int configIndex);
int oemailim_email_list(int configIndex, const char* path, char* outJson, int outSize);
int oemailim_email_select(int configIndex, const char* path, char* outJson, int outSize);

// Enter IMAP IDLE on the current folder, block until server notification.
// Returns 1 if notification received, 0 if timeout, negative on error.
int email_idle_wait(int configIndex, const char* folder, int timeoutSeconds);

// Discover the Sent folder name via IMAP SPECIAL-USE or name matching.
// Returns 0 on success, negative on error. outFolder contains the folder name.
int email_find_sent_folder(int configIndex, char* outFolder, int outSize);

int email_send_via_config(int configIndex, const char* content);

int email_get_last_error(int configIndex, char* outBuf, int outSize);

// Download email bodies for emails with islocal=0.
// Fetches full body via IMAP, saves to <storageDir>/<account>/<uuid>.eml, updates islocal=1.
// Returns number of emails downloaded, or negative on error.
int email_download_pending_bodies(int configIndex, const char* account,
                                  const char* storageDir, char* outJson, int outSize);

// Count pending email bodies (islocal=0) for a given account.
// Returns count, or negative on error. Does not connect to IMAP.
int email_count_pending_bodies(const char* account);

// Parse an .eml file using vmime and return structured JSON.
// Extracts text body, HTML body, and attachment info.
// Returns 0 on success, negative on error. outJson contains the parsed result.
int email_parse_eml(const char* filePath, char* outJson, int outSize);

// Save a specific attachment from an EML file to a target path.
// Returns 0 on success, negative on error.
int email_save_attachment(const char* emlPath, int attachmentIndex, const char* outputPath);

// Update session isread field to 1 for all emails in a session.
// Returns 0 on success, negative on error.
int email_update_session_read(const char* sessionId);

// Query unread count for a session (count of records with isread=0).
// Returns count on success, negative on error.
int email_query_session_unread(const char* sessionId);

// Code table operations (store peer's ECC public key, secretkey, and identify=MD5(pubkey))
int email_code_insert(const char* account, const char* pubkey, const char* secretkey, const char* sessionUuid);
int email_code_query_by_account(const char* account, char* outJson, int outSize);
int email_code_query_by_identify(const char* identify, char* outJson, int outSize);

// Create a new session with ECC key pair generation. Returns 0 on success.
int email_create_session(const char* account, const char* subject, const char* members, const char* message_id, int encrypt_method, char* outJson, int outSize);

// Prepare encrypted data body for x_start_new=data messages.
// Takes plaintext, recipient list (comma-separated), and sender account.
// Returns encrypted JSON body string in outJson.
int email_prepare_data_body(const char* plaintext, const char* recipients, const char* sender, const char* sessionUuid, char* outJson, int outSize);

// Decrypt data body for x_start_new=data messages.
// Takes encrypted JSON body and the account doing the decryption.
// Returns plaintext in outJson.
int email_decrypt_data_body(const char* encryptedBody, const char* account, char* outJson, int outSize);

// Task table operations for queued email sending
int email_task_insert(const char* account, const char* recipient,
                     const char* subject, const char* body,
                     const char* in_reply_to, const char* message_id,
                     const char* x_message_id, const char* session_id,
                     const char* x_session_chart);
int email_task_query_pending(const char* account, char* outJson, int outSize);
int email_task_mark_sent(int taskId);
int email_task_mark_failed(int taskId);
int email_task_delete(int taskId);
int email_task_process_pending(int configIndex, const char* account, char* outJson, int outSize);

// Migration: Update islocal for existing emails
int email_migrate_islocal();

// --- File Transfer Protocol ---

// Prepare file metadata message JSON (plaintext for encryption).
int email_prepare_file_message(const char* fileId, const char* fileName,
                                long long fileSize, const char* fileMd5,
                                int totalChunks, int chunkSize,
                                const char* text, const char* batchId,
                                char* outJson, int outSize);

// Prepare chunk message JSON (plaintext for encryption).
int email_prepare_truck_message(const char* fileId, int chunkIndex,
                                 const char* chunkDataB64, const char* chunkMd5,
                                 char* outJson, int outSize);

// Split a file into chunks and create send tasks for file metadata + each chunk.
// Returns 0 on success. outJson contains file_id and metadata.
int email_file_split_and_send(const char* filePath, const char* fileName,
                               const char* account, const char* recipient,
                               const char* sessionId, const char* inReplyTo,
                               const char* subject, const char* text,
                               const char* batchId,
                               char* outJson, int outSize);

// Process a received "file" metadata message (create file_transfer record on receiver).
int email_file_transfer_receive_file(const char* fileId, const char* sessionId,
                                      const char* account, const char* sender,
                                      const char* fileName, long long fileSize,
                                      const char* fileMd5, int totalChunks, int chunkSize,
                                      const char* messageId,
                                      char* outJson, int outSize);

// Process a received "truck" chunk message (store chunk, auto-reassemble if complete).
int email_file_transfer_receive_truck(const char* fileId, int chunkIndex,
                                       const char* chunkDataB64, const char* chunkMd5,
                                       const char* outputDir,
                                       char* outJson, int outSize);

// Query file transfer status by file_id.
int email_file_transfer_query(const char* fileId, char* outJson, int outSize);

// Query all pending file transfers for an account.
int email_file_transfer_query_pending(const char* account, char* outJson, int outSize);

// Reassemble a file from received chunks (manual trigger).
int email_file_transfer_reassemble(const char* fileId, const char* outputDir,
                                    char* outJson, int outSize);

#ifdef __cplusplus
}
#endif

#endif // EMAIL_CORE_H
