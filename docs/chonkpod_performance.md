# Playback timing and artwork startup

## Audio investigation

The reported full ring, stable underrun count and persistent slowdown do **not**
establish SD starvation. Artwork-off behaving identically is additional evidence
against blaming artwork. No matching hardware capture is available in the cloud;
the persistent symptom remains a hardware validation item, not a proven fix.

The pinned ESP32 Arduino core 3.3.10 uses ESP-IDF v5.5.4. Its legacy A2DP source
`btc_a2dp_source.c` hardcodes incoming PCM to 44100 Hz, stereo, 16 bit
(`btc_a2dp_source_setup_codec`). It handles SBC feeding configuration itself.
Do not change the PCM rate to a sink's advertised SBC capability. ChonkPod keeps
44100 Hz PCM and records AUDIO_CFG and AUDIO_STATE events when provided by the
stack. A negotiated rate of zero means no configuration event was reported;
it is not an inferred negotiation failure.

Code findings and corrections:

* The old interpolator multiplied a signed sample difference up to 65535 by a
  16.16 fraction in **signed 32-bit arithmetic**. This overflows at high signal
  amplitudes, invoking undefined behavior/distorting resampled music. Use a
  64-bit product. This defect does not explain all later 44.1 kHz tracks slowing.
* Use an exact rational phase (44100 units) instead of a rounded fixed-point
  step, preserving output duration across all supported MP3 source rates.
  Reset phase/history on every track/seek and source-rate change. Interrupted
  output returns without subtracting from an unfinished phase.
* At transitions the old consumer started with any available PCM, even a few
  frames, then counted underruns while the decoder was still refilling. Prime
  512 frames (~12 ms) in the **existing** ring; allow shorter EOF fragments.
  This addresses transition starvation, not persistent Bluetooth timing.
* In-place Helix reset already clears reservoir, DSP blocks and wrapper state.
  The real decoder reset regression continues to compare against fresh decoders;
  no decoder replacement or buffer enlargement was needed.

Every five seconds `rate:` reports actual elapsed `dt` (milliseconds), decoded
source rate/channels, source frames/s, decoded media milliseconds in that window,
generated ring frames/s, requested pull frames/s, consumed music frames/s,
silence frames, callback count, producer wait milliseconds, reset count and
invalid PCM count. Counts continue across tracks, so track/seek windows include
intentional aborts/flushes; paused and reconnect windows are not steady-state
measurements. Counters use unsigned subtraction and tolerate wrap. PCM block
accounting can lead output by one decoder block. `timing:` reports lifetime
maximum callback gap/work and decode-write time (including ring-full waits),
plus counts of gaps over 20/50 ms in the latest window. Bluetooth pulls normally
arrive in batches; a single long gap alone does not demonstrate lost audio.
`ui-timing:` reports lifetime loop gaps and >50 ms loop count. Serial output runs
only in the loop task; audio callbacks update fixed-size counters only, never
print, allocate, or touch SD.

Interpret steady uninterrupted Bluetooth playback windows:

| Observation | Interpretation / next check |
|---|---|
| genfps/pullfps/usedfps near 44100; media_ms near dt; no silence | Upstream PCM duration is correct; check speaker/radio pauses downstream, using an external recording. |
| pullfps and usedfps persistently below 44100 with full ring and producer waiting | A2DP consumption/transport timing, not decoder starvation. Compare AUDIO_STATE/CFG and gap counts; reconnect test. |
| pullfps near 44100; usedfps lower; silence increasing | Starvation or intentional pause/transition; correlate starting/playing, SD and UI timings. |
| media_ms consistently differs from generated frames / 44.1 | Decoder/resampling mismatch; record source rate and reproduce with a known tone. |

True slow pitch needs a recording of a known 440 Hz tone and elapsed time;
pauses preserve tone pitch but reduce average frames delivered. A full ring alone
cannot distinguish them. Do not reset Bluetooth on every song as a speculative
workaround.

Memory: resampler has only phase/rate/history (12 bytes); fixed diagnostics cost
roughly a few hundred bytes, no additional heap buffers. Ring size, Helix
allocations, Bluetooth pairing and audio routing remain unchanged.

## Validation and hardware procedure

Host: `bash tools/test_pcm_resampler.sh` checks 9 rates (8–48 kHz), mono/stereo,
440 Hz crossings/output frame counts, chunk-size invariance, full-scale signed
arithmetic under ASan/UBSan and interrupted-output/reset recovery.
`HELIX_SOURCE=/path/to/arduino-libhelix/src bash tools/test_mp3_reset.sh` checks
100 in-place resets against fresh decoder PCM with no reset allocation/free.

Hardware tests are **pending**, not performed in this cloud environment. Do not
flash automatically. After a local build/flash:

1. Play known 44.1, 48, 32 and 22.05 kHz MP3 tones/tracks for at least 60 seconds
   each. Capture `rate:`, `timing:`, `bt-rate:`, `diag:` and `ui-timing:` lines.
2. Alternate rates across 30 manual track changes, album navigation, seeks,
   pause/resume; repeat with GIF, artwork and visuals off. Record source rate,
   first bad track and whether subsequent 44.1 kHz tracks remain affected.
3. If slowdown returns, capture at least three steady five-second windows before
   changing state. Compare actual time and 440 Hz pitch using an external audio
   recording. Then pause/resume and reconnect the same Jambox separately, noting
   which (if either) restores normal timing.
4. Confirm saved pairing/reconnect, onboard DAC routing, CJK titles, ID3 labels,
   touch/seek/layouts and GIF mode still work. Hardware heap/stack measurements
   remain necessary; linker totals are not runtime heap evidence.

