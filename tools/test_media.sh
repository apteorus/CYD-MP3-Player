#!/usr/bin/env bash
# Production parser/render/cache tests, without an ESP32 or Arduino mocks of codecs.
# Requires gcc/g++, Python with Pillow. Optional first argument: user's full VLW.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MEDIA_PYTHON="${MEDIA_PYTHON:-python3}"
MEDIA_BUILD="$(mktemp -d)"
trap 'rm -rf "$MEDIA_BUILD"' EXIT
"$MEDIA_PYTHON" - "$MEDIA_BUILD" <<'PY'
import sys
from PIL import Image
from pathlib import Path
p=Path(sys.argv[1])
Image.new('RGB',(101,51),(255,0,0)).save(p/'cover.jpg')
Image.new('RGB',(100,100),(0,0,255)).save(p/'blue.jpg')
Image.new('RGB',(101,51),(255,0,0)).save(p/'progressive.jpg',progressive=True)
for name,size in [('scale0',(320,160)),('scale1',(640,320)),('scale2',(1280,640)),('scale3',(2560,1280)),('portrait',(51,101))]:
    Image.new('RGB',size,(255,0,0)).save(p/(name+'.jpg'))
PY
FLAGS=(-std=c++17 -Wall -Wextra -Werror -O1 -g -ffunction-sections -fdata-sections -I"$ROOT/tests/host" -I"$ROOT/app")
CFLAGS=(-O1 -g)
if [[ "${MEDIA_SANITIZE:-0}" = 1 ]]; then
  FLAGS+=(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie)
  CFLAGS+=(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie)
fi
mkdir -p "$MEDIA_BUILD"/{font,title,id3,jpeg,cover,storage}
g++ "${FLAGS[@]}" "$ROOT/app/vlw_stream.cpp" "$ROOT/tests/font_stream.cpp" -o "$MEDIA_BUILD/font-test"
"$MEDIA_BUILD/font-test" "$MEDIA_BUILD/font" "$@"
g++ "${FLAGS[@]}" "$ROOT/app/vlw_stream.cpp" "$ROOT/app/titles.cpp" "$ROOT/tests/title_recovery.cpp" -Wl,--gc-sections -o "$MEDIA_BUILD/title-test"
"$MEDIA_BUILD/title-test" "$MEDIA_BUILD/title"
g++ "${FLAGS[@]}" "$ROOT/app/id3.cpp" "$ROOT/tests/id3_metadata.cpp" -o "$MEDIA_BUILD/id3-test"
"$MEDIA_BUILD/id3-test" "$MEDIA_BUILD/id3"
gcc "${CFLAGS[@]}" -I"$ROOT/app/src/TJpgDec" -c "$ROOT/app/src/TJpgDec/tjpgd.c" -o "$MEDIA_BUILD/tjpgd.o"
g++ "${FLAGS[@]}" "$ROOT/app/jpeg_cache.cpp" "$ROOT/tests/jpeg_cache.cpp" "$MEDIA_BUILD/tjpgd.o" -o "$MEDIA_BUILD/jpeg-test"
"$MEDIA_BUILD/jpeg-test" "$MEDIA_BUILD/jpeg" "$MEDIA_BUILD/cover.jpg" "$MEDIA_BUILD/progressive.jpg" \
  "$MEDIA_BUILD/scale0.jpg" "$MEDIA_BUILD/scale1.jpg" "$MEDIA_BUILD/scale2.jpg" "$MEDIA_BUILD/scale3.jpg" "$MEDIA_BUILD/portrait.jpg"
g++ "${FLAGS[@]}" "$ROOT/app/id3.cpp" "$ROOT/app/jpeg_cache.cpp" "$ROOT/app/cover_cache.cpp" "$ROOT/tests/cover_pipeline.cpp" "$MEDIA_BUILD/tjpgd.o" -o "$MEDIA_BUILD/cover-test"
"$MEDIA_BUILD/cover-test" "$MEDIA_BUILD/cover" "$MEDIA_BUILD/cover.jpg" "$MEDIA_BUILD/blue.jpg" "$MEDIA_BUILD/progressive.jpg"

g++ "${FLAGS[@]}" "$ROOT/app/id3.cpp" "$ROOT/app/storage.cpp" "$ROOT/tests/storage_index.cpp" -o "$MEDIA_BUILD/storage-test"
"$MEDIA_BUILD/storage-test" "$MEDIA_BUILD/storage"
