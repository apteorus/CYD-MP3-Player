#!/usr/bin/env bash
# Run a proposed SDK patch against real source without changing the installed SDK.
# Usage: bash tools/test_idf_a2dp_packet.sh /path/to/v5.5.4/btc_a2dp_source.c
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
: "${1:?Pass ESP-IDF v5.5.4 btc_a2dp_source.c (see docs/chonkpod_a2dp_investigation.md)}"
TASK_BUILD="$(mktemp -d)"
trap 'rm -rf "$TASK_BUILD"' EXIT
REL=components/bt/host/bluedroid/btc/profile/std/a2dp/btc_a2dp_source.c
mkdir -p "$TASK_BUILD/$(dirname "$REL")"
python3 - "$1" <<'PY'
import hashlib,sys
assert hashlib.sha256(open(sys.argv[1],'rb').read()).hexdigest()=='ad71c76d27121f34ac3111ae3d9fcfd39aef25c4bad42f21231c6a0f4a692a4b', 'Unexpected SDK source; refuse a blind patch'
PY
cp "$1" "$TASK_BUILD/original.c"
cp "$1" "$TASK_BUILD/$REL"
patch --batch --forward -s -p1 -d "$TASK_BUILD" < "$ROOT/docs/patches/esp-idf-5.5.4-a2dp-packet-allocation.proposed.patch"
python3 - "$TASK_BUILD" "$REL" <<'PY'
import pathlib,sys
p=pathlib.Path(sys.argv[1])
for variant,file in [('original','original.c'),('proposed',sys.argv[2])]:
 s=(p/file).read_text();name='btc_media_aa_prep_sbc_2_send'
 start=s.index('static void '+name+'(UINT8 nb_frame)\n{');a=s.index('{',start);depth=1;b=a+1
 while depth:
  depth+=(s[b]=='{')-(s[b]=='}');b+=1
 (p/('packet_'+variant+'.inc')).write_text(s[start:b].replace(name,'packet_'+variant)+'\n')
PY
g++ -std=c++17 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -fno-pie -no-pie \
 -I"$TASK_BUILD" "$ROOT/tests/idf_a2dp_packet.cpp" -o "$TASK_BUILD/check"
"$TASK_BUILD/check"
