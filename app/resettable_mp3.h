#pragma once

#include "MP3DecoderHelix.h"
#include "libhelix-mp3/coder.h"

// Pinned to arduino-libhelix v0.9.4's software decoder layout. Its public
// end()/begin() frees and reallocates every buffer, including new[] buffers
// which can terminate the process when Bluetooth has fragmented the heap.
// Recreate the zeroed state of AllocateBuffers() in place instead. Do not
// flush old compressed audio: a seek/track change is explicitly discarding it.
// Only the decode task may call this, outside write()/the PCM callback.
class ResettableMp3 : public libhelix::MP3DecoderHelix {
 public:
  using libhelix::MP3DecoderHelix::MP3DecoderHelix;

  bool resetStream() {
    if (!active) return begin();  // first allocation, before Bluetooth startup
    if (!decoder) return false;
    auto* state = static_cast<MP3DecInfo*>(decoder);
    void* blocks[] = {state->FrameHeaderPS, state->SideInfoPS,
                     state->ScaleFactorInfoPS, state->HuffmanInfoPS,
                     state->DequantInfoPS, state->IMDCTInfoPS, state->SubbandInfoPS};
    for (void* block : blocks) if (!block) return false;
    memset(blocks[0], 0, sizeof(FrameHeader));
    memset(blocks[1], 0, sizeof(SideInfo));
    memset(blocks[2], 0, sizeof(ScaleFactorInfo));
    memset(blocks[3], 0, sizeof(HuffmanInfo));
    memset(blocks[4], 0, sizeof(DequantInfo));
    memset(blocks[5], 0, sizeof(IMDCTInfo));
    memset(blocks[6], 0, sizeof(SubbandInfo));
    memset(state, 0, sizeof(*state));  // reservoir, frame info and counters too
    state->FrameHeaderPS = blocks[0];
    state->SideInfoPS = blocks[1];
    state->ScaleFactorInfoPS = blocks[2];
    state->HuffmanInfoPS = blocks[3];
    state->DequantInfoPS = blocks[4];
    state->IMDCTInfoPS = blocks[5];
    state->SubbandInfoPS = blocks[6];
    frame_buffer.reset();
    memset(pcm_buffer.data(), 0, maxPCMSize());
    memset(&mp3FrameInfo, 0, sizeof(mp3FrameInfo));
    frame_counter = 0;
    parse_0_count = 0;
    time_last_write = time_last_result = 0;
    return true;
  }
};
