#!/usr/bin/env bash
# Baixa o toolchain Swift (Linux x86_64) para validação do núcleo. Idempotente.
set -euo pipefail
DEST="${SWIFT_TC_DIR:-$HOME/.cache/swifttc}"
URL="https://download.swift.org/swift-6.1.2-release/ubuntu2404/swift-6.1.2-RELEASE/swift-6.1.2-RELEASE-ubuntu24.04.tar.gz"
if [ -x "$DEST/swift-6.1.2-RELEASE-ubuntu24.04/usr/bin/swift" ]; then
  echo "toolchain já presente em $DEST"
  exit 0
fi
mkdir -p "$DEST"
echo "baixando $URL ..."
curl -sL -o "$DEST/swift.tar.gz" "$URL"
tar -xzf "$DEST/swift.tar.gz" -C "$DEST"
rm -f "$DEST/swift.tar.gz"
"$DEST/swift-6.1.2-RELEASE-ubuntu24.04/usr/bin/swift" --version
echo "ok"
