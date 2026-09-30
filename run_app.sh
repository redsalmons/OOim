#!/bin/bash

# Kill existing app and lingering processes first
echo "Killing existing app and processes..."
pkill -9 -f OceanTalk.app 2>/dev/null
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
APP_PATH="build/macos/Build/Products/Debug/OceanTalk.app"
FRAMEWORKS_DIR="$APP_PATH/Contents/Frameworks"

# Remove all previously copied dylibs
echo "Cleaning old libraries..."
find "$FRAMEWORKS_DIR" -name "*.dylib" -maxdepth 1 -delete

# Copy libemail_core.dylib
echo "Copying libemail_core.dylib..."
cp /Users/steven/Cascade/OIM/email/build/libemail_core.dylib "$FRAMEWORKS_DIR/"

# Copy native dependencies (universal arm64+x86_64)
echo "Copying native dependencies..."
UNIV_LIBS="/Users/steven/Cascade/OIM/email/deps/univ-libs/lib"
for lib in libgnutls.30 libintl.8 libp11-kit.0 libidn2.0 libunistring.5 libtasn1.6 libhogweed.7 libnettle.9 libhogweed.6 libnettle.8 libgmp.10 libgsasl.18; do
    src="$UNIV_LIBS/$lib.dylib"
    if [ -f "$src" ] && [ ! -f "$FRAMEWORKS_DIR/$lib.dylib" ]; then
        cp "$src" "$FRAMEWORKS_DIR/$lib.dylib"
        echo "  Copied $lib.dylib"
    fi
done

# Also copy libvmime (universal build)
if [ ! -f "$FRAMEWORKS_DIR/libvmime.1.dylib" ]; then
    cp /Users/steven/Cascade/OIM/email/deps/vmime-univ-install/lib/libvmime.1.dylib "$FRAMEWORKS_DIR/"
    echo "  Copied libvmime.1.dylib"
fi

# Fix library paths (multiple passes to handle nested dependencies)
echo "Fixing library paths..."
for pass in 1 2 3; do
    for f in "$FRAMEWORKS_DIR"/*.dylib; do
        base=$(basename "$f")
        chmod u+w "$f"
        install_name_tool -id @rpath/$base "$f" 2>/dev/null
        for dep in $(otool -L "$f" | grep -i homebrew | awk '{print $1}'); do
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
    codesign --force --sign "OceanTalk Dev" "$f" 2>&1
done

# Re-sign the app bundle with sandbox entitlements so it uses the container path
echo "Re-signing app bundle..."
codesign --force --deep --sign "OceanTalk Dev" --entitlements /Users/steven/Cascade/OIM/macos/Runner/DebugProfile.entitlements "$APP_PATH" 2>&1

# Launch app
echo "Launching app..."
open "$APP_PATH"
echo "App launched. Check sandbox logs at: ~/Library/Containers/com.redsalmon.oim/Data/Library/Application Support/com.redsalmon.oim/log/oim.log"
