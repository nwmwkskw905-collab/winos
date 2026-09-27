#!/bin/sh
# Build + testes completos do núcleo do Portico (C + Swift).
# Uso: ./scripts/build_and_test.sh
set -e
cd "$(dirname "$0")/.."
if ! command -v swift >/dev/null 2>&1; then
    ./scripts/setup_toolchain.sh
fi
if ! command -v make >/dev/null 2>&1; then
    echo "make é necessário" >&2; exit 1
fi
make test
python3 scripts/gen_xcodeproj.py
echo "== TUDO VERDE =="
