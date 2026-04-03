#!/usr/bin/env bash
set -euo pipefail

# Load credentials from file if available (local development)
if [ -e ./cred-macos ]; then
	source ./cred-macos
fi

: "${DEVELOPER_ID_APPLICATION:?DEVELOPER_ID_APPLICATION not set}"
: "${TEAM_ID:?TEAM_ID not set}"
: "${PASS:?PASS not set}"
: "${APPLE_ID:?APPLE_ID not set}"

# Usage: sign-macos.sh [ARCH]
# ARCH is optional; if not provided, uses uname -m (backward compatible)
ARCH="${1:-$(uname -m)}"
BINARY="build/GCFFlasher"
SIGNED_BINARY="build/GCFFlasher-${ARCH}"
ZIP_FILE="GCFFlasher-${ARCH}.zip"

if [ ! -x "$BINARY" ]; then
	echo "ERROR: Binary not found at $BINARY"
	exit 1
fi

echo "Signing binary..."
codesign --sign "$DEVELOPER_ID_APPLICATION" \
		--options runtime \
		--timestamp \
		--force \
		"$BINARY"

cp "$BINARY" "$SIGNED_BINARY"

echo "Creating archive for notarization..."
zip -j "$ZIP_FILE" "$SIGNED_BINARY"

echo "Submitting for notarization..."
xcrun notarytool submit "$ZIP_FILE" \
	--apple-id "$APPLE_ID" \
	--team-id "$TEAM_ID" \
	--password "$PASS" \
	--wait

echo "Verifying signature..."
codesign --verify --verbose "$SIGNED_BINARY"

rm -f "$ZIP_FILE"

echo "SUCCESS: Signed and notarized binary at $SIGNED_BINARY"
