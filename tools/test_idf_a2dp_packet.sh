#!/usr/bin/env bash
# Test actual integrated SDK packet code and real project allocator glue offline.
# Optional argument: full upstream v5.5.4 btc_a2dp_source.c for extra provenance check.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TASK_BUILD="$(mktemp -d)"
trap 'rm -rf "$TASK_BUILD"' EXIT
python3 "$ROOT/tools/verify_a2dp_link.py" --sources-only
python3 "$ROOT/tools/prepare_idf_packet_test.py" "$TASK_BUILD" "$@"
# Compile the real C glue unchanged, with only the underlying allocator stubbed.
mkdir -p "$TASK_BUILD/sketch/src/idf_a2dp/osi"
cp "$ROOT/app/src/idf_a2dp/packet_allocator.c" "$TASK_BUILD/sketch/src/idf_a2dp/"
cp "$ROOT/app/bt_sdk_patch.h" "$TASK_BUILD/sketch/"
cat > "$TASK_BUILD/sketch/src/idf_a2dp/osi/allocator.h" <<'HEADER'
#include <stddef.h>
void* osi_malloc_func(size_t size);
#define osi_malloc(n) osi_malloc_func(n)
HEADER
FLAGS=(-O1 -g -fsanitize=address,undefined -fno-pie -no-pie)
gcc -std=c17 -Wall -Wextra -Werror "${FLAGS[@]}" -c "$TASK_BUILD/sketch/src/idf_a2dp/packet_allocator.c" -o "$TASK_BUILD/glue.o"
g++ -std=c++17 -Wall -Wextra -Werror "${FLAGS[@]}" -I"$ROOT/app" \
 -I"$TASK_BUILD" "$ROOT/tests/idf_a2dp_packet.cpp" "$TASK_BUILD/glue.o" -o "$TASK_BUILD/check"
"$TASK_BUILD/check"
