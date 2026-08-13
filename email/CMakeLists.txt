cmake_minimum_required(VERSION 3.10)
project(EmailCore CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find SQLite3
find_package(SQLite3 REQUIRED)

# Find OpenSSL
find_package(OpenSSL REQUIRED)

# Find libcurl
find_package(CURL REQUIRED)

# Use static OpenSSL libraries
set(OPENSSL_SSL_LIBRARY "/opt/homebrew/opt/openssl@3/lib/libssl.a")
set(OPENSSL_CRYPTO_LIBRARY "/opt/homebrew/opt/openssl@3/lib/libcrypto.a")

# Use system vmime directly
set(VMIME_INCLUDE_DIRS "/usr/local/include")
set(VMIME_LIBRARIES "/usr/local/lib/libvmime.dylib")
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
    libemail/config_loader.cpp
    libemail/email_config_provider.cpp
    libemail/compress_zlib.cpp
    libemail/crypto_aes.cpp
    libemail/crypto_utils.cpp
)

target_include_directories(EmailComm PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/libemail
    ${CMAKE_CURRENT_SOURCE_DIR}/deps/nlohmann_json-src/single_include
    ${CMAKE_CURRENT_SOURCE_DIR}/deps/cppasyncworker-src
    ${VMIME_INCLUDE_DIRS}
    ${GSASL_INCLUDE_DIRS}
    ${OPENSSL_INCLUDE_DIR}
)

target_link_libraries(EmailComm /usr/local/lib/libvmime.dylib /opt/homebrew/lib/libgsasl.dylib ${OPENSSL_SSL_LIBRARY} ${OPENSSL_CRYPTO_LIBRARY} CURL::libcurl)

# Email core library (static, used by example executable)
add_library(email_core STATIC
    email_core.cpp
    email_core.h
)

# Email core shared library (used by Flutter via dart:ffi)
add_library(email_core_shared SHARED
    email_core.cpp
    email_core.h
)
set_target_properties(email_core_shared PROPERTIES
    OUTPUT_NAME email_core
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/build
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/build
)

# Include directories
target_include_directories(email_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/deps/nlohmann_json-src/single_include
    ${VMIME_INCLUDE_DIRS}
    ${GSASL_INCLUDE_DIRS}
    ${OPENSSL_INCLUDE_DIR}
)
target_include_directories(email_core_shared PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/deps/nlohmann_json-src/single_include
    ${VMIME_INCLUDE_DIRS}
    ${GSASL_INCLUDE_DIRS}
    ${OPENSSL_INCLUDE_DIR}
)

# Link SQLite3, EmailComm, and vmime
target_link_libraries(email_core SQLite3::SQLite3 EmailComm /usr/local/lib/libvmime.dylib /opt/homebrew/lib/libgsasl.dylib)
target_link_libraries(email_core_shared SQLite3::SQLite3 EmailComm /usr/local/lib/libvmime.dylib /opt/homebrew/lib/libgsasl.dylib)
