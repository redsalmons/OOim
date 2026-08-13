#ifndef LIB_EMAIL_H
#define LIB_EMAIL_H

#include "email_opt_interface.h"
#include <string>
#include <memory>
#include <functional>

#include "cppasyncworker.hpp"

// Forward declaration to avoid circular dependency
namespace oemailim {
class EmailHandler;
}

namespace EmailComm {

// Message type for the message queue
using Message = std::function<void()>;

// Base class for email accounts
class Email {
public:
    Email(const std::string& smtp_address,
          int smtp_port,
          const std::string& imap_address,
          int imap_port)
        : smtp_address_(smtp_address)
        , smtp_port_(smtp_port)
        , imap_address_(imap_address)
        , imap_port_(imap_port)
        , delegate_(nullptr) {}

    virtual ~Email() {
        stop();
    }

    // Getters
    std::string get_address() const { return address_; }
    std::string get_smtp_address() const { return smtp_address_; }
    int get_smtp_port() const { return smtp_port_; }
    std::string get_imap_address() const { return imap_address_; }
    int get_imap_port() const { return imap_port_; }

    // Set email address (obtained after OAuth)
    void set_address(const std::string& address) { address_ = address; }

    // Set delegate for operations
    void set_delegate(std::shared_ptr<EmailOptInterface> delegate) {
        delegate_ = delegate;
    }

    // Get delegate
    std::shared_ptr<EmailOptInterface> get_delegate() const {
        return delegate_;
    }

    // Set EmailHandler reference for notifications
    void set_email_handler(std::shared_ptr<oemailim::EmailHandler> handler) {
        email_handler_ = handler;
    }

    // Send notification through EmailHandler (declaration only)
    void notify(int message, const std::string& json = "");

    // Convenience methods that delegate to the implementation
    bool connect() {
        if (!delegate_) return false;
        return delegate_->connect();
    }

    bool authority(int timeout_seconds = 120) {
        if (!delegate_) return false;
        bool result = delegate_->authority(timeout_seconds);
        if (result) {
            // Set email address after successful OAuth
            address_ = delegate_->get_email();
        }
        return result;
    }

    // Start the worker pool
    void go() {
        if (event_context_) return; // Already running
        
        // Create event context for cpphttp async operations
        event_context_ = std::make_shared<cppasyncworker::WorkerPool>(4);
    }

    // Stop the worker pool
    void stop() {
        event_context_.reset();
    }

    // Check if the worker pool is running
    bool is_running() const {
        return event_context_ != nullptr;
    }

    // Post a message to the worker pool
    void post(Message message) {
            // Submit task directly to WorkerPool
            (void)event_context_->Enqueue(std::move(message));
    }

protected:
    std::string address_;
    std::string smtp_address_;
    int smtp_port_;
    std::string imap_address_;
    int imap_port_;
    std::shared_ptr<EmailOptInterface> delegate_;

    // Event context for cpphttp async operations
    std::shared_ptr<cppasyncworker::WorkerPool> event_context_;

    // Reference to EmailHandler for notifications
    std::shared_ptr<oemailim::EmailHandler> email_handler_;
};

} // namespace EmailComm

#endif // LIB_EMAIL_H
