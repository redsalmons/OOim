#!/usr/bin/env python3
import smtplib
import ssl
from email.mime.text import MIMEText
from email.mime.multipart import MIMEMultipart

# 163 SMTP settings
smtp_server = "smtp.163.com"
smtp_port = 465
email = "yangbo_889@163.com"
# Use authorization code instead of password
auth_code = "WRZQ2QLu6dAfJSjF"  # Replace with actual authorization code

try:
    # Create SSL context
    context = ssl.create_default_context()
    context.check_hostname = False
    context.verify_mode = ssl.CERT_NONE
    
    # Connect to SMTP server
    with smtplib.SMTP_SSL(smtp_server, smtp_port, context=context) as server:
        server.set_debuglevel(2)
        
        # Login with authorization code
        server.login(email, auth_code)
        
        # Create test email
        msg = MIMEMultipart()
        msg['From'] = email
        msg['To'] = "app_588@outlook.com"
        msg['Subject'] = "Test from Python"
        msg.attach(MIMEText("Test message", 'plain'))
        
        # Send email
        server.sendmail(email, "app_588@outlook.com", msg.as_string())
        print("Email sent successfully!")
        
except Exception as e:
    print(f"Error: {e}")
