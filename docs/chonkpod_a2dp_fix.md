# media5: A2DP packet-allocation fix

The media4 diagnostic hardware capture confirmed the mechanism: healthy playback
had no allocation failures and 10/11-callback groups, then each track change led
to ~166–167 seven-callback groups **and 4112-byte allocation failures** per five
seconds. It persisted with a full PCM ring, while paused, and without a power-mode
change. This is a confirmed SBC packet-production allocation bottleneck, not
MP3 starvation or a negotiated sample-rate change.

Media5 applies the allocation fix to the **actual SDK source component linked
into the firmware**. Hardware acceptance of the new media5 binary remains pending;
cloud builds/tests do not establish the physical Jambox result.

## Implementation and preserved behavior

`app/src/idf_a2dp/btc_a2dp_source.c` is pinned to ESP-IDF 5.5.4, commit
`735507283d5b2f9fb363a1901172dbd9e847945d`, with its required private headers and
notices. Arduino compiles it as a sketch source object. It supplies the same SDK
entrypoints, so the linker's normal archive selection excludes the original
`libbt.a(btc_a2dp_source.c.obj)`. All other SDK code, including the SBC encoder,
remains the installed implementation. The original archive is neither modified
nor copied/patched. There is no runtime global allocator replacement.

Packet storage is now:

```
min(4112, 8-byte BT_HDR + 24-byte offset + max(negotiated MTU, 1024))
```

For MTUs up to 1024 this is **1056 bytes instead of 4112**, saving 3056 bytes of
transient heap **per packet**. Larger negotiated MTUs receive enough storage up
to the existing limit. The minimum safely accommodates the first SBC frame even
if a small MTU is below that frame's size. Standard SBC frames are at most 524
bytes. The original MTU limit, packetization loop, codec setup, bitpool/bitrate,
encoder, PCM format, timer, queues, timestamps and free/ownership paths are
unchanged. There is no reconnect-per-song workaround or larger PCM buffer.

A small noinline allocation delegate retains SDK `osi_malloc` behavior and
collects allocation size/failure counts. Its distinct symbol makes the real
packet path auditable. All existing diagnostics remain. Added serial output:

```
bt-sdk: idf5.5.4-mtualloc-v1 (project-local component)
bt-packet: allocations=... failures=... last=1056 min=1056 max=1056 bytes
```

Counters are cumulative/approximate snapshots. The media task updates them
without printing, waiting, reserving buffers or allocating extra memory. A larger
negotiated MTU can legitimately produce a larger `last`/`max`; 4112 is still a
valid upper bound, not a globally forbidden allocation size.

## Reproducibility and proof of the linked fix

Normal Windows builds use the committed source/header snapshot and the already
installed Arduino ESP32 **3.3.10 / ESP-IDF 5.5.4** toolchain. No IDF installation,
CMake, new compiler, source download or custom Arduino platform file is needed.
An unsupported Arduino/IDF version, target or A2DP mode fails compilation.
The official SDK archive is pinned to SHA256
`d500529efd1d391492a4e5798f39f722e3bf0bbc359ff7c819bce56b1ca7141f`.
Custom rebuilt cores with the same version number are intentionally unsupported
until their ABI/configuration is reviewed.

`tools/verify_a2dp_link.py` fails closed unless all these checks pass:

