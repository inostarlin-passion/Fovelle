#!/usr/bin/env bash

set -euo pipefail

APP_NAME="Fovelle"
APP_BUNDLE="${APP_NAME}.app"
NIGHTLY_VERSION="${1:-}"

if [[ -n "$NIGHTLY_VERSION" ]]; then
    VERSION="$NIGHTLY_VERSION"
else
    VERSION=$(sed -n 's/^CMAKE_PROJECT_VERSION:STATIC=//p' ../build/CMakeCache.txt | head -n 1)
    if [[ -z "$VERSION" ]]; then
        echo "Unable to determine the project version from build/CMakeCache.txt" >&2
        exit 1
    fi
fi

cd bin

echo "Running macdeployqt"
macdeployqt "$APP_BUNDLE"

IMF_DIR="$APP_BUNDLE/Contents/PlugIns/imageformats"
if [[ (-f "$IMF_DIR/kimg_heif.dylib" || -f "$IMF_DIR/kimg_heif.so") && -f "$IMF_DIR/libqmacheif.dylib" ]]; then
    # Prefer kimageformats HEIF plugin for proper color space handling.
    echo "Removing duplicate HEIF plugin"
    rm "$IMF_DIR/libqmacheif.dylib"
fi
if [[ (-f "$IMF_DIR/kimg_tga.dylib" || -f "$IMF_DIR/kimg_tga.so") && -f "$IMF_DIR/libqtga.dylib" ]]; then
    # Prefer kimageformats TGA plugin which supports more formats.
    echo "Removing duplicate TGA plugin"
    rm "$IMF_DIR/libqtga.dylib"
fi

echo "Running codesign"
if [[ "${APPLE_NOTARIZE_REQUESTED:-false}" == "true" ]]; then
    APP_IDENTIFIER=$(/usr/libexec/PlistBuddy -c "Print :CFBundleIdentifier" "$APP_BUNDLE/Contents/Info.plist")
    codesign --sign "${CODESIGN_CERT_NAME:--}" --deep --force --options runtime --timestamp "$APP_BUNDLE"
else
    codesign --sign "${CODESIGN_CERT_NAME:--}" --deep --force "$APP_BUNDLE"
fi

echo "Creating disk image"
if [[ -n "$NIGHTLY_VERSION" ]]; then
    BUILD_NAME="${APP_NAME}-nightly-${VERSION}"
    DMG_FILENAME="${BUILD_NAME}.dmg"
    mv "$APP_BUNDLE" "${BUILD_NAME}.app"
    hdiutil create -volname "$BUILD_NAME" -srcfolder "${BUILD_NAME}.app" -fs HFS+ "$DMG_FILENAME"
else
    DMG_FILENAME="${APP_NAME}-${VERSION}.dmg"
    brew install create-dmg
    create-dmg --volname "$APP_NAME $VERSION" --window-size 660 400 --icon-size 160 \
        --icon "$APP_BUNDLE" 180 170 --hide-extension "$APP_BUNDLE" \
        --app-drop-link 480 170 "$DMG_FILENAME" "$APP_BUNDLE"
fi

if [[ "${APPLE_NOTARIZE_REQUESTED:-false}" == "true" ]]; then
    codesign --sign "${CODESIGN_CERT_NAME:--}" --timestamp --identifier "${APP_IDENTIFIER}.dmg" "$DMG_FILENAME"
    xcrun notarytool submit "$DMG_FILENAME" --apple-id "$APPLE_ID_USER" --password "$APPLE_ID_PASS" \
        --team-id "${CODESIGN_CERT_NAME: -11:10}" --wait
    xcrun stapler staple "$DMG_FILENAME"
    xcrun stapler validate "$DMG_FILENAME"
fi

rm -rf -- *.app
