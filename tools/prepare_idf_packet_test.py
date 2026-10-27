#!/usr/bin/env python3
"""Extract real packet functions and check a pinned upstream baseline, offline."""
import hashlib
import sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
NAME='btc_media_aa_prep_sbc_2_send'
def extract(s):
    start=s.index('static void '+NAME+'(UINT8 nb_frame)\n{')
    a=s.index('{',start);b=a+1;depth=1
    while depth:
        depth+=(s[b]=='{')-(s[b]=='}');b+=1
    return s[start:b]
p=Path(sys.argv[1])
integrated=extract((ROOT/'app/src/idf_a2dp/btc_a2dp_source.c').read_text())
patch=(ROOT/'docs/patches/esp-idf-5.5.4-a2dp-packet-allocation.patch').read_text().splitlines()
old='\n'.join(x[1:] for x in patch if x.startswith('-') and not x.startswith('---'))
new='\n'.join(x[1:] for x in patch if x.startswith('+') and not x.startswith('+++'))
proposed=integrated.replace('chonkpod_a2dp_packet_alloc(packet_bytes)','osi_malloc(packet_bytes)')
assert proposed.count(new)==1
original=proposed.replace(new,old)
assert hashlib.sha256(original.encode()).hexdigest()=='8111bea7158845879d97fca90f6d7ed246e3879ec8ac5cbc26ccb0f090ee394b', 'Packet function differs from pinned upstream baseline'
if len(sys.argv)>2:
    data=Path(sys.argv[2]).read_bytes()
    assert hashlib.sha256(data).hexdigest()=='ad71c76d27121f34ac3111ae3d9fcfd39aef25c4bad42f21231c6a0f4a692a4b'
    assert extract(data.decode())==original
for variant,s in [('original',original),('proposed',proposed),('integrated',integrated)]:
    (p/('packet_'+variant+'.inc')).write_text(s.replace(NAME,'packet_'+variant)+'\n')
