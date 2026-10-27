#pragma once
#include <stdint.h>
#include <stddef.h>

// Fixed-size telemetry. No allocation, printing, or heap inspection
// in the feed callback or allocation-failure hook. One writer for feed stats;
// heap hook writers use separate core banks and mask local interrupts briefly.
class BtFeedStats {
 public:
  struct Snapshot {
    uint32_t calls, frames, bursts, seven, ten, eleven, other;
    uint32_t maxGapUs, maxWorkUs, failures, packetFailures, lastSize, lastCaps;
  };
  void pull(uint32_t nowUs, uint32_t frames) {
    if (havePrevious) {
      const uint32_t gap = nowUs - previousUs;
      if (gap > maxGap) maxGap = gap;
      // Observable callback group, NOT an assertion about an internal IDF tick.
      // A 5 ms gap separates normal ~30 ms SBC bursts from callbacks within one.
      if (gap >= 5000) {
        finishBurst(); burstCalls = 0;
      }
    }
    havePrevious = true; previousUs = nowUs; ++burstCalls;
    ++calls;
    framesTotal += frames;
  }
  void work(uint32_t us) {
    if (us > maxWork) maxWork = us;
  }
  // Caller must exclude interrupt reentry on this core. Different cores write
  // different banks: no cross-core spinlock or libatomic helper is needed.
  void allocationFailed(size_t size, uint32_t caps, unsigned core) {
    auto& bank = heap[core & 1];
    ++bank.failures;
    if (size == 4112) ++bank.packetFailures;
    bank.size = uint32_t(size); bank.caps = caps;
  }
  Snapshot snapshot() const {
    const auto& sample = heap[0].failures ? heap[0] : heap[1];
    return {calls, framesTotal, bursts, seven, ten, eleven, other, maxGap, maxWork,
            heap[0].failures + heap[1].failures, heap[0].packetFailures + heap[1].packetFailures,
            sample.size, sample.caps};
  }
 private:
  void finishBurst() {
    ++bursts;
    auto& bin = burstCalls == 7 ? seven : burstCalls == 10 ? ten : burstCalls == 11 ? eleven : other;
    ++bin;
  }
  bool havePrevious = false;
  uint32_t previousUs = 0, burstCalls = 0;
  volatile uint32_t calls{0}, framesTotal{0}, bursts{0}, seven{0}, ten{0}, eleven{0}, other{0};
  volatile uint32_t maxGap{0}, maxWork{0};
  struct HeapBank { volatile uint32_t failures=0, packetFailures=0, size=0, caps=0; };
  HeapBank heap[2];
};
