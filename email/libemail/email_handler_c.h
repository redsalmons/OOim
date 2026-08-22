#ifndef OEMAILIM_EMAIL_HANDLER_C_H
#define OEMAILIM_EMAIL_HANDLER_C_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/**
 * @brief Email provider types
 */
typedef enum {
    EMAIL_PROVIDER_163 = 0,   // 163 email provider
    EMAIL_PROVIDER_OUTLOOK = 1, // Outlook email provider
    EMAIL_PROVIDER_GMAIL = 2   // Gmail email provider
} EmailProvider;

/**
 * @brief Initialize the email system (C interface)
 * @param dataDir Data directory path (null-terminated string)
 * @param configDir Configuration directory path (null-terminated string)
 * @param logDir Log directory path (null-terminated string)
 * @return 1 if initialization successful, 0 otherwise
 */
int systemOpen_c(const char* dataDir,
                 const char* configDir,
                 const char* logDir);

/**
 * @brief Set notification callback for browser launch and other events
 * @param callback Function pointer to callback function
 */
void oemailim_set_callback(void* callback);

/**
 * @brief Open a new email composition by email ID (C interface)
 * @param email_id Email ID from configuration (null-terminated string)
 * @return Config index for the new email, or -1 on error
 */
int OpenNewEmail_c(const char* email_id);

/**
 * @brief Start the email worker thread (C interface)
 * @param configIndex Config index returned by OpenNewEmail_c
 * @return 0 if successful, negative on error
 */
int Go_c(int configIndex);

/**
 * @brief Perform OAuth authorization (C interface)
 * @param configIndex Config index returned by OpenNewEmail_c
 * @return 0 if successful, negative on error
 */
int Authority_c(int configIndex);

int AddOutlookEmail_c();

/**
 * @brief Get email address for a config (C interface)
 * @param configIndex Config index returned by OpenNewEmail_c
 * @param outEmail Buffer to store email address
 * @param outSize Size of the output buffer
 * @return 0 if successful, negative on error
 */
int GetEmailAddress_c(int configIndex, char* outEmail, int outSize);

/**
 * @brief Get refresh token for a config (C interface)
 * @param configIndex Config index returned by OpenNewEmail_c
 * @param outToken Buffer to store refresh token
 * @param outSize Size of the output buffer
 * @return 0 if successful, negative on error
 */
int GetRefreshToken_c(int configIndex, char* outToken, int outSize);

/**
 * @brief Set IMAP server and port for a config (C interface)
 * @param configIndex Config index returned by OpenNewEmail_c
 * @param server IMAP server address
 * @param port IMAP port
 * @return 0 if successful, negative on error
 */
int SetImapServer_c(int configIndex, const char* server, int port);

/**
 * @brief Set SMTP server and port for a config (C interface)
 * @param configIndex Config index returned by OpenNewEmail_c
 * @param server SMTP server address
 * @param port SMTP port
 * @return 0 if successful, negative on error
 */
int SetSmtpServer_c(int configIndex, const char* server, int port);

/**
 * @brief Set account type for Outlook accounts (C interface)
 * @param configIndex Config index returned by OpenNewEmail_c
 * @param accountType Account type: "personal" or "enterprise"
 * @return 0 if successful, negative on error
 */
int SetAccountType_c(int configIndex, const char* accountType);

/**
 * @brief Set refresh token for a config (C interface)
 * @param configIndex Config index returned by OpenNewEmail_c
 * @param token Refresh token
 * @return 0 if successful, negative on error
 */
int SetRefreshToken_c(int configIndex, const char* token);

/**
 * @brief Refresh access token using refresh token (C interface)
 * @param configIndex Config index returned by OpenNewEmail_c
 * @return 0 if successful, negative on error
 */
int RefreshToken_c(int configIndex);

/**
 * @brief Stop thread and release resources for an email (C interface)
 * @param configIndex Config index returned by OpenNewEmail_c
 */
void systemClose_c(int configIndex);

/**
 * @brief List emails in a folder (C interface)
 * @param configIndex Config index returned by OpenNewEmail_c
 * @param path Folder path (default "*" for root directory)
 * @param outJson Output buffer for JSON result (must be freed by caller)
 * @param outSize Size of output buffer
 * @return 0 if successful, negative on error
 */
int Email_List_c(int configIndex, const char* path, char* outJson, int outSize);

/**
 * @brief Select a folder (C interface)
 * @param configIndex Config index returned by OpenNewEmail_c
 * @param path Folder path to select
 * @param outJson Output buffer for JSON result (must be freed by caller)
 * @param outSize Size of output buffer
 * @return 0 if successful, negative on error
 */
int Email_Select_c(int configIndex, const char* path, char* outJson, int outSize);

/**
 * @brief Set email credentials (email address and auth code) on an email config
 * @param configIndex Config index returned by OpenNewEmail_c
 * @param email Email address
 * @param authCode Authorization code or password
 * @return 0 if successful, negative on error
 */
int SetCredentials_c(int configIndex, const char* email, const char* authCode);

/**
 * @brief Get email content by UID (C interface)
 * @param configIndex Config index returned by OpenNewEmail_c
 * @param folder Folder path
 * @param uid Email UID
 * @param outJson Output buffer for JSON result
 * @param outSize Size of output buffer
 * @return 0 if successful, negative on error
 */
int GetEmail_c(int configIndex, const char* folder, const char* uid, char* outJson, int outSize);

/**
 * @brief Get email content by UID and save directly to file (C interface)
 * @param configIndex Config index returned by OpenNewEmail_c
 * @param folder Folder path
 * @param uid Email UID
 * @param filePath Full path to save the email content
 * @return 0 if successful, negative on error
 */
int GetEmailToFile_c(int configIndex, const char* folder, const char* uid, const char* filePath);

/**
 * @brief Fetch email headers and store in localemail table (C interface)
 * @param configIndex Config index returned by OpenNewEmail_c
 * @param folder Folder path (e.g. "INBOX")
 * @param startUid Only fetch emails with UID >= startUid (empty for all)
 * @param account Email account address for storing in localemail table
 * @param outJson Output buffer for JSON result with fetched emails
 * @param outSize Size of output buffer
 * @return 0 if successful, negative on error
 */
int FetchAndStore_c(int configIndex, const char* folder, const char* startUid,
                    const char* account, const char* storageDir,
                    char* outJson, int outSize);

/**
 * @brief Enter IMAP IDLE on the current folder, block until server notification
 * @param configIndex Config index returned by OpenNewEmail_c
 * @param folder Folder path to select before IDLE (e.g. "INBOX", "SENT")
 * @param timeoutSeconds Maximum seconds to wait (0 = no timeout)
 * @return 1 if notification received, 0 if timeout, negative on error
 */
int IdleWait_c(int configIndex, const char* folder, int timeoutSeconds);

int FindSentFolder_c(int configIndex, char* outFolder, int outSize);

int SendEmail_c(int configIndex, const char* content);

int GetLastError_c(int configIndex, char* outBuf, int outSize);

#ifdef __cplusplus
}
#endif

#endif // OEMAILIM_EMAIL_HANDLER_C_H
