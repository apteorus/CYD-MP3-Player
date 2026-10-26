#!/usr/bin/env bash
# Requires existing gcc/g++, ffmpeg and arduino-libhelix v0.9.4 checkout.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
: "${HELIX_SOURCE:?Set HELIX_SOURCE to arduino-libhelix/src}"
TASK_BUILD="$(mktemp -d)"
trap 'rm -rf "$TASK_BUILD"' EXIT
ffmpeg -v error -f lavfi -i 'sine=frequency=440:duration=3' -ac 2 -ar 44100 \
  -c:a libmp3lame -b:a 128k "$TASK_BUILD/test.mp3"
for source in "$HELIX_SOURCE"/libhelix-mp3/*.c; do
  gcc -O1 -I"$HELIX_SOURCE" -c "$source" -o "$TASK_BUILD/$(basename "$source").o"
done
g++ -std=c++17 -O1 -DHELIX_LOGGING_ACTIVE=false -include "$ROOT/tests/helix_host.h" -I"$ROOT/app" -I"$HELIX_SOURCE" \
  "$ROOT/tests/mp3_reset.cpp" "$HELIX_SOURCE/utils/helix_memory.cpp" "$TASK_BUILD"/*.o \
  -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free -o "$TASK_BUILD/check"
"$TASK_BUILD/check" "$TASK_BUILD/test.mp3"
