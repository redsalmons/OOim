cmake_minimum_required(VERSION 3.10)
project(EmailCore CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build the Rust MLS FFI crate (staticlib) before linking.
# Uses the system proxy via CARGO_HTTP_PROXY if set in the environment.
set(MLS_FFI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/mls_ffi")
set(MLS_FFI_LIB "${MLS_FFI_DIR}/target/release/libmls_ffi.a")
if(DEFINED ENV{CARGO_HTTP_PROXY})
    set(_cargo_proxy "CARGO_HTTP_PROXY=$ENV{CARGO_HTTP_PROXY}")
else()
    set(_cargo_proxy "")
endif()
add_custom_command(
    OUTPUT ${MLS_FFI_LIB}
    COMMAND ${_cargo_proxy} cargo build --release --manifest-path ${MLS_FFI_DIR}/Cargo.toml
    WORKING_DIRECTORY ${MLS_FFI_DIR}
    COMMENT "Building Rust MLS FFI static library"
    VERBATIM
)
add_custom_target(mls_ffi_build DEPENDS ${MLS_FFI_LIB})

# Find SQLite3
find_package(SQLite3 REQUIRED)

# Find OpenSSL
find_package(OpenSSL REQUIRED)

# Find libcurl
find_package(CURL REQUIRED)

# Use static OpenSSL libraries
set(OPENSSL_SSL_LIBRARY "/opt/homebrew/opt/openssl@3/lib/libssl.a")
set(OPENSSL_CRYPTO_LIBRARY "/opt/homebrew/opt/openssl@3/lib/libcrypto.a")

# Use locally built vmime with OpenSSL TLS backend
set(VMIME_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/deps/vmime-install/include")
set(VMIME_LIBRARIES "${CMAKE_CURRENT_SOURCE_DIR}/deps/vmime-install/lib/libvmime.dylib")
set(VMIME_FOUND TRUE)

# Find gsasl
find_package(PkgConfig)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(GSASL libgsasl)
endif()

# Fallback to Homebrew path if pkg-config fails
if(NOT GSASL_FOUND)
    set(GSASL_INCLUDE_DIRS "/opt/homebrew/include")
    set(GSASL_LIBRARIES "/opt/homebrew/lib/libgsasl.dylib")
    set(GSASL_FOUND TRUE)
endif()

# Build libemail
add_library(EmailComm STATIC
    libemail/email_handler.cpp
    libemail/email_handler_c.cpp
    libemail/email_opt_163_impl.cpp
    libemail/email_opt_outlook_impl.cpp
    libemail/email_opt_gmail_impl.cpp
    libemail/config_loader.cpp
    libemail/email_config_provider.cpp
    libemail/compress_zlib.cpp
    libemail/crypto_aes.cpp
    libemail/crypto_utils.cpp
)

target_include_directories(EmailComm PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/libemail
    ${CMAKE_CURRENT_SOURCE_DIR}/persistence
    ${CMAKE_CURRENT_SOURCE_DIR}/deps/nlohmann_json-src/single_include
    ${CMAKE_CURRENT_SOURCE_DIR}/deps/cppasyncworker-src
    ${VMIME_INCLUDE_DIRS}
    ${GSASL_INCLUDE_DIRS}
    ${OPENSSL_INCLUDE_DIR}
)

target_link_libraries(EmailComm ${VMIME_LIBRARIES} /opt/homebrew/lib/libgsasl.dylib ${OPENSSL_SSL_LIBRARY} ${OPENSSL_CRYPTO_LIBRARY} CURL::libcurl)

# Email core library (static, used by example executable)
add_library(email_core STATIC
    email_core_utils.cpp
    email_handler.cpp
    email_db.cpp
    email_session.cpp
    email_imap.cpp
    email_oemailim.cpp
    email_contact.cpp
    email_addressbook.cpp
    email_file_transfer.cpp
    file_chunk_util.cpp
    signal/signal_protocol.cpp
    signal/signal_message.cpp
    signal/signal_c_api.cpp
    signal/mls_group.cpp
    signal/group_c_api.cpp
    email_core.h
    email_core_common.h
    persistence/db_connection.cpp
    persistence/email_repo.cpp
    persistence/session_repo.cpp
    persistence/key_repo.cpp
    persistence/contact_repo.cpp
    persistence/addressbook_repo.cpp
    persistence/task_repo.cpp
    persistence/file_transfer_repo.cpp
    persistence/signal_key_repo.cpp
    persistence/signal_session_repo.cpp
    persistence/mls_repo.cpp
    persistence/group_session_repo.cpp
    unified/unified_session_repo.cpp
    unified/unified_session_manager.cpp
    unified/unified_session_c_api.cpp
)

# Email core shared library (used by Flutter via dart:ffi)
add_library(email_core_shared SHARED
    email_core_utils.cpp
    email_handler.cpp
    email_db.cpp
    email_session.cpp
    email_imap.cpp
    email_oemailim.cpp
    email_contact.cpp
    email_addressbook.cpp
    email_file_transfer.cpp
    file_chunk_util.cpp
    signal/signal_protocol.cpp
    signal/signal_message.cpp
    signal/signal_c_api.cpp
    signal/mls_group.cpp
    signal/group_c_api.cpp
    email_core.h
    email_core_common.h
    persistence/db_connection.cpp
    persistence/email_repo.cpp
    persistence/session_repo.cpp
    persistence/key_repo.cpp
    persistence/contact_repo.cpp
    persistence/addressbook_repo.cpp
    persistence/task_repo.cpp
    persistence/file_transfer_repo.cpp
    persistence/signal_key_repo.cpp
    persistence/signal_session_repo.cpp
    persistence/mls_repo.cpp
    persistence/group_session_repo.cpp
    unified/unified_session_repo.cpp
    unified/unified_session_manager.cpp
    unified/unified_session_c_api.cpp
)
set_target_properties(email_core_shared PROPERTIES
    OUTPUT_NAME email_core
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/build
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/build
)

# Include directories
target_include_directories(email_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/persistence
    ${CMAKE_CURRENT_SOURCE_DIR}/signal
    ${CMAKE_CURRENT_SOURCE_DIR}/unified
    ${CMAKE_CURRENT_SOURCE_DIR}/deps/nlohmann_json-src/single_include
    ${VMIME_INCLUDE_DIRS}
    ${GSASL_INCLUDE_DIRS}
    ${OPENSSL_INCLUDE_DIR}
)
target_include_directories(email_core_shared PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/persistence
    ${CMAKE_CURRENT_SOURCE_DIR}/signal
    ${CMAKE_CURRENT_SOURCE_DIR}/unified
    ${CMAKE_CURRENT_SOURCE_DIR}/deps/nlohmann_json-src/single_include
    ${VMIME_INCLUDE_DIRS}
    ${GSASL_INCLUDE_DIRS}
    ${OPENSSL_INCLUDE_DIR}
)

# Link SQLite3, EmailComm, vmime, and the Rust MLS static library
target_link_libraries(email_core SQLite3::SQLite3 EmailComm ${VMIME_LIBRARIES} /opt/homebrew/lib/libgsasl.dylib iconv)
target_link_libraries(email_core_shared SQLite3::SQLite3 EmailComm ${VMIME_LIBRARIES} /opt/homebrew/lib/libgsasl.dylib z iconv)

# Link the Rust MLS static library into the shared lib (Flutter-facing).
# macOS system libs that the Rust staticlib depends on are pulled in here.
add_dependencies(email_core_shared mls_ffi_build)
target_link_libraries(email_core_shared
    ${MLS_FFI_LIB}
    "-framework Security" "-framework Foundation" "-framework CoreFoundation"
    -lpthread -ldl -lm
)

# Sign the shared library on macOS to prevent code signing crashes
if(APPLE)
    add_custom_command(TARGET email_core_shared POST_BUILD
        COMMAND codesign --force --sign - $<TARGET_FILE:email_core_shared>
        COMMENT "Ad-hoc signing libemail_core.dylib"
    )
endif()
