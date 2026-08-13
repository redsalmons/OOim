#!/usr/bin/env python3
import imaplib
import ssl
import sys

# Test 163 IMAP connection
EMAIL = "yangbo_889@163.com"
AUTH_CODE = "WRZQ2QLu6dAfJSjF"

def test_163_imap():
    print("Testing 163 IMAP connection...")
    print(f"Email: {EMAIL}")
    print(f"Server: imap.163.com:993")
    
    # Create SSL context
    context = ssl.create_default_context()
    context.check_hostname = False
    context.verify_mode = ssl.CERT_NONE
    
    try:
        # Connect to IMAP server
        print("\n1. Connecting to imap.163.com:993...")
        imap = imaplib.IMAP4_SSL("imap.163.com", 993, ssl_context=context)
        print("   ✓ Connected")
        
        # Login
        print("\n2. Logging in...")
        imap.login(EMAIL, AUTH_CODE)
        print("   ✓ Logged in")
        
        # Send ID command AFTER login to simulate Apple Mail (required by 163)
        print("\n2.5. Sending ID command to simulate Apple Mail (after login)...")
        try:
            # Use raw command to send ID (send requires bytes)
            tag = imap._new_tag()
            cmd = f'{tag} ID ("name" "iOS Mail" "version" "17.0" "os" "iOS" "os-version" "17.0" "vendor" "Apple Inc.")\r\n'
            imap.send(cmd.encode())
            status, response = imap._get_tagged_response(tag)
            print(f"   ID Status: {status}")
            print(f"   ID Response: {response}")
            if status == "OK":
                print("   ✓ ID command successful")
        except Exception as e:
            print(f"   ID command failed (may not be critical): {e}")
        
        # Test SELECT INBOX
        print("\n3. Testing SELECT INBOX...")
        status, messages = imap.select("INBOX")
        print(f"   Status: {status}")
        print(f"   Response: {messages}")
        if status == "OK":
            print("   ✓ SELECT INBOX successful")
        else:
            print("   ✗ SELECT INBOX failed")
            return False
        
        # Test UID FETCH
        print("\n4. Testing UID FETCH...")
        # Get max UID
        status, messages = imap.search(None, "ALL")
        if status == "OK":
            uids = messages[0].split()
            if uids:
                max_uid = uids[-1]
                print(f"   Max UID: {max_uid}")
                
                # Fetch email body
                print(f"\n5. Fetching email body for UID {max_uid}...")
                status, msg_data = imap.fetch(max_uid, "(BODY[])")
                print(f"   Status: {status}")
                if status == "OK":
                    print(f"   ✓ FETCH successful")
                    print(f"   Email size: {len(str(msg_data))} bytes")
                else:
                    print(f"   ✗ FETCH failed")
                    print(f"   Response: {msg_data}")
            else:
                print("   No emails found")
        else:
            print(f"   ✗ SEARCH failed: {messages}")
        
        # Test SELECT Sent with different paths
        print("\n6. Testing SELECT Sent folder with different paths...")
        paths_to_try = ["Sent Messages", "\"Sent Messages\"", "Sent", "&XfNW9GN2-"]
        for path in paths_to_try:
            print(f"   Trying path: '{path}'")
            try:
                status, messages = imap.select(path)
                print(f"     Status: {status}")
                print(f"     Response: {messages}")
                if status == "OK":
                    print(f"     ✓ SELECT Sent successful with path: '{path}'")
                    break
                else:
                    print(f"     ✗ SELECT Sent failed with path: '{path}'")
            except Exception as e:
                print(f"     ✗ SELECT Sent exception with path '{path}': {e}")
        
        # List all folders
        print("\n7. Listing all folders...")
        status, folders = imap.list()
        if status == "OK":
            print("   Available folders:")
            for folder in folders:
                print(f"     {folder.decode() if isinstance(folder, bytes) else folder}")
        
        # Close connection
        print("\n8. Closing connection...")
        try:
            imap.close()
        except:
            pass  # May not be in SELECTED state
        imap.logout()
        print("   ✓ Connection closed")
        
        return True
        
    except Exception as e:
        print(f"\n✗ Error: {e}")
        import traceback
        traceback.print_exc()
        return False

if __name__ == "__main__":
    success = test_163_imap()
    sys.exit(0 if success else 1)
