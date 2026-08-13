#include "email_core.h"
#include <stdio.h>
#include <stdlib.h>

int main() {
    printf("Email Core Example\n");
    printf("==================\n\n");

    // Initialize system
    initialize_email_system();
    printf("Version: %s\n\n", get_version());

    // Create email handler
    EmailHandler* handler = email_handler_create(10);
    if (!handler) {
        printf("Failed to create email handler\n");
        return 1;
    }

    // Add some emails
    email_add(handler, "sender@example.com", "recipient@example.com", 
              "Test Subject", "This is a test email body.");
    email_add(handler, "admin@company.com", "user@company.com",
              "Welcome", "Welcome to our service!");
    email_add(handler, "support@service.com", "customer@email.com",
              "Support Ticket", "Your ticket has been received.");

    printf("Total emails: %d\n\n", email_count(handler));

    // Display emails
    for (int i = 0; i < email_count(handler); i++) {
        Email* email = email_get(handler, i);
        if (email) {
            printf("Email %d:\n", i + 1);
            printf("  From: %s\n", email->sender);
            printf("  To: %s\n", email->recipient);
            printf("  Subject: %s\n", email->subject);
            printf("  Time: %s\n", email->timestamp);
            printf("\n");
        }
    }

    // Search emails
    Email* results = NULL;
    int result_count = 0;
    email_search(handler, "Welcome", &results, &result_count);
    
    printf("Search results for 'Welcome': %d\n", result_count);
    for (int i = 0; i < result_count; i++) {
        printf("  - %s\n", results[i].subject);
    }

    // Clean up
    if (results) {
        for (int i = 0; i < result_count; i++) {
            email_free(&results[i]);
        }
        free(results);
    }

    email_handler_destroy(handler);
    shutdown_email_system();

    return 0;
}
