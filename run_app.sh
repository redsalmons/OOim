#!/bin/bash

# Kill existing app and lingering processes first
echo "Killing existing app and processes..."
pkill -9 -f oim.app 2>/dev/null
pkill -9 -f "flutter" 2>/dev/null
pkill -9 -f "dart" 2>/dev/null
sleep 2
# Remove Flutter startup lock files
rm -f /Users/steven/Cascade/OIM/.dart_tool/hooks_runner/*/.lock 2>/dev/null
rm -f /Users/steven/Cascade/OIM/.dart_tool/hooks_runner/shared/objective_c/.lock 2>/dev/null
rm -f /Users/steven/Cascade/OIM/build/macos/CompilationCache.noindex/generic/lock 2>/dev/null

# Build email_core library
echo "Building email_core library..."
cd /Users/steven/Cascade/OIM/email
cmake --build build -j8
if [ $? -ne 0 ]; then
    echo "Build failed: email_core library"
    exit 1
fi

# Build Flutter app
echo "Building Flutter app..."
cd /Users/steven/Cascade/OIM
flutter build macos --debug
if [ $? -ne 0 ]; then
    echo "Build failed: Flutter app"
    exit 1
fi

# Copy dependent libraries
echo "Copying dependent libraries..."
APP_PATH="build/macos/Build/Products/Debug/oim.app"
FRAMEWORKS_DIR="$APP_PATH/Contents/Frameworks"

# Remove all previously copied dylibs
echo "Cleaning old libraries..."
find "$FRAMEWORKS_DIR" -name "*.dylib" -maxdepth 1 -delete

# Copy libemail_core.dylib
echo "Copying libemail_core.dylib..."
cp /Users/steven/Cascade/OIM/email/build/libemail_core.dylib "$FRAMEWORKS_DIR/"

# Copy homebrew dependencies
echo "Copying homebrew dependencies..."
export PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin:$PATH
for pair in "gnutls:libgnutls.30" "gettext:libintl.8" "p11-kit:libp11-kit.0" "libidn2:libidn2.0" "libunistring:libunistring.5" "libtasn1:libtasn1.6" "nettle:libhogweed.7" "nettle:libnettle.9" "gmp:libgmp.10" "gsasl:libgsasl.18"; do
    opt=$(echo $pair | cut -d: -f1)
    lib=$(echo $pair | cut -d: -f2)
    src="/opt/homebrew/opt/$opt/lib/$lib.dylib"
    if [ -f "$src" ] && [ ! -f "$FRAMEWORKS_DIR/$lib.dylib" ]; then
        cp "$src" "$FRAMEWORKS_DIR/$lib.dylib"
        echo "  Copied $lib.dylib"
    fi
done

# Also copy libvmime
if [ -f "/usr/local/lib/libvmime.1.dylib" ] && [ ! -f "$FRAMEWORKS_DIR/libvmime.1.dylib" ]; then
    cp /usr/local/lib/libvmime.1.dylib "$FRAMEWORKS_DIR/"
    echo "  Copied libvmime.1.dylib"
fi

# Fix library paths (multiple passes to handle nested dependencies)
echo "Fixing library paths..."
for pass in 1 2 3; do
    for f in "$FRAMEWORKS_DIR"/*.dylib; do
        base=$(basename "$f")
        chmod u+w "$f"
        install_name_tool -id @rpath/$base "$f" 2>/dev/null
        for dep in $(otool -L "$f" | grep homebrew | awk '{print $1}'); do
            depbase=$(basename "$dep")
            if [ -f "$FRAMEWORKS_DIR/$depbase" ]; then
                install_name_tool -change "$dep" @rpath/$depbase "$f" 2>/dev/null
            fi
        done
    done
done

# Re-sign all libraries
echo "Re-signing libraries..."
for f in "$FRAMEWORKS_DIR"/*.dylib; do
    codesign --force --sign - "$f" 2>&1
done

# Launch app
echo "Launching app..."
open "$APP_PATH"
echo "App launched. Check logs at: ~/Library/Application Support/com.redsalmon.oim/oim.log"
