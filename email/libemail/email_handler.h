#ifndef OEMAILIM_EMAIL_HANDLER_H
#define OEMAILIM_EMAIL_HANDLER_H

#include <string>
#include <functional>
#include <vector>
#include <memory>
#include "email.h"
#include "email_163.h"
#include "email_outlook.h"
#include "email_google.h"
#include "config_loader.h"

namespace oemailim {

// Email provider types
enum class EmailProvider {
    EMAIL_PROVIDER_163 = 0,   // 163 email provider
    EMAIL_PROVIDER_OUTLOOK = 1, // Outlook email provider
    EMAIL_PROVIDER_GMAIL = 2   // Gmail email provider
};

// Notification message type macros
#define NOTIFICATION_MESSAGE_BROWSER_LAUNCH 1  // Request to launch browser
#define NOTIFICATION_MESSAGE_ERROR 2          // Error occurred
#define NOTIFICATION_MESSAGE_SUCCESS 3        // Operation succeeded
#define NOTIFICATION_MESSAGE_AUTH_COMPLETE 4   // OAuth authorization completed
#define NOTIFICATION_MESSAGE_EMAIL_SENT 5     // Email sent successfully

// Notification callback function type
// Parameters: configIndex (int), message (int), json (string)
using NotificationCallback = std::function<void(int, int, const std::string&)>;

/**
 * @brief Email handler class for email operations (Singleton)
 */
class EmailHandler {
public:
    EmailHandler();
    ~EmailHandler();

    // Delete copy constructor and assignment operator
    EmailHandler(const EmailHandler&) = delete;
    EmailHandler& operator=(const EmailHandler&) = delete;

    /**
     * @brief Get the singleton instance
     * @return Shared pointer to the singleton instance
     */
    static std::shared_ptr<EmailHandler> getInstance();

    /**
     * @brief Initialize the email system
     * @param dataDir Data directory path
     * @param configDir Configuration directory path
     * @param logDir Log directory path
     * @param callback Notification callback function
     * @return true if initialization successful, false otherwise
     */
    bool systemOpen(const std::string& dataDir,
                    const std::string& configDir,
                    const std::string& logDir,
                    NotificationCallback callback);

    /**
     * @brief Open a new email composition by email ID
     * @param email_id Email ID from configuration
     * @return Config index for the new email, or -1 if not found
     */
    int OpenNewEmail(const std::string& email_id);

    int AddOutlookEmail();

    /**
     * @brief Send notification callback
     * @param email Email object that is sending the notification
     * @param message Message type (use NOTIFICATION_MESSAGE_* macros)
     * @param json JSON string with additional parameters
     */
    void notify(EmailComm::Email* email, int message, const std::string& json = "");

    /**
     * @brief Load email configuration from config structure
     * @param config Email configuration structure
     */
    void loadEmailConfig(const oemail::EmailConfig& config);

    // Static member variables
    static std::shared_ptr<EmailHandler> g_instance;
    static std::vector<std::shared_ptr<EmailComm::Email>> g_EmailConfigIndices;
    static std::string g_workspace;
    static std::string g_data;
    static std::string g_log;
    static NotificationCallback g_callback;

private:
    std::string dataDir;
    std::string configDir;
    std::string logDir;
    NotificationCallback callback;
    int nextConfigIndex;
};

} // namespace oemailim

#endif // OEMAILIM_EMAIL_HANDLER_H
