#!/usr/bin/env bash
# Package "Voice Control.app" for distribution to other Macs:
#   1. Re-sign every nested dylib + ffmpeg + the app itself, inside-out, with a
#      Developer ID Application cert + hardened runtime + secure timestamp.
#   2. Zip it and submit to Apple's notary service.
#   3. Staple the notarization ticket so it works offline.
#
# Prereqs (one-time):
#   - A "Developer ID Application" certificate in your keychain
#     (Xcode > Settings > Accounts > Manage Certificates > + ).
#   - Stored notarytool credentials, e.g.:
#       xcrun notarytool store-credentials "voicecontrol-notary" \
#         --apple-id "dmkwjk@gmail.com" --team-id "2SBVUZX3N7" \
#         --password "<app-specific-password>"
#     (app-specific password from appleid.apple.com > Sign-In & Security).
#
# Usage:
#   scripts/package-macos.sh [path/to/Voice Control.app] [notary-profile]
#
set -euo pipefail

APP="${1:-build-xcode/voice-control-app_artefacts/Release/Voice Control.app}"
NOTARY_PROFILE="${2:-voicecontrol-notary}"

if [[ ! -d "$APP" ]]; then
    echo "error: app not found: $APP" >&2
    echo "Build the Release config first (xcodebuild ... -configuration Release)." >&2
    exit 1
fi

# Find the Developer ID Application identity (not the "Apple Development" one).
IDENTITY="$(security find-identity -v -p codesigning \
    | grep "Developer ID Application" \
    | head -1 \
    | sed -E 's/.*"(.*)"/\1/')"
if [[ -z "${IDENTITY}" ]]; then
    echo "error: no 'Developer ID Application' identity in keychain." >&2
    echo "Create one: Xcode > Settings > Accounts > Manage Certificates > + > Developer ID Application." >&2
    exit 1
fi
echo "Signing identity: ${IDENTITY}"

SIGN=(codesign --force --timestamp --options runtime --sign "${IDENTITY}")

# 1. Sign inside-out: nested libraries and helper executables first, app last.
echo "Signing nested binaries..."
while IFS= read -r -d '' f; do
    "${SIGN[@]}" "$f"
done < <(find "$APP/Contents" -type f \( -name "*.dylib" -o -name "ffmpeg" \) -print0)

echo "Signing app bundle..."
"${SIGN[@]}" "$APP"

# 2. Verify the signature is well-formed before wasting a notary round-trip.
echo "Verifying signature..."
codesign --verify --deep --strict --verbose=2 "$APP"

# 3. Zip with ditto (preserves bundle structure / metadata for notarization).
ZIP="${APP%.app}.zip"
rm -f "$ZIP"
echo "Zipping -> $ZIP"
/usr/bin/ditto -c -k --keepParent "$APP" "$ZIP"

# 4. Submit and wait for the notary service.
echo "Submitting to notary service (this can take a few minutes)..."
xcrun notarytool submit "$ZIP" --keychain-profile "${NOTARY_PROFILE}" --wait

# 5. Staple the ticket onto the .app, then re-zip for shipping.
echo "Stapling ticket..."
xcrun stapler staple "$APP"
xcrun stapler validate "$APP"

rm -f "$ZIP"
/usr/bin/ditto -c -k --keepParent "$APP" "$ZIP"

echo
echo "Done. Send this to your friend:"
echo "  $ZIP"
echo "Gatekeeper check (should say 'accepted' / 'Notarized Developer ID'):"
spctl --assess --type execute --verbose=4 "$APP" || true
