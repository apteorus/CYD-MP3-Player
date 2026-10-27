// Project glue only; allocation sizing lives in patched btc_a2dp_source.c.
#ifndef CYD_UPLOADER
#include "../../bt_sdk_patch.h"
#include "osi/allocator.h"

static volatile uint32_t allocations, failures, last_bytes, min_bytes, max_bytes;
const char* chonkpod_a2dp_patch_id(void) {
  return "idf5.5.4-mtualloc-v1";
}
// Kept as a distinct symbol for post-link verification. The media task is the
// sole writer; loop reads approximate snapshots. Never print, wait or allocate
// any extra memory here. Preserve the SDK's allocator/free ownership contract.
__attribute__((noinline)) void* chonkpod_a2dp_packet_alloc(uint32_t bytes) {
  ++allocations;
  last_bytes = bytes;
  if (!min_bytes || bytes < min_bytes) min_bytes = bytes;
  if (bytes > max_bytes) max_bytes = bytes;
  void* p = osi_malloc(bytes);
  if (!p) ++failures;
  return p;
}
void chonkpod_a2dp_packet_stats(struct ChonkpodA2dpPacketStats* out) {
  out->allocations = allocations;
  out->failures = failures;
  out->last_bytes = last_bytes;
  out->min_bytes = min_bytes;
  out->max_bytes = max_bytes;
}
#endif