## Artwork checkpoint (media4)

`/.sys/art/prepared` is a 16-byte ACP1 record containing format version,
library/folder-cover fingerprint, track count and integrity check. An unchanged
boot reads this record and **does not invoke storage_forEachInfo or visit track
references**. The existing music-directory scan and index validation still run;
this change removes the additional full artwork pass, not all startup SD work.
There is no whole-library RAM artwork manifest.

During the existing music scan, collect an order-independent checksum of
`cover.jpg` paths, sizes and modification times without opening any extra files.
Combine it with the actual library index CRC, which already includes music file
path/size/mtime and metadata. Added, removed or changed music/folder covers
invalidate the checkpoint. On that boot, scan the index sequentially and reuse
existing per-track reference signatures; only affected/missing covers need JPEG
extraction/conversion. Changed library traversal is still O(tracks), while an
unchanged artwork pass is O(1). Same-size, same-timestamp edits require manual
refresh, as do metadata edits invisible to the library index. Existing library
rescan requirements for such edits still apply.

A checkpoint is committed via a temporary file only after a complete successful
pass. Truncated/wrong-version/mismatched records and failed commits cause a safe
rescan. Existing per-track `.ref.busy` crash guards remain active; failures prevent
checkpoint commit and keep placeholders. These failed preparations conservatively
retry validation on later boots, but never retry a guarded JPEG decode. Bump ACP1
when changing preparation semantics, and the per-reference conversion version
when RGB conversion changes.

At track change, artwork still opens only a tiny reference and the cached RGB565
asset. Missing/corrupt references/assets invalidate the checkpoint for **next
boot**, with a placeholder now. No JPEG decoding occurs while Bluetooth runs.
The row-at-a-time artwork display and its stack buffers are unchanged.

**Setup → Refresh artwork** asks to restart, saves the resume position, and writes
a refresh request. Before Bluetooth starts on that next boot it invalidates the
checkpoint, removes track references, unsupported-picture markers and per-track
crash guards, then rechecks covers. Valid content-addressed RGB assets remain and
are reused. The request is consumed before JPEG decoding so a new crash cannot
create an automatic decode/reboot cycle. Manual retry of a repeatedly crashing
picture can still reset once; its new busy marker protects the following boot.
Only invoke refresh after changing/removing a known problematic image.
Alternatively create `/.sys/art/refresh` on the card while powered off. This does
not touch CJK/title caches, ID3 index, pairing, GIF files or music. New labels use
the existing English/Chinese fallback pipeline. The previously deferred branding,
loading-percentages and boot/autoplay behavior changes remain deferred.

Additional hardware tests (pending):

1. First media4 boot prepares/checkpoints; second unchanged boot prints
   `art: checkpoint current -- skipped full artwork traversal`. Compare boot
   elapsed time with 427 songs and check an already cached cover at track change.
2. Add/change/remove a track and a folder `cover.jpg`; confirm preparation occurs
   once, correct affected covers update, and the following boot skips again.
3. Remove/truncate one RGB asset: selecting that track shows a placeholder and
   invalidates the checkpoint. Reboot repairs it without decoding during playback.
4. Exercise Setup refresh and interrupted preparation; verify busy guards and
   existing CJK caches survive ordinary boots. Test a read-only/full SD card for
   graceful placeholder/checkpoint failures, not a restart loop.

Host media suites include the production checkpoint code: zero traversal on a
matching reboot, version/truncation rejection, changed fingerprints, index/
preparation failure, failed rename, manual recovery cleanup, and retained RGB
assets. Production storage tests verify folder-cover-only changes invalidate the
art fingerprint without rebuilding the music index. Existing parser, JPEG,
CJK/title recovery, cache and four-open-file-budget tests also run.

Cloud validation completed with ESP32 Arduino 3.3.10, TFT_eSPI 2.5.43,
ESP32-A2DP 1.8.11 and arduino-libhelix 0.9.4:

| Image | Flash bytes | Static RAM bytes |
|---|---:|---:|
| E32R28T player | 1,354,529 | 56,512 |
| E32R28T uploader | 1,149,433 | 48,980 |
| Original ST7789 player | 1,354,453 | 56,512 |
| Original ST7789 uploader | 1,149,365 | 48,980 |

All fit the existing 1,966,080-byte application slots. Relative to media3,
player static RAM increases **176 bytes**; uploader static RAM is unchanged.
All seven media suites passed ASan/UBSan, including the supplied 7,284-glyph
CJK font; resampler and mixed-rate real-Helix reset tests passed as well.
No compiler installation or hardware flashing was performed for these changes.

For local Windows compilation use the existing instructions in
[media update steps](chonkpod_media.md#updating-and-testing-on-windows), or from
the updated repository folder run:

```powershell
.\tools\flash_windows.ps1 -WithUploader -Python python
```

That command compiles both images without flashing. Firmware identifies itself
as `1.2.0-chonkpod-e32r28t-media4` on E32R28T. Capture hardware logs at 115200 baud
using `arduino-cli monitor --port COM4 --config baudrate=115200` after the local
update. The cloud builds cannot establish real-time Bluetooth performance or
mark the persistent slowdown resolved.

ESP-IDF source reviewed:
[btc_a2dp_source.c at v5.5.4](https://github.com/espressif/esp-idf/blob/v5.5.4/components/bt/host/bluedroid/btc/profile/std/a2dp/btc_a2dp_source.c),
particularly setup/feeding initialization and timer-driven frame consumption.
