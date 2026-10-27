# Persistent A2DP rate reduction: media4 investigation

Historical record of the **media4-btprobe1 diagnostic/proposal phase** below.
Subsequent hardware results confirmed that seven-callback groups correspond
almost one-for-one to 4112-byte allocation failures. The allocation fix is now
integrated in media5; see [implementation and current build/flash instructions](chonkpod_a2dp_fix.md).
The new binary still needs local hardware acceptance. Statements about pending
confirmation or an unapplied proposal below describe the earlier investigation.

## What the supplied capture establishes

The fresh pairing session contains two healthy windows with 1726 callbacks per
~5 seconds (`pullfps` 44176/44159). Immediately after its first manual track
switch, this settles to 1162–1169 callbacks per ~5 seconds (`pullfps` ~29900),
and remains there even when paused. Steady playing windows have zero silence,
99% ring fill and no growing underrun count. Source and reported SBC rates stay
44100; the decoder spends ~4.3 seconds of each five-second window waiting for
ring space. No new AUDIO_CFG or AUDIO_STATE event accompanies the rate drop.
Across the full capture there are 86 rate windows: two healthy windows, 60
windows between 29000 and 31000 requested frames/s, and 43 fully paused windows
in that slow range. The initial segment shows it in an older session as well.

A long pause is **not required** to trigger this capture. The second session's
maximum callback gap is only ~28 ms after the transition; maximum callback work
is 54 us initially and later 98 us. Neither an MP3 decoding bottleneck nor
application callback blocking explains the subsequent sustained rate.

Both healthy and bad windows have ~166–167 gaps over 20 ms per five seconds:
the observable burst cadence remains ~33.3 Hz, consistent with IDF's **30 ms**
media tick. What falls is the number of callbacks/PCM frames **within** a burst:
~10.4 callbacks normally versus ~7.0 afterwards. At 128 PCM frames per callback,
seven per 30 ms predicts **29,866.7 PCM frames/s**, closely matching the capture.
A reported 44100 Hz codec does not require the source to deliver PCM at that rate
when its packet-production path is failing or being throttled. Reduced average
delivery can produce gaps at the speaker; it does not by itself prove that the
instantaneous decoded tone pitch or physical codec clock has changed.

## Strong candidate: SBC packet allocations, not the PCM ring

Reviewed ESP-IDF v5.5.4 `btc_a2dp_source.c` and disassembled the actual
`btc_a2dp_source.c.obj` from Arduino ESP32 3.3.10's `libbt.a`:

1. `btc_get_num_aa_frame()` computes the work due on a tick, limits it by the
   output queue capacity and subtracts that work from its feeding counter.
2. `btc_media_aa_prep_sbc_2_send()` allocates **4112 bytes for each outgoing SBC
   packet**, even when the negotiated MTU needs far less storage. It reads PCM
   and encodes multiple SBC frames into that packet before queueing it.
3. If a subsequent packet allocation fails, it immediately returns. The remaining
   scheduled PCM work is **not restored** (unlike the PCM-underflow branch).
   Fewer PCM callbacks occur on that tick, with no audio-state/configuration
   change and no ChonkPod ring underrun.
4. If only one packet fits at a time, a seven-frame packet ceiling produces the
   observed seven callbacks per tick indefinitely. Pause/resume changes PCM to
   silence but does not repair allocation availability. Reboot/re-pair resets the
   heap and queues as well as the connection, so recovery is consistent with this
   mechanism; it does not prove the radio clock was reset incorrectly.

The largest free block drops from 11764 to 8180 around the first track change.
Two 4112-byte allocations need **8224 bytes before heap overhead**, so a free
heap total near 22 KB can still be insufficient for the packet burst. However,
this snapshot does **not** prove allocation failure: blocks elsewhere may satisfy
it and the snapshot is outside the failing instant. The exact Jambox MTU and
packet boundaries have not been captured either. Seven-frame packets are a
source-level reproduction assumption, not a measured Jambox configuration.

This remains a **strong, testable candidate**, not a confirmed device root cause.
Transport backpressure can also reduce work per tick through
`27 - fixed_queue_length(TxAaQ)`, without a configuration/state event. Link power
mode changes can happen without either A2DP event. Neither should be ruled out
until allocation-failure telemetry is captured.

## Track transition audit

