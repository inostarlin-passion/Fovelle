#!/bin/bash

set -euo pipefail

fail() {
    echo "ERROR: $*" >&2
    exit 1
}

require_env() {
    local name="$1"
    [[ -n "${!name:-}" ]] || fail "required environment variable is missing: $name"
}

has_architecture() {
    local architectures="$1"
    local expected="$2"
    [[ " $architectures " == *" $expected "* ]]
}

is_macho() {
    file -b "$1" | grep -q "Mach-O"
}

version_is_at_most() {
    local candidate="$1"
    local expected="$2"
    local candidate_major
    local candidate_minor
    local candidate_patch
    local candidate_extra
    local expected_major
    local expected_minor
    local expected_patch

    [[ "$candidate" =~ ^[0-9]+\.[0-9]+(\.[0-9]+)?$ ]] || return 1
    IFS=. read -r candidate_major candidate_minor candidate_patch candidate_extra <<< "$candidate"
    IFS=. read -r expected_major expected_minor expected_patch <<< "$expected"
    [[ -z "${candidate_extra:-}" ]] || return 1
    candidate_patch="${candidate_patch:-0}"
    expected_patch="${expected_patch:-0}"
    (( candidate_major < expected_major ||
       (candidate_major == expected_major && candidate_minor < expected_minor) ||
       (candidate_major == expected_major && candidate_minor == expected_minor && candidate_patch <= expected_patch) ))
}

APP_PATH="${RELEASE_APP_PATH:-build/Fovelle.app}"
RELEASE_ZIP_PATH="${RELEASE_ZIP_PATH:-Fovelle-${GITHUB_REF_NAME:-local}-macOS-universal.zip}"
RELEASE_DRY_RUN="${RELEASE_DRY_RUN:-false}"
NOTARIZATION_TIMEOUT="${NOTARIZATION_TIMEOUT:-30m}"
EXPECTED_MACOS_DEPLOYMENT_TARGET="${FOVELLE_EXPECTED_MACOS_DEPLOYMENT_TARGET:-15.0}"

validate_notarization_timeout() {
    [[ "$NOTARIZATION_TIMEOUT" =~ ^[1-9][0-9]*(s|m|h)?$ ]] || fail "NOTARIZATION_TIMEOUT must be a positive integer with optional s/m/h suffix: $NOTARIZATION_TIMEOUT"
}

validate_deployment_target() {
    [[ "$EXPECTED_MACOS_DEPLOYMENT_TARGET" =~ ^[0-9]+\.[0-9]+$ ]] || fail "FOVELLE_EXPECTED_MACOS_DEPLOYMENT_TARGET must be a numeric macOS version: $EXPECTED_MACOS_DEPLOYMENT_TARGET"
}

validate_notarization_timeout
validate_deployment_target

if [[ "${RELEASE_VALIDATE_VERSION_ONLY:-false}" == "true" ]]; then
    candidate_version="${RELEASE_CANDIDATE_VERSION:-}"
    [[ -n "$candidate_version" ]] || fail "RELEASE_CANDIDATE_VERSION is required for version validation"
    if version_is_at_most "$candidate_version" "$EXPECTED_MACOS_DEPLOYMENT_TARGET"; then
        echo "VERSION_COMPATIBLE: $candidate_version <= $EXPECTED_MACOS_DEPLOYMENT_TARGET"
        exit 0
    fi
    fail "VERSION_INCOMPATIBLE: $candidate_version > $EXPECTED_MACOS_DEPLOYMENT_TARGET"
fi

if [[ "$RELEASE_DRY_RUN" == "true" ]]; then
    echo "DRY_RUN: macdeployqt -> Developer ID Application signing -> notarization -> stapling -> Gatekeeper verification -> Universal zip"
    echo "DRY_RUN_APP_PATH: $APP_PATH"
    echo "DRY_RUN_RELEASE_ZIP_PATH: $RELEASE_ZIP_PATH"
    echo "DRY_RUN_NOTARIZATION_TIMEOUT: $NOTARIZATION_TIMEOUT"
    echo "DRY_RUN_EXPECTED_MACOS_DEPLOYMENT_TARGET: $EXPECTED_MACOS_DEPLOYMENT_TARGET"
    exit 0
