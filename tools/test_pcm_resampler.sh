#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TASK_BUILD="$(mktemp -d)"
trap 'rm -rf "$TASK_BUILD"' EXIT
g++ -std=c++17 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -fno-pie -no-pie \
  -I"$ROOT/app" "$ROOT/tests/pcm_resampler.cpp" -o "$TASK_BUILD/check"
"$TASK_BUILD/check"
