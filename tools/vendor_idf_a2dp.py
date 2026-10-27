#!/usr/bin/env python3
"""Developer-only reproducible import; normal Arduino builds need no downloads."""
import argparse
import hashlib
import json
import posixpath
import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEST = ROOT / 'app/src/idf_a2dp'
COMMIT = '735507283d5b2f9fb363a1901172dbd9e847945d'
SOURCE = 'components/bt/host/bluedroid/btc/profile/std/a2dp/btc_a2dp_source.c'
SOURCE_SHA = 'ad71c76d27121f34ac3111ae3d9fcfd39aef25c4bad42f21231c6a0f4a692a4b'
INCLUDE = re.compile(r'(^\s*#\s*include\s*)["<]([^">]+)[">]', re.M)

def sha(data):
    return hashlib.sha256(data).hexdigest()

def main():
    p = argparse.ArgumentParser(__doc__)
    p.add_argument('--idf', type=Path, required=True)
    p.add_argument('--sdk-bt-include', type=Path, required=True)
    a = p.parse_args()
    assert subprocess.check_output(['git', '-C', str(a.idf), 'rev-parse', 'HEAD'], text=True).strip() == COMMIT
    original = (a.idf / SOURCE).read_bytes()
    assert sha(original) == SOURCE_SHA
    headers = list((a.idf / 'components/bt').rglob('*.h'))
    sdk_headers = list(a.sdk_bt_include.rglob('*.h'))
    selected = {}
    records = {}
    def visit(name):
        # Exported ESP API headers continue to come from the pinned installed core.
        if name.startswith("esp_") and any(str(x).endswith('/'+name) for x in sdk_headers):
            return False
        candidates = sorted(x for x in headers if str(x).endswith('/'+name))
        if not candidates:
            return False # standard C or another SDK component header
        assert len({sha(x.read_bytes()) for x in candidates}) == 1, ('ambiguous header', name)
        path = candidates[0]
        if name in selected:
            assert selected[name] == path
            return True
        selected[name] = path
        for _, inc in INCLUDE.findall(path.read_text()):
            visit(inc)
        return True
    for _, inc in INCLUDE.findall(original.decode()):
        visit(inc)
    def rewrite(text, location):
        parent = posixpath.dirname(location) or '.'
        def change(m):
            name = m[2]
            return m[1] + '"' + posixpath.relpath(name, parent) + '"' if name in selected else m[0]
        return INCLUDE.sub(change, text)
    DEST.mkdir(parents=True, exist_ok=True)
    for name, path in sorted(selected.items()):
        target = DEST / name
        target.parent.mkdir(parents=True, exist_ok=True)
        data = rewrite(path.read_text(), name).encode()
        target.write_bytes(data)
        records[name] = {'upstream': path.relative_to(a.idf).as_posix(), 'upstream_sha256': sha(path.read_bytes()), 'sha256': sha(data)}
    # Apply the reviewed patch in a temporary tree, without modifying the checkout.
    import tempfile
    with tempfile.TemporaryDirectory() as tmp:
        temp = Path(tmp) / SOURCE
        temp.parent.mkdir(parents=True)
        temp.write_bytes(original)
        subprocess.run(['patch', '--batch', '--forward', '-s', '-p1', '-d', tmp, '-i', str(ROOT/'docs/patches/esp-idf-5.5.4-a2dp-packet-allocation.patch')], check=True)
        patched = temp.read_text()
    patched = patched.replace('osi_malloc(packet_bytes)', 'chonkpod_a2dp_packet_alloc(packet_bytes)')
    codec_guard = "#if (BTC_AV_SRC_INCLUDED == TRUE) && (BTC_AV_EXT_CODEC == FALSE)"
    patched = patched.replace(codec_guard, """#if BTC_AV_SRC_INCLUDED != TRUE || BTC_AV_EXT_CODEC != FALSE || A2D_DYNAMIC_MEMORY != TRUE
#error "ChonkPod requires the pinned legacy SBC source and dynamic A2DP memory configuration"
#endif
""" + codec_guard, 1)
    guard = '''// Project-local rebuild of one ESP-IDF 5.5.4 component; see README.md.
#ifndef CYD_UPLOADER
#include <esp_arduino_version.h>
#include <esp_idf_version.h>
#include <sdkconfig.h>
#if ESP_ARDUINO_VERSION != ESP_ARDUINO_VERSION_VAL(3, 3, 10) || ESP_IDF_VERSION != ESP_IDF_VERSION_VAL(5, 5, 4)
#error "ChonkPod A2DP override requires Arduino ESP32 3.3.10 / ESP-IDF 5.5.4"
#endif
#ifndef CONFIG_IDF_TARGET_ESP32
#error "ChonkPod A2DP override supports the original ESP32 only"
#endif
#include "../../bt_sdk_patch.h"
'''
    data = (guard + rewrite(patched, 'btc_a2dp_source.c') + '\n#endif // !CYD_UPLOADER\n').encode()
    (DEST/'btc_a2dp_source.c').write_bytes(data)
    records['btc_a2dp_source.c'] = {'upstream': SOURCE, 'upstream_sha256': SOURCE_SHA, 'sha256': sha(data)}
    # Source and all private-header notices remain intact. Root license applies.
    license_data = (a.idf/'LICENSE').read_bytes()
    (DEST/'LICENSE').write_bytes(license_data)
    records['LICENSE'] = {'upstream': 'LICENSE', 'upstream_sha256': sha(license_data), 'sha256': sha(license_data)}
    manifest = {'idf_commit': COMMIT, 'idf_tag': 'v5.5.4', 'arduino_esp32': '3.3.10', 'files': records}
    (DEST/'manifest.json').write_text(json.dumps(manifest, indent=2, sort_keys=True)+'\n')
    print(f'Imported one C module and {len(selected)} private headers; installed SDK unchanged')

if __name__ == '__main__':
    main()
