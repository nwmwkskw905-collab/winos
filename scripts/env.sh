#!/usr/bin/env bash
# Ambiente de build do Portico. Fonte: `source scripts/env.sh`
PORTICO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export PORTICO_ROOT

# Toolchain Swift local (fora do snapshot do workspace). Se ausente, rodar setup_toolchain.sh.
for candidate in \
  "${SWIFT_TOOLCHAIN:-}" \
  "$HOME/.cache/swifttc/swift-6.1.2-RELEASE-ubuntu24.04/usr/bin" \
  "$HOME/.cache/swifttc/current/usr/bin" \
  "/usr/bin"; do
  if [ -n "$candidate" ] && [ -x "$candidate/swift" ]; then
    export PATH="$candidate:$PATH"
    break
  fi
done

if ! command -v swift >/dev/null 2>&1; then
  echo "env.sh: swift não encontrado — rode scripts/setup_toolchain.sh" >&2
fi