`requestFlush()` communicates only with `audio_pull()` using a flush flag; it
waits in the **decode task**, not the BT callback. The callback resets the PCM
tail and position counter. `openTrack()` and seeking reset Helix/resampler state
and read SD/index data in that task; priming only selects whether to copy PCM or
return silence. They do not suspend/start A2DP, change its timer, set a codec
rate, or tell IDF to return fewer frames. Callback return length remains the
full request. The independent BT feed probe counts requests even when the
onboard-speaker route makes A2DP output silence.

There are allocation/SD/NVS operations around a transition, so it can expose
heap fragmentation or a transient scheduling delay. The capture does not yet
identify which allocation, if any, changes the heap layout. Do not blame CJK,
artwork, the resampler or a particular file based solely on this correlation.

## Diagnostic changes (no audio behavior change)

Every five seconds, from the loop task:

* `bt-feed`: independent callback frame rate/count, completed callback groups,
  groups of exactly 7/10/11 callbacks, other groups, lifetime maximum gap/work.
  A group is delimited by a gap **>=5 ms**, not a direct observation of an IDF
  timer tick or SBC packet. One group may straddle a reporting window.
* `bt-heap`: allocation failures in that window, failures requesting exactly
  4112 bytes, lifetime 4112 failures, one recorded failure size/capability sample,
  and current `MALLOC_CAP_DEFAULT` free/largest block. Size alone is not caller
  identification; correlation with seven-call groups provides stronger evidence.
* `bt-link`: GAP power mode, interval in 0.625 ms slots and event count. Mode 255
  means no event reported, not a diagnosed power mode. The last event may be
  from connection negotiation; compare event-count changes with rate changes.
* `transition`: track serial, flush count/fallback count, maximum flush duration,
  and last open/seek duration, including failure paths. A fallback while no
  consumer is connected at boot is expected.

No new logs, heap queries, allocation or waiting occur in the PCM callback or
failed-allocation hook. The hook uses a separate fixed counter bank per core and
briefly masks **local** interrupts around a few integer updates to exclude
interrupt reentry; it does not take a cross-core spinlock. Feed counters have a
single callback writer, naturally aligned 32-bit storage and read-only snapshots;
window boundaries are approximate. No `std::atomic` RMW/libatomic helper is used
on ESP32 (the target compiler does not guarantee lock-free 32-bit RMW).
The hook is registered before BT starts; `bt-probe: allocation hook=0` indicates
success. ESP-IDF has one failed-allocation hook; this repository has no other
registrant. Preserve/compose it if a future component also needs that hook.

## Proposed fix, before broader integration

The reviewable proposal is
[esp-idf-5.5.4-a2dp-packet-allocation.patch](patches/esp-idf-5.5.4-a2dp-packet-allocation.patch).
It changes **one allocation site**, not timers, codec configuration, MP3 buffers,
SBC quality, queue limits or packet timestamps:

Allocate header/offset + max(negotiated MTU, 1024 payload bytes), capped at the
original 4112-byte allocation. The 1024-byte floor covers even a first SBC frame
larger than a small MTU (standard SBC maximum is 524 bytes); the encoding loop
always emits its first frame before testing the next-frame MTU bound. At a small
MTU this needs **1056 bytes instead of 4112**, reducing transient memory use and
contiguous-allocation requirements. Large negotiated packets retain sufficient
space up to the existing maximum. Actual packet sizes/codec parameters do not
change. This does **not** add a reserved packet pool or permanent large buffer.

The patch is **not applied to the installed Arduino SDK or shipped probe**.
Arduino supplies this code in a precompiled archive, so editing a `.c` file in a
checkout or adding a sketch `#define` would not change it. Once the next capture
confirms this path, integrate through a reproducible, pinned rebuild of the BT
component/core (or a carefully validated component override), verify the linked
allocation code, then run the hardware regressions below. Do not blindly patch
binary constants, replace global allocators, or reconnect on every song.

If `fail4112` stays zero through the bad windows, **do not promote this patch as
the device fix**. The next necessary step is instrumenting IDF's scheduled frame
count, TX queue length and allocation outcome per media tick to distinguish
transport throttling from a media timer/accounting defect. That requires a
component rebuild, and is broader than the current application-only probe.

## Regression evidence

`bash tools/test_bt_feed.sh` runs production telemetry with normal 10/11-call
bursts followed by seven-call bursts, silent/paused feed accounting, `micros()`
wraparound and concurrent failure updates in the two core banks (ASan/UBSan).