1. Pinned source/private-header hashes match the manifest; the compiled source
   copies match (allowing Arduino's initial `#line` directive).
2. The installed SDK archive has the expected hash.
3. The final map assigns A2DP startup, codec setup and feeding to the sketch's
   imported component; the original archive member is absent.
4. The component object references the patched allocator, and the final ELF
   actually calls it. Linked Xtensa instructions show the **1056-byte floor,
   dynamic MTU+32 calculation and upper clamp**, not the old fixed argument.
5. The allocator delegates to the original SDK allocator; its identity/stat
   symbols remain in the final ELF.

Each verified build writes `a2dp-link-audit.json` (ELF/source/object hashes and
call sites) and `a2dp-allocator-disassembly.txt`. The Windows helper copies them
beside the player image. It stops before any flash if verification fails. The
Unix build/flash/release helpers perform the same player check. Arduino IDE or
a bare CLI compile still compiles the component automatically, but use the
verified helpers for update/review artifacts.

The SDK snapshot uses enforced LF line endings so Git's Windows checkout does
not invalidate its hashes. The developer-only import tool checks the upstream
commit/hash and reproduces the relative include rewrites and patch. Detailed
provenance and regeneration steps are in `app/src/idf_a2dp/README.md`.

## Exact Windows update, build and flash

Use PowerShell in the existing repository. Keep the current E32R28T TFT setup
and libraries; no wiring, SD cache, calibration, pairing or partition changes
are needed. Existing original-flash backup should remain stored separately.

```powershell
Set-Location "$HOME\CYD-MP3-Player"
git fetch origin
git switch feature/chonkpod-e32r28t
git pull --ff-only origin feature/chonkpod-e32r28t
arduino-cli core list
```

The core list must show `esp32:esp32` **3.3.10**. If another version is installed,
select the required version locally before building:

```powershell
arduino-cli core install esp32:esp32@3.3.10
```

Compile both images and verify the player link, without flashing:

```powershell
.\tools\flash_windows.ps1 -WithUploader -Python python
```

Look for:

```
PASS: project-local IDF A2DP entrypoints and dynamic packet allocator verified in final ELF; original SDK object absent
```

Review these files:

```
app\build\e32r28t\player\a2dp-link-audit.json
app\build\e32r28t\player\a2dp-allocator-disassembly.txt
app\build\e32r28t\player\source.txt
```

Stop the serial monitor with **Ctrl+C** so COM4 is free. To build, re-verify and
flash locally using the existing uploader slot:

```powershell
.\tools\flash_windows.ps1 -WithUploader -Flash -Port COM4 -Python python
```

The cached rebuild is intentional: the flash helper verifies the firmware again
before writing. Player upload uses the existing partition layout; uploader goes
to `0x1F0000`. No erase-all or merged factory-reset image is used, and NVS is
preserved. These are **local instructions**; cloud hardware flashing was not done.

Then capture logs:

```powershell
arduino-cli monitor --port COM4 --config baudrate=115200
```

Confirm banner `1.2.0-chonkpod-e32r28t-media5`, the `bt-sdk` identity above, and
`bt-probe: allocation hook=0`. `bt-packet` reports zero values until the first
packet is produced. Avoid deleting pairing for the first test: ordinary reconnect
is part of acceptance. PowerShell itself is unavailable in the Linux cloud;
the updated script was reviewed but has not been executed on Windows here.

## Host regressions and hardware acceptance

Offline host commands (existing GCC/G++, Python/Pillow and Helix checkout):

```sh
bash tools/test_idf_a2dp_packet.sh
bash tools/test_bt_feed.sh
bash tools/test_pcm_resampler.sh
HELIX_SOURCE=/path/to/arduino-libhelix/src bash tools/test_mp3_reset.sh
MEDIA_PYTHON=/path/to/python-with-Pillow MEDIA_SANITIZE=1 bash tools/test_media.sh
python3 tests/test_a2dp_integration.py --player-build /path/to/player-build --uploader-build /path/to/uploader-build
```

The packet regression extracts the **actual integrated SDK function** and
compiles the real C allocation delegate. It checks the upstream function hash
and compares original, proposed and integrated implementations. Under the
one-large-packet heap constraint, original consumption is **29866.7 frames/s**;
integrated consumption is **44088.9 frames/s** (whole SBC-block rounding over nine
simulated seconds). ASan/UBSan check allocation bounds across MTUs 48–4080 and
SBC frames up to 524 bytes. Packet payloads, timestamps and grouping match the
original when allocation succeeds. Flush/underflow behavior and real allocator
failure telemetry also pass. These are controlled host tests, not radio tests.

The integration tests reject corrupted private headers and an uploader ELF
presented as a patched player, exercise Windows path/line-ending handling, and
use the real target compiler to reject an unsupported Arduino version. Existing
feed/resampler/decoder and CJK/metadata/JPEG/cache-recovery tests remain required.

Hardware acceptance, **pending local testing**:

1. Keep the full serial log from boot. Require normal steady PCM consumption
   within 1% of 44100 over 30-second windows, no persistent seven-callback regime,
   and no repeated 4112-byte allocation failures. Check `bt-packet` failure count
   and actual sizes in addition to the general heap hook.
2. Perform at least **30 consecutive track changes**, with a few natural EOF
   transitions, across albums and mixed MP3 sample rates. Keep enough playing
   time between changes for steady diagnostic windows. Observe no lasting rate
   reduction after any change; brief deliberate transition silence is separate.
3. Pause for 10 seconds, 5 minutes and 30 minutes. Silent BT feed should remain
   near normal; resume must retain normal rate. Seek forward/back at several
   positions and repeat track changes afterwards.
4. Power-cycle/reconnect the Jambox without forgetting pairing, and test
   disconnect/reconnect several times. Check saved peer identity and AVRCP keys.
5. Use CJK titles, album browsing, metadata, artwork and GIF modes, touch/seek and
   onboard speaker routing normally. Check heap minima, largest block and stack
   headroom; no resets or accumulating packet failures. Repeat with another BT
   speaker if available. No UI/layout/MP3-ring changes are part of this fix.

## Completed cloud build evidence

Tested toolchain/dependencies: Arduino CLI 1.3.1, Arduino ESP32 3.3.10,
ESP-IDF 5.5.4, TFT_eSPI 2.5.43, ESP32-A2DP 1.8.11 and arduino-libhelix 0.9.4.
All eight firmware variants passed compilation and existing application-slot
limits (1,966,080 bytes). Every player passed the final map/disassembly audit;
every uploader was checked to contain neither the patch allocator nor A2DP
source startup symbols.

| Board profile | Image | Flash bytes | Static RAM bytes |
|---|---|---:|---:|
| LCDWiki E32R28T | Player | 1,357,369 | 56,708 |
| LCDWiki E32R28T | Uploader | 1,149,433 | 48,980 |
| Original 2.8-inch ST7789 | Player | 1,357,289 | 56,708 |
| Original 2.8-inch ST7789 | Uploader | 1,149,365 | 48,980 |
| Original 2.8-inch ILI9341 | Player | 1,357,385 | 56,708 |
| Original 2.8-inch ILI9341 | Uploader | 1,149,477 | 48,980 |
| Original 2.4-inch ILI9341 | Player | 1,357,369 | 56,708 |
| Original 2.4-inch ILI9341 | Uploader | 1,149,461 | 48,980 |

E32R28T player static RAM is **+28 bytes versus btprobe1** (+196 versus plain
media4), with no added persistent heap buffer. Packet heap requirements decrease
by up to 3056 bytes per packet for small MTUs. The additional delegate stack is
small and bounded; runtime heap/stack headroom remains a hardware acceptance item.
`bt-packet`'s `allocations` counter counts attempts, including failures.

The all-profile matrix exposed a pre-existing omission in the legacy 2.4-inch
TFT template: HSPI pins were declared but `USE_HSPI_PORT` was absent, contrary
to the player's existing compile-time requirement. Adding that one define makes
the template select the already intended bus; no pins were changed. Its player
and uploader then passed. This compatibility correction is committed separately.

Host results: integrated packet/allocator tests passed ASan/UBSan, including
wire-payload/timestamp equivalence and constrained-heap rate recovery. All five
integration tests passed. Existing BT feed, nine-rate resampler, 100 mixed-rate
Helix-reset and all seven font/title/ID3/JPEG/cover/index/checkpoint suites passed.
An independent temporary regeneration reproduced all 54 pinned SDK/license file
hashes. The installed `libbt.a` retained its original SHA256 after all builds.
PowerShell execution and physical acceptance are pending, not claimed as passed.
