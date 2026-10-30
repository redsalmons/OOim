#!/bin/bash
# Post-build (Run Script via Runner.xcscheme BuildAction post-action):
# bundle the full native dylib closure into the app and rewrite all
# homebrew/usr-local references to @rpath so the sandboxed app can dlopen.
# Kept as a separate script because `pod install` rewrites project.pbxproj.
set -e

SRC_ROOT="${SRCROOT:-$(cd "$(dirname "$0")" && pwd)}"
EMAIL_DIR="$SRC_ROOT/../email"
DEST_DIR="${BUILT_PRODUCTS_DIR}/${EXECUTABLE_FOLDER_PATH}"
FRAMEWORKS_DIR="$DEST_DIR/../Frameworks"
mkdir -p "$FRAMEWORKS_DIR"

UNIV_LIBS="$EMAIL_DIR/deps/univ-libs/lib"
for f in "$UNIV_LIBS"/lib*.*.dylib; do
  [ -f "$f" ] && cp "$f" "$FRAMEWORKS_DIR/" 2>/dev/null || true
done
cp "$EMAIL_DIR/deps/vmime-univ-install/lib/libvmime.1.dylib" "$FRAMEWORKS_DIR/" 2>/dev/null || true

# Multiple passes to handle nested dependencies and @@HOMEBREW_PREFIX@@/
# @@HOMEBREW_CELLAR@@ placeholders inside the universal bottles.
for pass in 1 2 3; do
  for f in "$FRAMEWORKS_DIR"/*.dylib "$DEST_DIR/libemail_core.dylib"; do
    base=$(basename "$f")
    chmod u+w "$f" 2>/dev/null || true
    install_name_tool -id @rpath/$base "$f" 2>/dev/null || true
    for dep in $(otool -L "$f" | grep -iE 'homebrew|/usr/local/(lib|opt)/' | awk '{print $1}'); do
      depbase=$(basename "$dep")
      if [ -f "$FRAMEWORKS_DIR/$depbase" ]; then
        install_name_tool -change "$dep" @rpath/$depbase "$f" 2>/dev/null || true
      fi
    done
  done
done

# Sign with the project's stable local identity — ad-hoc signing changes the
# cdhash every build, which breaks Keychain ACL trust for the DB master key
# and leaves SecItemCopyMatching waiting on a prompt that gets suppressed.
SIGN_ID="OceanTalk Dev"
security find-identity -v -p codesigning 2>/dev/null | grep -q "$SIGN_ID" || SIGN_ID="${EXPANDED_CODE_SIGN_IDENTITY:--}"
for f in "$FRAMEWORKS_DIR"/*.dylib "$DEST_DIR/libemail_core.dylib"; do
  [ -f "$f" ] && codesign --force --sign "$SIGN_ID" "$f" 2>/dev/null || true
done

# The dylib fixups invalidated the app seal — re-sign the whole bundle.
APP_DIR="${BUILT_PRODUCTS_DIR}/${FULL_PRODUCT_NAME}"
ENT="$SRC_ROOT/Runner/${CONFIGURATION:-Release}.entitlements"
[ "$CONFIGURATION" = "Debug" ] || [ "$CONFIGURATION" = "Profile" ] && ENT="$SRC_ROOT/Runner/DebugProfile.entitlements"
if [ -d "$APP_DIR" ]; then
  if [ -f "$ENT" ]; then
    codesign --force --deep --sign "$SIGN_ID" --entitlements "$ENT" "$APP_DIR" 2>/dev/null || true
  else
    codesign --force --deep --sign "$SIGN_ID" "$APP_DIR" 2>/dev/null || true
  fi
fi