The packet test compiles the **actual upstream packet-preparation function**,
unmodified and with the proposal, using a deterministic heap/queue/encoder host
harness. It does not substitute a model for that function. It does substitute
host heap/transport and encoder output, so it cannot validate radio scheduling.

```sh
curl -fsSL https://raw.githubusercontent.com/espressif/esp-idf/v5.5.4/components/bt/host/bluedroid/btc/profile/std/a2dp/btc_a2dp_source.c -o /tmp/btc_a2dp_source.c
bash tools/test_idf_a2dp_packet.sh /tmp/btc_a2dp_source.c
```

The script checks source SHA256
`ad71c76d27121f34ac3111ae3d9fcfd39aef25c4bad42f21231c6a0f4a692a4b`,
applies the proposal only in a temporary directory and refuses mismatched source.
It never modifies the installed SDK. Results over nine simulated seconds:

| Real IDF function / controlled heap | PCM frames/s |
|---|---:|
| Unpatched, sufficient contiguous packet space | 44088.9 |
| Unpatched, only one 4112-byte packet fits; MTU 900 | 29866.7 |
| Proposed allocation, same constrained heap/MTU | 44088.9 |

Counts reflect whole 128-frame SBC blocks, hence the small rounding difference
from 44100. ASan/UBSan also check multiple MTUs through 4080, typical and maximum
standard SBC frame lengths, unchanged TX flush behavior, and unchanged PCM
underflow credit restoration. Tests show that this allocation path **can** cause
and fix precisely this rate regime; they do not show that the Jambox took it.

## Local hardware capture and acceptance

No hardware is accessible from this cloud environment; no flash was performed.
Compile the updated branch on Windows with the existing command:

```powershell
.\tools\flash_windows.ps1 -WithUploader -Python python
```

This command compiles only. After the local update, use
`arduino-cli monitor --port COM4 --config baudrate=115200` and retain the banner
and `bt-probe:` hook-registration line. No additional re-pairing is necessary
unless the device will not reconnect normally.

First capture: allow two healthy five-second windows, switch tracks once, retain
at least three bad windows, then pause for two windows and resume for two. Keep
`bt-feed`, `bt-heap`, `bt-link`, `transition`, `rate`, `timing` and `bt-rate` lines.
A jump to seven-call groups plus repeated 4112-byte failures strongly confirms
the candidate. Zero such failures demands the alternative IDF queue/timer probe.

After a confirmed fix is integrated, acceptance requires:

1. At least 30 manual and natural track changes with 44100 Hz PCM consumption
   within 1% over steady 30-second windows; no persistent seven-call ceiling.
2. Pause for 10 seconds, 5 minutes and 30 minutes: silent BT feed remains normal,
   then resumed PCM remains normal. Repeat seeking at several positions.
3. Disconnect/reconnect the same Jambox repeatedly without deleting pairing;
   confirm correct rate and state, saved peer identity and AVRCP controls.
4. Repeat transitions under normal UI use with GIFs, CJK titles and existing
   artwork modes; check peak allocation/stack headroom. No enlarged audio ring.
5. Test another speaker and the onboard DAC route; no decoder, artwork, CJK,
   pairing, SD browsing or touch regressions.

Source references (Apache-2.0 IDF code; notices remain in upstream source):
[media feed/packet/timer implementation](https://github.com/espressif/esp-idf/blob/v5.5.4/components/bt/host/bluedroid/btc/profile/std/a2dp/btc_a2dp_source.c),
[OSI allocation behavior](https://github.com/espressif/esp-idf/blob/v5.5.4/components/bt/common/osi/allocator.c).

## Build validation of the diagnostic firmware

All four builds passed using the existing toolchain (no compiler installation):

| Diagnostic image | Flash bytes | Static RAM bytes |
|---|---:|---:|
| E32R28T player | 1,356,237 | 56,680 |
| E32R28T uploader | 1,149,433 | 48,980 |
| Original ST7789 player | 1,356,165 | 56,680 |
| Original ST7789 uploader | 1,149,365 | 48,980 |

All fit the existing 1,966,080-byte slots. Player static RAM is +168 bytes versus
media4; uploader static RAM is unchanged. No new heap buffers, no changes to the
ring/decoder/resampler/artwork, and no SDK binary modifications. Feed/proposed
packet tests passed ASan/UBSan; existing resampler and 100 mixed-rate/channel
Helix reset regressions passed. Hardware confirmation and SDK integration remain
pending as described above.
