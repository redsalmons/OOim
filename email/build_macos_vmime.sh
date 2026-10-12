#!/bin/bash
# Build vmime for macOS as a universal (arm64 + x86_64) static library with the
# OpenSSL TLS backend, against the fat OpenSSL/gsasl in deps/univ-libs.
# Output: email/deps/vmime-univ-ossl-install/{include,lib/libvmime.a}
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
DEPS="$HERE/deps"
UNIV="$DEPS/univ-libs"
BUILD="$DEPS/vmime-univ-ossl-build"
PREFIX="$DEPS/vmime-univ-ossl-install"

rm -rf "$BUILD" "$PREFIX"
cmake -S "$DEPS/vmime-src" -B "$BUILD" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    -DVMIME_BUILD_STATIC_LIBRARY=ON \
    -DVMIME_BUILD_SHARED_LIBRARY=OFF \
    -DVMIME_BUILD_SAMPLES=OFF \
    -DVMIME_BUILD_TESTS=OFF \
    -DVMIME_BUILD_DOCUMENTATION=OFF \
    -DVMIME_HAVE_MESSAGING_PROTO_SENDMAIL=OFF \
    -DVMIME_TLS_SUPPORT_LIB=openssl \
    -DOPENSSL_INCLUDE_DIR="$UNIV/include" \
    -DOPENSSL_SSL_LIBRARY="$UNIV/lib/libssl.a" \
    -DOPENSSL_CRYPTO_LIBRARY="$UNIV/lib/libcrypto.a" \
    -DGSASL_INCLUDE_DIR="$UNIV/include" \
    -DGSASL_LIBRARIES="$UNIV/lib/libgsasl.dylib" \
    -DVMIME_CHARSETCONV_LIB=iconv
cmake --build "$BUILD" -j"$(sysctl -n hw.ncpu)"
cmake --install "$BUILD"
rm -rf "$BUILD"
lipo -info "$PREFIX/lib/libvmime.a"
grep -E "TLS_SUPPORT_LIB_IS_(OPENSSL|GNUTLS)" "$PREFIX/include/vmime/config.hpp"
