#!/bin/bash
# Cross-compile native dependencies for Android with the NDK.
# Output: email/deps/android/<abi>/{include,lib}
# Usage:  ./build_android_deps.sh [abi ...]   (default: arm64-v8a)
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="$HERE/deps/android-src"
OUT_ROOT="$HERE/deps/android"
API=28
OPENSSL_VER=3.5.0
GSASL_VER=2.2.4
LIBICONV_VER=1.17
NDK="${ANDROID_NDK_HOME:-$HOME/Library/Android/sdk/ndk/28.2.13676358}"
TOOLCHAIN="$NDK/toolchains/llvm/prebuilt/darwin-x86_64"
ABIS=("${@:-arm64-v8a}")
JOBS="$(sysctl -n hw.ncpu)"

openssl_target() {
    case "$1" in
        arm64-v8a) echo android-arm64 ;;
        x86_64)    echo android-x86_64 ;;
        *) echo "unsupported ABI: $1" >&2; exit 1 ;;
    esac
}

android_triplet() {
    case "$1" in
        arm64-v8a) echo aarch64-linux-android ;;
        x86_64)    echo x86_64-linux-android ;;
        *) echo "unsupported ABI: $1" >&2; exit 1 ;;
    esac
}

build_openssl() {
    local abi="$1" out="$OUT_ROOT/$1"
    if [ -f "$out/lib/libcrypto.a" ] && [ -f "$out/lib/libssl.a" ]; then
        echo "[openssl] $abi already built"; return
    fi
    local work="$SRC/build-openssl-$abi"
    rm -rf "$work" && mkdir -p "$work"
    tar -xzf "$SRC/openssl-$OPENSSL_VER.tar.gz" -C "$work" --strip-components=1
    (
        cd "$work"
        export ANDROID_NDK_ROOT="$NDK"
        export PATH="$TOOLCHAIN/bin:$PATH"
        ./Configure "$(openssl_target "$abi")" -D__ANDROID_API__=$API \
            no-shared no-tests no-docs no-apps no-module \
            -Wl,-z,max-page-size=16384 \
            --prefix="$out" --libdir=lib
        make -j"$JOBS" build_libs
        make install_dev
    )
    rm -rf "$work"
    echo "[openssl] $abi -> $out"
}

build_gsasl() {
    local abi="$1" out="$OUT_ROOT/$1"
    if [ -f "$out/lib/libgsasl.a" ]; then
        echo "[gsasl] $abi already built"; return
    fi
    local triplet work
    triplet="$(android_triplet "$abi")"
    work="$SRC/build-gsasl-$abi"
    rm -rf "$work" && mkdir -p "$work"
    tar -xzf "$SRC/gsasl-$GSASL_VER.tar.gz" -C "$work" --strip-components=1
    (
        cd "$work"
        export CC="$TOOLCHAIN/bin/${triplet}${API}-clang"
        export AR="$TOOLCHAIN/bin/llvm-ar"
        export RANLIB="$TOOLCHAIN/bin/llvm-ranlib"
        ./configure --host="$triplet" --prefix="$out" \
            --disable-shared --enable-static \
            --disable-nls --without-stringprep \
            --without-libidn-prefix --without-libntlm-prefix \
            --without-libgcrypt --disable-gssapi \
            --disable-saml20 --disable-openid20 --disable-ntlm
        make -j"$JOBS" -C lib
        make -C lib install
        make install-includeHEADERS 2>/dev/null || make -C include install 2>/dev/null || true
    )
    rm -rf "$work"
    echo "[gsasl] $abi -> $out"
}

build_libiconv() {
    local abi="$1" out="$OUT_ROOT/$1"
    if [ -f "$out/lib/libiconv.a" ]; then
        echo "[libiconv] $abi already built"; return
    fi
    local triplet work
    triplet="$(android_triplet "$abi")"
    work="$SRC/build-libiconv-$abi"
    rm -rf "$work" && mkdir -p "$work"
    tar -xzf "$SRC/libiconv-$LIBICONV_VER.tar.gz" -C "$work" --strip-components=1
    (
        cd "$work"
        export CC="$TOOLCHAIN/bin/${triplet}${API}-clang"
        export AR="$TOOLCHAIN/bin/llvm-ar"
        export RANLIB="$TOOLCHAIN/bin/llvm-ranlib"
        ./configure --host="$triplet" --prefix="$out" \
            --enable-static --disable-shared \
            --disable-nls --enable-relocatable=no
        make -j"$JOBS"
        make install
    )
    rm -rf "$work"
    echo "[libiconv] $abi -> $out"
}

build_vmime() {
    local abi="$1" out="$OUT_ROOT/$1"
    if [ -f "$out/lib/libvmime.a" ]; then
        echo "[vmime] $abi already built"; return
    fi
    local work="$SRC/build-vmime-$abi"
    rm -rf "$work"
    cmake -S "$HERE/deps/vmime-src" -B "$work" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
        -DANDROID_ABI="$abi" \
        -DANDROID_PLATFORM="android-$API" \
        -DANDROID_STL=c++_static \
        -DCMAKE_INSTALL_PREFIX="$out" \
        -DVMIME_BUILD_STATIC_LIBRARY=ON \
        -DVMIME_BUILD_SHARED_LIBRARY=OFF \
        -DVMIME_BUILD_SAMPLES=OFF \
        -DVMIME_BUILD_TESTS=OFF \
        -DVMIME_BUILD_DOCUMENTATION=OFF \
        -DVMIME_HAVE_MESSAGING_PROTO_SENDMAIL=OFF \
        -DVMIME_TLS_SUPPORT_LIB=openssl \
        -DOPENSSL_INCLUDE_DIR="$out/include" \
        -DOPENSSL_SSL_LIBRARY="$out/lib/libssl.a" \
        -DOPENSSL_CRYPTO_LIBRARY="$out/lib/libcrypto.a" \
        -DGSASL_INCLUDE_DIR="$out/include" \
        -DGSASL_LIBRARIES="$out/lib/libgsasl.a" \
        -DICONV_INCLUDE_DIR="$out/include" \
        -DICONV_LIBRARIES="$out/lib/libiconv.a" \
        -DVMIME_CHARSETCONV_LIB=iconv
    cmake --build "$work" -j"$JOBS"
    cmake --install "$work"
    rm -rf "$work"
    echo "[vmime] $abi -> $out"
}

[ -d "$TOOLCHAIN" ] || { echo "NDK toolchain not found: $TOOLCHAIN" >&2; exit 1; }
for abi in "${ABIS[@]}"; do
    build_openssl "$abi"
    build_gsasl "$abi"
    build_libiconv "$abi"
    build_vmime "$abi"
done
