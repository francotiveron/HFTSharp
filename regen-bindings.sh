#!/usr/bin/env bash
# Regenerate shared/generated/HftShm.g.cs from shared/hft_shm.h.
# Run this whenever hft_shm.h changes.
# Requires: dotnet tool install -g ClangSharpPInvokeGenerator
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
HEADER="$SCRIPT_DIR/shared/hft_shm.h"
OUTPUT="$SCRIPT_DIR/shared/generated/HftShm.g.cs"

LIBCLANG_DIR="$HOME/.dotnet/tools/.store/clangsharppinvokegenerator/21.1.8.3/clangsharppinvokegenerator.linux-x64/21.1.8.3/tools/any/linux-x64"

mkdir -p "$(dirname "$OUTPUT")"

LD_LIBRARY_PATH="$LIBCLANG_DIR:${LD_LIBRARY_PATH:-}" \
ClangSharpPInvokeGenerator \
    --file          "$HEADER" \
    --namespace     HftDemo.Interface \
    --output        "$OUTPUT" \
    --methodClassName HftNative \
    --libraryPath   hft_shm \
    --config        unix-types generate-macro-bindings \
    -I /usr/lib/llvm-18/lib/clang/18/include \
    -I /usr/include \
    -I /usr/include/x86_64-linux-gnu

echo "Generated: $OUTPUT"
