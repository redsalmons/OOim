cmake_minimum_required(VERSION 3.10)
project(EmailCore CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build the Rust MLS FFI crate (staticlib) before linking.
# Uses the system proxy via CARGO_HTTP_PROXY if set in the environment.
set(MLS_FFI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/mls_ffi")
if(WIN32)
    # Rust target follows the vcpkg triplet: x64-windows -> x86_64, arm64-windows -> aarch64
    if(DEFINED VCPKG_TARGET_TRIPLET AND VCPKG_TARGET_TRIPLET MATCHES "^x64")
        set(MLS_RUST_TARGET "x86_64-pc-windows-msvc")
    else()
        set(MLS_RUST_TARGET "aarch64-pc-windows-msvc")
    endif()
    set(MLS_FFI_LIB "${MLS_FFI_DIR}/target/${MLS_RUST_TARGET}/release/mls_ffi.lib")
    set(MLS_CARGO_TARGET_ARGS --target ${MLS_RUST_TARGET})
else()
    set(MLS_FFI_LIB "${MLS_FFI_DIR}/target/release/libmls_ffi.a")
    set(MLS_CARGO_TARGET_ARGS "")
endif()
if(DEFINED ENV{CARGO_HTTP_PROXY})
    set(_cargo_proxy "CARGO_HTTP_PROXY=$ENV{CARGO_HTTP_PROXY}")
else()
    set(_cargo_proxy "")
endif()
if(APPLE)
    add_custom_command(
        OUTPUT ${MLS_FFI_LIB}
        COMMAND ${_cargo_proxy} cargo build --release --target aarch64-apple-darwin --manifest-path ${MLS_FFI_DIR}/Cargo.toml
        COMMAND ${_cargo_proxy} cargo build --release --target x86_64-apple-darwin --manifest-path ${MLS_FFI_DIR}/Cargo.toml
        COMMAND lipo -create ${MLS_FFI_DIR}/target/aarch64-apple-darwin/release/libmls_ffi.a ${MLS_FFI_DIR}/target/x86_64-apple-darwin/release/libmls_ffi.a -output ${MLS_FFI_LIB}
        WORKING_DIRECTORY ${MLS_FFI_DIR}
        COMMENT "Building Rust MLS FFI static library (universal)"
        VERBATIM
    )
else()
    add_custom_command(
        OUTPUT ${MLS_FFI_LIB}
        COMMAND ${_cargo_proxy} cargo build --release ${MLS_CARGO_TARGET_ARGS} --manifest-path ${MLS_FFI_DIR}/Cargo.toml
        WORKING_DIRECTORY ${MLS_FFI_DIR}
        COMMENT "Building Rust MLS FFI static library"
        VERBATIM
    )
endif()
add_custom_target(mls_ffi_build DEPENDS ${MLS_FFI_LIB})

# ---------------------------------------------------------------------------
# Platform-specific dependency resolution
# ---------------------------------------------------------------------------
if(WIN32)
    # Windows: use vcpkg toolchain (pass -DCMAKE_TOOLCHAIN_FILE=.../vcpkg.cmake)
    find_package(SQLite3 REQUIRED)
    find_package(OpenSSL REQUIRED)
    find_package(CURL REQUIRED)
    find_package(ZLIB REQUIRED)
    find_package(nlohmann_json REQUIRED)

    # vmime - manually built for Windows ARM64.
    # Default location on this machine; override via VMIME_ROOT env var.
    if(DEFINED ENV{VMIME_ROOT})
        set(_vmime_root "$ENV{VMIME_ROOT}")
    else()
        set(_vmime_root "E:/vmime-install")
    endif()
    set(VMIME_INCLUDE_DIRS "${_vmime_root}/include")
    set(VMIME_LIBRARIES "${_vmime_root}/lib/vmime.lib")
    set(VMIME_FOUND TRUE)

    # gsasl - installed via vcpkg
    find_path(GSASL_INCLUDE_DIR gsasl.h REQUIRED)
    find_library(GSASL_LIBRARY gsasl REQUIRED)
    set(GSASL_INCLUDE_DIRS "${GSASL_INCLUDE_DIR}")
    set(GSASL_LIBRARIES "${GSASL_LIBRARY}")
    set(GSASL_FOUND TRUE)
else()
    # macOS: Find SQLite3, libcurl via system; native deps are universal
    # (arm64 + x86_64) fat libraries staged under deps/univ-libs.
    find_package(SQLite3 REQUIRED)
    find_package(CURL REQUIRED)

    set(_univ_libs "${CMAKE_CURRENT_SOURCE_DIR}/deps/univ-libs")

    # Static OpenSSL fat archives (headers from the older slice's version for ABI safety)
    set(OPENSSL_SSL_LIBRARY "${_univ_libs}/lib/libssl.a")
    set(OPENSSL_CRYPTO_LIBRARY "${_univ_libs}/lib/libcrypto.a")
    set(OPENSSL_INCLUDE_DIR "${_univ_libs}/include")

    # Universal vmime (GnuTLS TLS backend + gsasl)
    set(VMIME_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/deps/vmime-univ-install/include")
    set(VMIME_LIBRARIES "${CMAKE_CURRENT_SOURCE_DIR}/deps/vmime-univ-install/lib/libvmime.dylib")
    set(VMIME_FOUND TRUE)

    # Universal gsasl
    set(GSASL_INCLUDE_DIRS "${_univ_libs}/include")
    set(GSASL_LIBRARIES "${_univ_libs}/lib/libgsasl.dylib")
    set(GSASL_FOUND TRUE)
endif()

# cppasyncworker - header-only worker pool used by libemail.
# Default location on this machine; override via CPPASYNCWORKER_SRC env var.
if(DEFINED ENV{CPPASYNCWORKER_SRC})
    set(CPPASYNCWORKER_SRC_DIR "$ENV{CPPASYNCWORKER_SRC}")
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/deps/cppasyncworker-src")
    set(CPPASYNCWORKER_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/deps/cppasyncworker-src")
else()
    set(CPPASYNCWORKER_SRC_DIR "C:/cppasyncworker-src")
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
    libemail/message_id.cpp
)

target_include_directories(EmailComm PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/libemail
    ${CMAKE_CURRENT_SOURCE_DIR}/persistence
    ${CPPASYNCWORKER_SRC_DIR}
    ${VMIME_INCLUDE_DIRS}
    ${GSASL_INCLUDE_DIRS}
    ${OPENSSL_INCLUDE_DIR}
)
if(WIN32)
    target_compile_definitions(EmailComm PUBLIC
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        VMIME_STATIC
        _WIN32_WINNT=0x0A00
        _CRT_SECURE_NO_WARNINGS
    )
    target_compile_options(EmailComm PUBLIC /utf-8)
endif()
if(WIN32)
    target_link_libraries(EmailComm PUBLIC nlohmann_json::nlohmann_json)
else()
    target_include_directories(EmailComm PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/deps/nlohmann_json-src/single_include
    )
endif()

if(WIN32)
    target_link_libraries(EmailComm PUBLIC ${VMIME_LIBRARIES} ${GSASL_LIBRARIES}
        OpenSSL::SSL OpenSSL::Crypto CURL::libcurl)
else()
    target_link_libraries(EmailComm PUBLIC ${VMIME_LIBRARIES} ${GSASL_LIBRARIES} ${OPENSSL_SSL_LIBRARY} ${OPENSSL_CRYPTO_LIBRARY} CURL::libcurl)
endif()

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
    persistence/keychain_store.cpp
    persistence/email_repo.cpp
    persistence/email_body_repo.cpp
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
    persistence/keychain_store.cpp
    persistence/email_repo.cpp
    persistence/email_body_repo.cpp
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
    ARCHIVE_OUTPUT_NAME email_core_shared
    # Output into the configured build dir so arm64/x64 builds don't overwrite each other
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
    WINDOWS_EXPORT_ALL_SYMBOLS ON
)

# SQLCipher amalgamation provides sqlite3 — compiled directly into each target
# so the whole emails.db is encrypted at rest (CommonCrypto provider, no extra
# dylib, builds for every CMAKE_OSX_ARCHITECTURES slice automatically).
target_sources(email_core PRIVATE deps/sqlcipher-amalg/sqlite3.c)
target_sources(email_core_shared PRIVATE deps/sqlcipher-amalg/sqlite3.c)
set_source_files_properties(deps/sqlcipher-amalg/sqlite3.c PROPERTIES
    COMPILE_DEFINITIONS "SQLITE_HAS_CODEC;SQLCIPHER_CRYPTO_CC;SQLITE_TEMP_STORE=2;SQLITE_THREADSAFE=1;SQLITE_EXTRA_INIT=sqlcipher_extra_init;SQLITE_EXTRA_SHUTDOWN=sqlcipher_extra_shutdown")

# Include directories
target_include_directories(email_core BEFORE PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/deps/sqlcipher-amalg
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/persistence
    ${CMAKE_CURRENT_SOURCE_DIR}/signal
    ${CMAKE_CURRENT_SOURCE_DIR}/unified
    ${VMIME_INCLUDE_DIRS}
    ${GSASL_INCLUDE_DIRS}
    ${OPENSSL_INCLUDE_DIR}
)
target_include_directories(email_core_shared BEFORE PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/deps/sqlcipher-amalg
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/persistence
    ${CMAKE_CURRENT_SOURCE_DIR}/signal
    ${CMAKE_CURRENT_SOURCE_DIR}/unified
    ${VMIME_INCLUDE_DIRS}
    ${GSASL_INCLUDE_DIRS}
    ${OPENSSL_INCLUDE_DIR}
)
if(WIN32)
    target_link_libraries(email_core PUBLIC nlohmann_json::nlohmann_json)
    target_link_libraries(email_core_shared PUBLIC nlohmann_json::nlohmann_json)
else()
    target_include_directories(email_core PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/deps/nlohmann_json-src/single_include
    )
    target_include_directories(email_core_shared PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/deps/nlohmann_json-src/single_include
    )
endif()

# Link SQLite3, EmailComm, vmime, and the Rust MLS static library
if(WIN32)
    find_package(Iconv REQUIRED)
    target_link_libraries(email_core PUBLIC SQLite3::SQLite3 EmailComm ${VMIME_LIBRARIES} ${GSASL_LIBRARIES} Iconv::Iconv)
    target_link_libraries(email_core_shared PUBLIC SQLite3::SQLite3 EmailComm ${VMIME_LIBRARIES} ${GSASL_LIBRARIES} ZLIB::ZLIB Iconv::Iconv)
else()
    target_link_libraries(email_core PUBLIC EmailComm ${VMIME_LIBRARIES} ${GSASL_LIBRARIES} iconv)
    target_link_libraries(email_core_shared PUBLIC EmailComm ${VMIME_LIBRARIES} ${GSASL_LIBRARIES} z iconv)
endif()

# Link the Rust MLS static library into the shared lib (Flutter-facing).
add_dependencies(email_core_shared mls_ffi_build)
if(WIN32)
    # Windows system libs that the Rust staticlib depends on
    target_link_libraries(email_core_shared PUBLIC
        ${MLS_FFI_LIB}
        ws2_32 crypt32 ncrypt userenv secur32
        bcrypt advapi32 ntdll
    )
else()
    # macOS system libs that the Rust staticlib depends on
    target_link_libraries(email_core_shared PUBLIC
        ${MLS_FFI_LIB}
        "-framework Security" "-framework Foundation" "-framework CoreFoundation"
        -lpthread -ldl -lm
    )
endif()

# Sign the shared library on macOS to prevent code signing crashes
if(APPLE)
    add_custom_command(TARGET email_core_shared POST_BUILD
        COMMAND codesign --force --sign - $<TARGET_FILE:email_core_shared>
        COMMENT "Ad-hoc signing libemail_core.dylib"
    )
endif()
