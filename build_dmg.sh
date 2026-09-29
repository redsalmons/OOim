#!/bin/bash
set -e

# Build email_core library
echo "Building email_core library..."
cd /Users/steven/Cascade/OIM/email
cmake --build build -j8

# Build Flutter app (release)
echo "Building Flutter app (release)..."
cd /Users/steven/Cascade/OIM
flutter build macos --release

# Copy dependent libraries
echo "Copying dependent libraries..."
APP_PATH="build/macos/Build/Products/Release/oceantalk.app"
FRAMEWORKS_DIR="$APP_PATH/Contents/Frameworks"

echo "Cleaning old libraries..."
find "$FRAMEWORKS_DIR" -name "*.dylib" -maxdepth 1 -delete

echo "Copying libemail_core.dylib..."
cp /Users/steven/Cascade/OIM/email/build/libemail_core.dylib "$FRAMEWORKS_DIR/"

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
    codesign --force --sign - "$f"
done

# Re-sign the app bundle with release entitlements (sandbox)
echo "Re-signing app bundle..."
codesign --force --deep --sign - --entitlements /Users/steven/Cascade/OIM/macos/Runner/Release.entitlements "$APP_PATH"

# Stage DMG contents: the app plus an Applications symlink for drag-install
echo "Staging DMG contents..."
DMG_STAGE="build/dmg_stage"
DMG_PATH="build/OceanTalk.dmg"
rm -rf "$DMG_STAGE"
mkdir -p "$DMG_STAGE"
cp -R "$APP_PATH" "$DMG_STAGE/"
ln -s /Applications "$DMG_STAGE/Applications"

# Create compressed DMG
echo "Creating DMG..."
rm -f "$DMG_PATH"
hdiutil create -volname "零海 OceanTalk" -srcfolder "$DMG_STAGE" -ov -format UDZO "$DMG_PATH"
rm -rf "$DMG_STAGE"

echo "Done: $DMG_PATH"
hdiutil verify "$DMG_PATH" >/dev/null && echo "DMG verified."
ls -lh "$DMG_PATH"
