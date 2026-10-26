#pragma once
#include <stdint.h>
#include <stddef.h>

// No allocations. One instance, owned only by the decoder task.
class PcmResampler {
 public:
  void reset() { phase = rate = 0; prevL = prevR = 0; }
  template<class Emit>
  bool process(const int16_t* pcm, size_t samples, int channels, uint32_t hz, Emit emit) {
    if ((channels != 1 && channels != 2) || hz < 8000 || hz > 48000 || samples % channels) return false;
    if (rate != hz) { reset(); rate = hz; }
    for (size_t i = 0; i < samples / channels; ++i) {
      int16_t l = pcm[i * channels], r = channels == 2 ? pcm[i * channels + 1] : l;
      if (hz == 44100) {
        if (!emit(l, r)) return false;
      } else {
        while (phase < 44100) {
          // Full-scale opposite samples overflowed the old signed 32-bit product.
          const uint32_t fraction = phase * 65536U / 44100;
          int16_t ol = prevL + ((int64_t(l - int32_t(prevL)) * fraction) >> 16);
          int16_t oright = prevR + ((int64_t(r - int32_t(prevR)) * fraction) >> 16);
          if (!emit(ol, oright)) return false; // never subtract from an aborted phase
          phase += hz;
        }
        phase -= 44100;
      }
      prevL = l; prevR = r;
    }
    return true;
  }
 private:
  uint32_t phase = 0, rate = 0;
  int16_t prevL = 0, prevR = 0;
};
