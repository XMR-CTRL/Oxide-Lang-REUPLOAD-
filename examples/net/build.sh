#!/usr/bin/env bash
# Build + run the Oxide TCP echo server (Windows / Winsock2).
# Needs clang (+ its lld) and the built oxide compiler.
set -euo pipefail
cd "$(dirname "$0")/../.."
CLANG="${CLANG:-clang}"
OXIDE="${OXIDE:-./build/oxide.exe}"
OUT="examples/net/echo_server.exe"
echo "[build] $OXIDE exe examples/net/echo_server.ox --link ws2_32 -o $OUT"
"$OXIDE" exe examples/net/echo_server.ox --link ws2_32 -o "$OUT"
echo "built $OUT  (port 0x4F4F = 20303)"
if [ "${1:-}" = "--run" ]; then
  "$OUT"
fi
