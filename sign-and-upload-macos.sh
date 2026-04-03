#!/usr/bin/env bash
set -euo pipefail

# Check for gh CLI
if ! command -v gh &> /dev/null; then
	echo "ERROR: GitHub CLI (gh) is not installed"
	echo "Install with: brew install gh"
	exit 1
fi

# Parse arguments
VERBOSE=false
while [[ $# -gt 0 ]]; do
	case "$1" in
		-v|--verbose)
			VERBOSE=true
			shift
			;;
		-*)
			echo "ERROR: Unknown option: $1"
			exit 1
			;;
		*)
			VERSION="$1"
			shift
			;;
	esac
done

# Get release tag from argument or latest tag
RELEASE_TAG="${VERSION:-$(git describe --tags --abbrev=0 | sed 's/^v//')}"

# Detect repository from git remote and set GH_REPO
if [ -z "${GH_REPO:-}" ]; then
	REMOTE=$(git remote get-url origin 2>/dev/null | sed 's|.*github.com[:/]||' | sed 's|\.git||')
	if [ -n "$REMOTE" ]; then
		export GH_REPO="$REMOTE"
		$VERBOSE && echo "Using repository: $GH_REPO"
	else
		echo "ERROR: Could not detect GitHub repository from git remote"
		echo "Please set GH_REPO environment variable or run 'gh repo set-default'"
		exit 1
	fi
fi

$VERBOSE && echo "Processing release v${RELEASE_TAG}..."

# Create temp directory (clean any existing files)
rm -rf ./unsigned
mkdir -p ./unsigned

# Download unsigned binaries from release (pattern doesn't include version)
$VERBOSE && echo "Downloading unsigned binaries from release v${RELEASE_TAG}..."
gh release download "v${RELEASE_TAG}" \
	--pattern "*macos*unsigned*.zip" \
	--dir ./unsigned

# Check if any files were downloaded
if [ -z "$(ls -A ./unsigned/*.zip 2>/dev/null)" ]; then
	echo "ERROR: No unsigned macOS binaries found in release v${RELEASE_TAG}"
	rm -rf ./unsigned
	exit 1
fi

# Extract version from downloaded filename (e.g., GCFFlasher-4.12.0-macos-arm64-unsigned.zip)
VERSION=$(basename ./unsigned/*.zip | head -1 | sed 's/GCFFlasher-\([0-9.]*\)-.*/\1/')
$VERBOSE && echo "Detected version from assets: ${VERSION}"

# Process each architecture
for zip in ./unsigned/GCFFlasher-${VERSION}-macos-*-unsigned.zip; do
	# Extract architecture from filename
	# From: GCFFlasher-4.12.0-macos-arm64-unsigned.zip
	ARCH=$(basename "$zip" | sed 's/.*GCFFlasher-[0-9.]*-macos-\([^-]*\)-unsigned.*/\1/')
	
	$VERBOSE && echo "Processing architecture: ${ARCH}"
	
	# Extract
	unzip "$zip"
	
	# Move to build/ for sign-macos.sh
	mv "GCFFlasher-${VERSION}-macos-${ARCH}-unsigned" build/GCFFlasher
	
	# Sign and notarize (creates build/GCFFlasher-${ARCH})
	./sign-macos.sh "$ARCH"
	
	# Move signed binary to current dir, zip it, then clean up
	mv "build/GCFFlasher-${ARCH}" .
	rm -f "GCFFlasher-${VERSION}-macos-${ARCH}.zip"
	zip "GCFFlasher-${VERSION}-macos-${ARCH}.zip" "GCFFlasher-${ARCH}"
	rm -f "GCFFlasher-${ARCH}"
	rm -f "GCFFlasher-${VERSION}-macos-${ARCH}-unsigned"
	
	$VERBOSE && echo "Created: GCFFlasher-${VERSION}-macos-${ARCH}.zip"
done

# Upload signed binaries to release
$VERBOSE && echo "Uploading signed binaries to release v${RELEASE_TAG}..."
gh release upload "v${RELEASE_TAG}" GCFFlasher-${VERSION}-macos-*.zip --clobber

# Delete unsigned assets from release
$VERBOSE && echo "Removing unsigned assets from release..."
for zip in ./unsigned/GCFFlasher-${VERSION}-macos-*-unsigned.zip; do
	ASSET_NAME=$(basename "$zip")
	gh release delete-asset "v${RELEASE_TAG}" "$ASSET_NAME" --yes || true
done

# Clean up
rm -rf ./unsigned

echo "SUCCESS: Signed and notarized binaries uploaded to release v${RELEASE_TAG}"
