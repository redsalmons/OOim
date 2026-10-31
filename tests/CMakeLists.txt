cmake_minimum_required(VERSION 3.10)
project(test_outlook_tls CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# vmime (built locally with OpenSSL TLS backend)
set(VMIME_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/../email/deps/vmime-install/include")
set(VMIME_LIBRARIES "${CMAKE_CURRENT_SOURCE_DIR}/../email/deps/vmime-install/lib/libvmime.dylib")

# gsasl (vmime SASL dependency)
set(GSASL_INCLUDE_DIRS "/opt/homebrew/include")
set(GSASL_LIBRARIES "/opt/homebrew/lib/libgsasl.dylib")

add_executable(test_outlook_tls test_outlook_tls.cpp)

target_include_directories(test_outlook_tls PRIVATE
    ${VMIME_INCLUDE_DIRS}
    ${GSASL_INCLUDE_DIRS}
)

target_link_libraries(test_outlook_tls ${VMIME_LIBRARIES} ${GSASL_LIBRARIES})

# macOS ad-hoc signing
if(APPLE)
    add_custom_command(TARGET test_outlook_tls POST_BUILD
        COMMAND codesign --force --sign - $<TARGET_FILE:test_outlook_tls>
        COMMENT "Ad-hoc signing test_outlook_tls"
    )
endif()