fi

require_env APPLE_CERTIFICATE_BASE64
require_env APPLE_CERTIFICATE_PASSWORD
require_env APPLE_APP_SPECIFIC_PASSWORD
require_env APPLE_ID
require_env APPLE_TEAM_ID

[[ -d "$APP_PATH" ]] || fail "application bundle does not exist: $APP_PATH"

MACDEPLOYQT="${MACDEPLOYQT:-}"
if [[ -z "$MACDEPLOYQT" && -n "${QT_ROOT_DIR:-}" && -x "$QT_ROOT_DIR/bin/macdeployqt" ]]; then
    MACDEPLOYQT="$QT_ROOT_DIR/bin/macdeployqt"
fi
if [[ -z "$MACDEPLOYQT" ]]; then
    MACDEPLOYQT="$(command -v macdeployqt || true)"
fi
[[ -x "$MACDEPLOYQT" ]] || fail "macdeployqt was not found"

for tool in base64 codesign ditto file lipo otool security spctl xattr xcrun; do
    command -v "$tool" >/dev/null 2>&1 || fail "required macOS tool was not found: $tool"
done

echo "Deploying Qt and plugin dependencies with $MACDEPLOYQT"
"$MACDEPLOYQT" "$APP_PATH" -always-overwrite

RELEASE_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
echo "Staging a macOS ${EXPECTED_MACOS_DEPLOYMENT_TARGET}-compatible universal Ghostscript runtime from source"
FOVELLE_GHOSTSCRIPT_FORCE_SOURCE=true \
FOVELLE_GHOSTSCRIPT_DEPLOYMENT_TARGET="$EXPECTED_MACOS_DEPLOYMENT_TARGET" \
FOVELLE_GHOSTSCRIPT_ARCHITECTURES="x86_64;arm64" \
    bash "$RELEASE_ROOT/dist/scripts/prepare-ghostscript.sh" \
    --output "$APP_PATH/Contents/Resources/ghostscript"
xattr -cr "$APP_PATH"

assert_macos_deployment_target() {
    local app_path="$1"
    local candidate
    local minos
    local sdk
    local main_binary="$app_path/Contents/MacOS/Fovelle"
    local main_minos=""
    local main_sdk=""
    local plist_target
    local macho_count=0

    while IFS= read -r -d '' candidate; do
        if ! is_macho "$candidate"; then
            continue
        fi
        minos="$(otool -l "$candidate" | awk '$1 == "minos" { print $2; exit }')"
        sdk="$(otool -l "$candidate" | awk '$1 == "sdk" { print $2; exit }')"
        [[ -n "$minos" ]] || fail "Mach-O has no readable minimum macOS version: $candidate"
        if [[ "$candidate" == "$main_binary" ]]; then
            main_minos="$minos"
            main_sdk="$sdk"
        else
            version_is_at_most "$minos" "$EXPECTED_MACOS_DEPLOYMENT_TARGET" || \
                fail "embedded dependency requires newer macOS: $candidate requires $minos, target is $EXPECTED_MACOS_DEPLOYMENT_TARGET"
        fi
        macho_count=$((macho_count + 1))
    done < <(find "$app_path/Contents" -type f -print0)

    ((macho_count > 0)) || fail "no Mach-O files were found while checking deployment target: $app_path"
    [[ "$main_minos" == "$EXPECTED_MACOS_DEPLOYMENT_TARGET" ]] || fail "main executable minimum macOS version mismatch: ${main_minos:-<missing>}, expected $EXPECTED_MACOS_DEPLOYMENT_TARGET"
    [[ "${main_sdk%%.*}" == "${EXPECTED_MACOS_DEPLOYMENT_TARGET%%.*}" ]] || fail "main executable was not compiled with the expected macOS SDK family: ${main_sdk:-<missing>}, expected ${EXPECTED_MACOS_DEPLOYMENT_TARGET%%.*}.x"
    plist_target="$(/usr/libexec/PlistBuddy -c 'Print :LSMinimumSystemVersion' "$app_path/Contents/Info.plist" 2>/dev/null || true)"
    [[ "$plist_target" == "$EXPECTED_MACOS_DEPLOYMENT_TARGET" ]] || fail "Info.plist LSMinimumSystemVersion mismatch: ${plist_target:-<missing>}, expected $EXPECTED_MACOS_DEPLOYMENT_TARGET"
    echo "macOS deployment target verified: $EXPECTED_MACOS_DEPLOYMENT_TARGET across $macho_count Mach-O files and Info.plist"
}

