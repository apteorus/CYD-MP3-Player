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
