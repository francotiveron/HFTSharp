#!/usr/bin/env bash
set -euo pipefail

BUILD_DIR="build"

echo "==> Building C shared library + C++ executor..."
cmake -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release .
cmake --build "$BUILD_DIR" -j"$(nproc)"

echo ""
echo "==> Building F# commander (includes C# interface project)..."
dotnet build -c Release fsharp/Commander.fsproj

echo ""
echo "==> Build complete."
echo ""
echo "To regenerate C# bindings after changing shared/hft_shm.h:"
echo "  dotnet tool install -g ClangSharpPInvokeGenerator  # once"
echo "  cmake --build $BUILD_DIR --target generate_bindings"
echo ""
echo "Run in two separate terminals:"
echo ""
echo "  Terminal 1 (start first):"
echo "    ./$BUILD_DIR/cpp/executor"
echo ""
echo "  Terminal 2:"
echo "    LD_LIBRARY_PATH=\$(pwd)/$BUILD_DIR dotnet run --project fsharp/Commander.fsproj -c Release"