assert_macos_deployment_target "$APP_PATH"

assert_universal_app() {
    local app_path="$1"
    local candidate
    local architectures
    local macho_count=0

    while IFS= read -r -d '' candidate; do
        if ! is_macho "$candidate"; then
            continue
        fi
        architectures="$(lipo -archs "$candidate")"
        if ! has_architecture "$architectures" "arm64" || ! has_architecture "$architectures" "x86_64"; then
            fail "non-Universal Mach-O in app: $candidate ($architectures)"
        fi
        macho_count=$((macho_count + 1))
    done < <(find "$app_path/Contents" -type f -print0)

    ((macho_count > 0)) || fail "no Mach-O files were found in app: $app_path"
    echo "Universal architecture verified for $macho_count Mach-O files"
}

assert_universal_app "$APP_PATH"

TEMP_ROOT="${RUNNER_TEMP:-${TMPDIR:-/tmp}}/fovelle-release-$$"
KEYCHAIN_PATH="$TEMP_ROOT/signing.keychain-db"
CERTIFICATE_PATH="$TEMP_ROOT/developer-id-application.p12"
KEYCHAIN_PASSWORD="$(uuidgen)"
NOTARIZATION_ZIP_PATH="$TEMP_ROOT/Fovelle-notarization.zip"
VERIFY_ROOT="$TEMP_ROOT/verified-release"

cleanup() {
    security delete-keychain "$KEYCHAIN_PATH" >/dev/null 2>&1 || true
    rm -rf "$TEMP_ROOT"
}

mkdir -p "$TEMP_ROOT"
trap cleanup EXIT

echo "Importing the Developer ID certificate into an ephemeral keychain"
printf '%s' "$APPLE_CERTIFICATE_BASE64" | base64 --decode > "$CERTIFICATE_PATH"
security create-keychain -p "$KEYCHAIN_PASSWORD" "$KEYCHAIN_PATH" >/dev/null
security set-keychain-settings -lut 21600 "$KEYCHAIN_PATH"
security unlock-keychain -p "$KEYCHAIN_PASSWORD" "$KEYCHAIN_PATH"
security import "$CERTIFICATE_PATH" \
    -P "$APPLE_CERTIFICATE_PASSWORD" \
    -A \
    -t cert \
    -f pkcs12 \
    -T /usr/bin/codesign \
    -T /usr/bin/security \
    -k "$KEYCHAIN_PATH" >/dev/null
security list-keychain -d user -s "$KEYCHAIN_PATH"

SIGNING_IDENTITY="$(security find-identity -v -p codesigning "$KEYCHAIN_PATH" | awk -F'"' '/Developer ID Application:/{print $2; exit}')"
case "$SIGNING_IDENTITY" in
    "Developer ID Application:"*) ;;
    *) fail "the imported certificate is not a Developer ID Application identity" ;;
esac
[[ "$SIGNING_IDENTITY" == *"($APPLE_TEAM_ID)" ]] || fail "the signing identity does not belong to APPLE_TEAM_ID"
echo "Using signing identity: $SIGNING_IDENTITY"

security find-key -s -t private "$KEYCHAIN_PATH" >/dev/null || fail "the imported PKCS#12 has no private signing key"
# The keychain is unique to this job and is deleted by cleanup. The explicit
# codesign ACL and -A access flag cover non-interactive signing; the
# identity-scoped partition update is best effort because some runner images
# reject it for freshly imported PKCS#12 items.
if ! security set-key-partition-list \
    -S apple-tool:,apple:,codesign: \
    -s \
    -k "$KEYCHAIN_PASSWORD" \
    -t private \
    -l "$SIGNING_IDENTITY" \
    "$KEYCHAIN_PATH" >/dev/null 2>&1; then
    echo "WARNING: identity-scoped keychain partition update was unavailable; continuing with the imported codesign ACL"
