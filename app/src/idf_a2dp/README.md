# Project-local ESP-IDF A2DP source component

This directory rebuilds **one** ESP-IDF 5.5.4 translation unit as part of the
Arduino sketch. The installed Arduino core and `libbt.a` remain unchanged.
All other Bluetooth components, including the SBC encoder, come from that SDK.

Upstream: espressif/esp-idf tag `v5.5.4`, commit
`735507283d5b2f9fb363a1901172dbd9e847945d`.
`manifest.json` records the upstream path/hash and imported hash of every SDK
file. Copyright and license notices are retained; see `LICENSE` (Apache-2.0).

The import contains the source module and its 52 required private headers.
Private include paths are rewritten relative to each imported file so Arduino
requires no custom include paths/build recipes. Exported `esp_*.h` APIs and
`sdkconfig.h` still come from the installed SDK. This requires the **official
Arduino ESP32 3.3.10 SDK**, original ESP32 target, legacy SBC source, and dynamic
A2DP allocation configuration; unsupported versions/configurations fail closed.

Changes to upstream source:

* Exclude the component entirely from `CYD_UPLOADER` images.
* Enforce the pinned Arduino/IDF/configuration at compile time.
* Apply `docs/patches/esp-idf-5.5.4-a2dp-packet-allocation.patch`: replace the fixed
  4112-byte packet allocation with header/offset + max(MTU, 1024), capped at 4112.
  Explicit C conditionals implement min/max without adding a macro dependency.
* Route that allocation through `packet_allocator.c`, which delegates to the
  original SDK allocator and adds fixed counters and a distinct audited symbol.
  It has no pool, heap reserve, printing or wait. Existing SDK free paths apply.

Public SDK entrypoints from this sketch object resolve before the SDK archive,
so its original `btc_a2dp_source.c.obj` is not extracted. A strong reference from
`bt.cpp` to the project identity/stats also prevents a silent fallback if this
directory is missing. `tools/verify_a2dp_link.py` verifies source hashes, the
installed SDK hash, map ownership, original object exclusion, and the actual
linked allocation arithmetic/call site. Supported build/flash helpers require
this check to pass before proceeding. No `--allow-multiple-definition`, symbol
weakening, binary constant patching or system-wide SDK edits are used.

Normal builds need only the existing Arduino dependencies and Python 3. The
files here are committed, so no IDF checkout, CMake, ESP-IDF installation or
additional downloads are needed on Windows.

Developer regeneration (not part of a normal build):

```sh
git clone --depth 1 --filter=blob:none --sparse --branch v5.5.4 https://github.com/espressif/esp-idf.git /path/to/idf
# In that checkout: git sparse-checkout set components/bt
python3 tools/vendor_idf_a2dp.py --idf /path/to/idf --sdk-bt-include /path/to/esp32-libs/3.3.10/include/bt
python3 tools/verify_a2dp_link.py --sources-only
bash tools/test_idf_a2dp_packet.sh
```

Regeneration checks the exact commit and upstream C-file hash. Review all diffs
and rerun link/hardware acceptance before changing the pin. See
[build and acceptance instructions](../../../docs/chonkpod_a2dp_fix.md).
