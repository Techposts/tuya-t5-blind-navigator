#!/usr/bin/env bash
# Cuts a release: pulls the latest QIO binary from the build VM,
# tags the current commit, creates a GitHub release with the .bin attached.
#
# Usage:  ./scripts/release.sh v0.2.0  "Brief release notes go here"
set -euo pipefail

TAG="${1:?tag required, e.g. v0.2.0}"
NOTES="${2:-Auto-generated release}"

VM_USER="${VM_USER:-tuya}"
VM_HOST="${VM_HOST:-192.168.0.171}"
VM_BIN_GLOB='/home/tuya/TuyaOpen/apps/tuya.ai/blind_navigator/.build/bin/blind_navigator_QIO_*.bin'

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
mkdir -p "$ROOT/releases"

echo "==> Fetching latest firmware from $VM_USER@$VM_HOST"
sshpass -p "${VM_PASS:?VM_PASS env var required}" \
    scp -o StrictHostKeyChecking=no "$VM_USER@$VM_HOST:$VM_BIN_GLOB" "$ROOT/releases/"
BIN="$(ls -t "$ROOT"/releases/blind_navigator_QIO_*.bin | head -1)"
SHA="$(shasum -a 256 "$BIN" | awk '{print $1}')"

echo "==> Binary: $BIN"
echo "    SHA256: $SHA"

echo "==> Tagging $TAG"
git tag -a "$TAG" -m "$NOTES"
git push origin "$TAG"

echo "==> Creating GitHub release"
gh release create "$TAG" \
    "$BIN" \
    --title "$TAG" \
    --notes "$NOTES

**Firmware binary:** $(basename "$BIN")
**SHA256:** $SHA

## How to flash
\`\`\`bash
python3 ~/TuyaOpen/tos.py flash -p /dev/ttyUSB0 -b 460800
\`\`\`
See [docs/BUILD_AND_FLASH.md](https://github.com/Techposts/tuya-t5-blind-navigator/blob/main/docs/BUILD_AND_FLASH.md)."

echo "==> Done: https://github.com/Techposts/tuya-t5-blind-navigator/releases/tag/$TAG"