fi

sign_code() {
    echo "Signing code object: $1"
    codesign \
        --force \
        --timestamp \
        --options runtime \
        --keychain "$KEYCHAIN_PATH" \
        --sign "$SIGNING_IDENTITY" \
        "$1"
}

CODE_FILES=()
while IFS= read -r -d '' candidate; do
    if is_macho "$candidate"; then
        CODE_FILES+=("$candidate")
    fi
done < <(find "$APP_PATH/Contents" -type f -print0)

for ((index = ${#CODE_FILES[@]} - 1; index >= 0; index--)); do
    sign_code "${CODE_FILES[index]}"
done

BUNDLE_DIRECTORIES=()
while IFS= read -r -d '' candidate; do
    BUNDLE_DIRECTORIES+=("$candidate")
done < <(
    find "$APP_PATH/Contents" -type d \( \
        -name '*.app' -o \
        -name '*.appex' -o \
        -name '*.bundle' -o \
        -name '*.framework' -o \
        -name '*.xpc' \
    \) -print0
)

for candidate in "${BUNDLE_DIRECTORIES[@]}"; do
    sign_code "$candidate"
done
sign_code "$APP_PATH"

codesign --verify --deep --strict --verbose=2 "$APP_PATH"
CODE_SIGNATURE="$(codesign --display --verbose=4 "$APP_PATH" 2>&1 || true)"
echo "$CODE_SIGNATURE" | grep -q "Authority=Developer ID Application:" || fail "final app is not signed by Developer ID Application"
echo "$CODE_SIGNATURE" | grep -q "flags=.*runtime" || fail "Hardened Runtime is not enabled"
echo "$CODE_SIGNATURE" | grep -q "Timestamp=" || fail "secure signing timestamp is missing"

echo "Submitting the signed app to Apple notarization"
ditto -c -k --sequesterRsrc --keepParent "$APP_PATH" "$NOTARIZATION_ZIP_PATH"
echo "Notarization wait timeout: $NOTARIZATION_TIMEOUT"
if ! xcrun notarytool submit "$NOTARIZATION_ZIP_PATH" \
    --apple-id "$APPLE_ID" \
    --password "$APPLE_APP_SPECIFIC_PASSWORD" \
    --team-id "$APPLE_TEAM_ID" \
    --wait \
    --timeout "$NOTARIZATION_TIMEOUT" \
    --verbose; then
    fail "Apple notarization failed or exceeded timeout ($NOTARIZATION_TIMEOUT); no release artifact was created. Inspect the submission ID above with notarytool info/log."
fi
xcrun stapler staple "$APP_PATH"
xcrun stapler validate "$APP_PATH"
spctl --assess --type execute --verbose=4 --ignore-cache "$APP_PATH"

rm -f "$RELEASE_ZIP_PATH"
ditto -c -k --sequesterRsrc --keepParent "$APP_PATH" "$RELEASE_ZIP_PATH"

mkdir -p "$VERIFY_ROOT"
ditto -x -k "$RELEASE_ZIP_PATH" "$VERIFY_ROOT"
VERIFIED_APP="$VERIFY_ROOT/$(basename "$APP_PATH")"
[[ -d "$VERIFIED_APP" ]] || fail "release zip did not contain the expected app bundle"
assert_universal_app "$VERIFIED_APP"
assert_macos_deployment_target "$VERIFIED_APP"
codesign --verify --deep --strict --verbose=2 "$VERIFIED_APP"
xcrun stapler validate "$VERIFIED_APP"
spctl --assess --type execute --verbose=4 --ignore-cache "$VERIFIED_APP"

echo "Release package created: $RELEASE_ZIP_PATH"
shasum -a 256 "$RELEASE_ZIP_PATH"
