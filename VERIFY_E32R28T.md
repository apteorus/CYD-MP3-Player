# E32R28T source verification record

Base: `apteorus/CYD-MP3-Player` at
`404332e93974f4f70392b613c9f3dfb25e1b89b8` (initial local branch `work`).
Feature branch: `feature/chonkpod-e32r28t`.
The brief names Garionhk upstream; work uses the user-selected apteorus checkout.
Manufacturer pinout is taken from the supplied brief and linked reference;
PCB revision and electrical behavior still require the user's confirmation.

## Changes

- `config/board.h` and eight synchronized sketch copies: distinct selectable
  `CYD_BOARD_LCDWIKI_E32R28T`, independent red22/amp4, battery ADC34 capabilities,
  panel defaults, board banner, and amplifier shutdown preloaded before output.
- `app/app.ino`: explicit amp shutdown at the start of setup in both images;
  matching E32R28T TFT setup marker required; clearer driver errors and MISO/reset
  pin checks. Five LED-parking Stage 0 sketches also explicitly mute at startup.
- `app/settings.h`: E32R28T first-boot OUT_BT; original OUT_SPEAKER and persisted
  route behavior retained. `app/firmware.h`: target-only ChonkPod version suffix.
- `app/speaker.cpp` / `.h`: correct comments for independent and shared pins;
  DAC ramp, route handoff and shutdown sequence unchanged.
- E32R28T TFT_eSPI template: ILI9341 standard init candidate, HSPI, 40 MHz;
  explicit unverified color/init defaults and alternative init guidance.
- `tools/flash_windows.ps1`: sync; identical board flags in C/C++ for both images;
  separate caches; min_spiffs; exit-code checks; source record; explicit opt-in
  flashing with COM port; uploader only at 0x1F0000. No factory erase.
- `README_E32R28T.md` and README link: Windows prerequisites, compile/flash commands,
  provenance, licensing and manual bench checklist.

## Executed source-only checks

- `git diff --check`: passed.
- `bash -n tools/sync_shared.sh tools/build_all.sh tools/flash.sh`: passed.
- Ran `tools/sync_shared.sh`; byte comparisons confirmed all eight sketch headers
  exactly match `config/board.h`.
- Python text/byte assertions against the base commit confirmed all three original
  panel profile bodies and global default are unchanged. Common TFT/touch/SD pin
  definitions remain unchanged in the diff.
- Byte comparisons confirmed audio.cpp, bt.cpp, storage.cpp, touch.cpp,
  display.cpp, settings.cpp, playlist.cpp, uploadmode.cpp and upload_page.h are
  unchanged. This preserves source behavior, not proof of runtime compatibility.
- Source assertions checked template pins, geometry, HSPI and target marker,
  both-image Windows board flags, opt-in flash block, uploader offset, absence
  of full-chip erase commands, and distinct red/amp/battery definitions.
- Searched app/config/stage0/tools for board comparisons, LDR/LED/amp symbols,
  GPIO4/22/34 and direct GPIO writes. No application LDR consumers were found;
  GPIO34 capability is named without adding measurement or conversion code.
  Historical s06/s07 speaker investigations intentionally remain historical;
  their pin-sweep sketches are not E32R28T bring-up recommendations.
- Inspected existing stereo flow: audio.cpp retains left/right interleaved
  samples, bt.cpp supplies those frames to A2DP. No decoder/radio edits or fixed
  peer identities were introduced. Stereo playback still needs a physical test.

## Not performed / risks

No compiler/toolchain installation, board-package downloads, firmware builds,
uploads or hardware tests were performed for this source-port task. Tooling left
from the earlier environment-onboarding task was not used to build firmware.
No binaries are part of the changes.

PowerShell is unavailable here: its script was reviewed as source, not parsed
or executed by PowerShell. Windows command behavior, ESP32 core 3.3.10 API
compatibility, linking, slot sizes and original-board builds remain unverified.
The new driver/template marker and pin guards must be exercised by local builds.
The standard ILI9341 versus ILI9341_2 init, BGR order, inversion OFF, rotation1
and SPI speed need panel bench checks; both images must use the same setup.
Saved NVS display and audio preferences override defaults; settings are not erased.

Follow the USB-first checklist in README_E32R28T.md for panel/touch/SD, stereo
A2DP, discovery/reconnect/AVRCP, local mono route and amp shutdown, uploader
transitions, and all original profiles. Battery divider/calibration, battery
wiring and MH-M38 hardware tests remain pending. Do not interpret source checks
as compile-time or hardware validation.

Apache-2.0 project license retained. Preserve upstream attribution and third-party
library/vendor licenses and notices when distributing a derived build. No binary
redistribution is included in this task.

## Subsequent playback/seek investigation

The earlier sections record the initial source-only handoff. Both firmware images
were subsequently built on Linux, and the user also reported successful Windows
builds and multi-track playback with a Jawbone Mini Jambox.

The supplied crash occurred on seeking. Resolving its addresses against our Linux
ELF maps the abort to C++ termination during Helix Vector allocation, reached via
CommonHelix::begin → resetDecoder → seekTo/decodeTask. The Windows binary differs
slightly, so exact symbol confirmation requires that build's ELF; this is strong
supporting evidence rather than an exact-ELF crash decode. Steady playback shows
stable heap and no rising underruns within tracks; the log does not establish a
board-pin or receiver compatibility fault.

`app/resettable_mp3.h` replaces end/begin churn with an in-place reset matching
v0.9.4 AllocateBuffers' zero initialization while retaining all allocations.
The wrapper clears compressed input, PCM scratch, frame counters, full MP3 state
and all seven DSP blocks, including reservoir and synthesis history. It does not
flush old-position audio. `audio.cpp` resets resampling state and checks failure.
The local E32R28T version suffix is `-seek1` for identifying the update.

Regression runner: `HELIX_SOURCE=/path/to/arduino-libhelix/src bash tools/test_mp3_reset.sh`.
Uses the actual pinned decoder sources and an ffmpeg-generated stereo MP3, no
mock decoder: 100 resets allocate/free nothing and match the PCM hash/sample count
of a newly initialized decoder at three byte offsets. Test logging shims only
replace Arduino logging macros absent on the host; assertions remain enabled.
The player and uploader are compiled again on Linux. Bench verification of
repeated seeking, track changes, pause/reconnect and original-board playback is
still required; there is no claim of a hardware-confirmed fix.

Metadata findings: the log reports 18 folders and a missing CJK title font.
Album artwork is not implemented. Non-Latin titles may be read correctly but
invisible with the ASCII fallback. No speculative ID3 changes were made; an
affected MP3 is needed if installing the font does not resolve missing titles.

## Media revision (supersedes earlier font/art limitations)

The font/index/art changes and their exact memory/failure contracts are documented
in docs/chonkpod_media.md. Separate commits cover streamed font recovery, extended
ID3/index migration, and the artwork pipeline. The user font is structurally valid;
unchecked full glyph-table allocations were an unsafe original path, but the
original hardware reset was not symbolicated because its boot log was unavailable.

Current validation includes six production-code host suites with ASan/UBSan and
both firmware images for E32R28T and the original default ST7789 profile. The
MP3 decoder and ring implementation were not modified by the media work. Hardware
validation of this revision remains pending; prior successful playback/seek tests
predate these media changes. No device was flashed by the cloud agent.
