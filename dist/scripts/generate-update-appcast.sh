#!/bin/bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SPARKLE_ROOT="${FOVELLE_SPARKLE_ROOT:-$ROOT/build/_deps/sparkle}"
: "${SPARKLE_PRIVATE_ED_KEY:?32-byte base64 signing seed is required}"
: "${RELEASE_TAG:?release tag is required}"
: "${RELEASE_ZIP_PATH:?notarized release archive is required}"
APP_PATH="${RELEASE_APP_PATH:-$ROOT/build/Fovelle.app}"
PUBLIC_KEY=$(/usr/libexec/PlistBuddy -c 'Print :SUPublicEDKey' "$APP_PATH/Contents/Info.plist")
printf '%s' "$SPARKLE_PRIVATE_ED_KEY" | swift "$ROOT/dist/scripts/validate-update-key.swift" "$PUBLIC_KEY"
OUTPUT="${FOVELLE_APPCAST_OUTPUT_DIR:-$ROOT/build/update-release}"
mkdir -p "$OUTPUT"
cp "$RELEASE_ZIP_PATH" "$OUTPUT/"
printf '%s' "$SPARKLE_PRIVATE_ED_KEY" | "$SPARKLE_ROOT/bin/generate_appcast" \
    --ed-key-file - --maximum-deltas 0 \
    --download-url-prefix "https://github.com/inostarlin-passion/Fovelle/releases/download/$RELEASE_TAG/" \
    -o "$OUTPUT/appcast.xml" "$OUTPUT"
test -s "$OUTPUT/appcast.xml"
echo "Signed update feed generated: $OUTPUT/appcast.xml"
