#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
struct ChonkpodA2dpPacketStats {
  uint32_t allocations, failures, last_bytes, min_bytes, max_bytes;
};
const char* chonkpod_a2dp_patch_id(void);
void* chonkpod_a2dp_packet_alloc(uint32_t bytes);
void chonkpod_a2dp_packet_stats(struct ChonkpodA2dpPacketStats* out);
#ifdef __cplusplus
}
#endif
