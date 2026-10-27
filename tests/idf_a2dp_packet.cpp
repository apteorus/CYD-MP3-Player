// Host harness around the ACTUAL IDF v5.5.4 packet-preparation function.
// Codec output, heap and queues are controlled; this is not a radio benchmark.
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstddef>
using UINT8=uint8_t; using UINT16=uint16_t; using UINT32=uint32_t;
using std::min; using std::max;
#define TRUE true
#define FALSE false
#define BTC_MEDIA_AA_BUF_SIZE (4096+16)
#define BTC_MEDIA_AA_SBC_OFFSET 24
#define FIXED_QUEUE_MAX_TIMEOUT 0
#define APPL_TRACE_ERROR(...) ((void)0)
#define APPL_TRACE_WARNING(...) ((void)0)
#define APPL_TRACE_DEBUG(...) ((void)0)
struct BT_HDR { uint16_t event,len,offset,layer_specific; };
static_assert(sizeof(BT_HDR)==8);
struct Queue { BT_HDR* items[64]{}; unsigned count=0; } queue;
struct Encoder {
  int s16NumOfSubBands=8,s16NumOfBlocks=16,s16NumOfChannels=2;
  uint16_t u16PacketLength=119;
  uint8_t* pu8Packet=nullptr;
  int16_t as16PcmBuffer[256]{};
};
struct State {
  Encoder encoder;
  Queue* TxAaQ=&queue;
  uint16_t TxAaMtuSize=900;
  uint32_t timestamp=0;
  bool tx_flush=false,is_tx_timer=true;
  struct { struct {uint32_t counter=0;int aa_feed_residue=0;} pcm; } media_feeding_state;
  struct { struct { struct {int num_channel=2,bit_per_sample=16;} pcm; } cfg; } media_feeding;
};
struct { State btc_aa_src_cb; } a2dp_source_local_param;
struct alignas(max_align_t) Allocation {size_t size;};
static size_t budget=8180, live=0, maxRequest=0;
static unsigned failures=0, reads=0;
static bool underflow=false;
static BT_HDR* osi_malloc(size_t n) {
 maxRequest=max(maxRequest,n);
 if(n>budget-live){++failures;return nullptr;}
 auto* p=static_cast<Allocation*>(malloc(sizeof(Allocation)+n));assert(p);p->size=n;live+=n;return reinterpret_cast<BT_HDR*>(p+1);
}
static void osi_free(void* p) {auto* a=static_cast<Allocation*>(p)-1;live-=a->size;free(a);}
static unsigned fixed_queue_length(Queue* q){return q->count;}
static void fixed_queue_enqueue(Queue* q,BT_HDR* p,int){assert(q->count<64);q->items[q->count++]=p;}
static void btc_a2dp_source_flush_q(Queue* q){while(q->count)osi_free(q->items[--q->count]);}
static bool btc_media_aa_read_feeding(){if(underflow)return false;++reads;return true;}
static void SBC_Encoder(Encoder* e){memset(e->pu8Packet,0x55,e->u16PacketLength);}
#include "packet_original.inc"
#include "packet_proposed.inc"
using Prepare=void(*)(UINT8);
static unsigned run(Prepare fn,size_t bytes) {
 budget=bytes;live=failures=reads=maxRequest=0;uint32_t phase=0;
 for(int tick=0;tick<300;++tick) {
  phase+=44100*30;unsigned n=phase/(128*1000);phase%=128*1000;
  fn(n);btc_a2dp_source_flush_q(&queue);assert(!live);
 }
 return reads;
}
int main() {
 const unsigned healthy=run(packet_original,16384);assert(healthy==3100&&failures==0);
 const unsigned slow=run(packet_original,8180);assert(slow==2100&&failures==300);
 const unsigned fixed=run(packet_proposed,8180);assert(fixed==healthy&&!failures&&maxRequest==1056);
 printf("PASS: actual IDF function: healthy %.1f, one-4112-buffer %.1f, proposed allocation %.1f PCM frames/s\n",
   healthy*128/9.0,slow*128/9.0,fixed*128/9.0);
 // ASan checks packet bounds across MTUs and worst legal standard SBC frame.
 for(unsigned mtu:{48U,128U,672U,900U,1005U,2048U,4080U}) {
  auto& state=a2dp_source_local_param.btc_aa_src_cb;
  state.TxAaMtuSize=mtu;
  for(unsigned len:{119U,524U}) {
   state.encoder.u16PacketLength=len;
   assert(run(packet_proposed,65536)==healthy&&!failures);
   assert(maxRequest<=4112);
  }
 }
 auto& s=a2dp_source_local_param.btc_aa_src_cb;
 s.TxAaMtuSize=900;s.encoder.u16PacketLength=119;s.tx_flush=true;
 budget=16384;live=0;packet_proposed(11);assert(!live);s.tx_flush=false;
 underflow=true;s.media_feeding_state.pcm.counter=0;
 packet_proposed(10);assert(s.media_feeding_state.pcm.counter==10*512);btc_a2dp_source_flush_q(&queue);assert(!live);
 puts("PASS: MTU/maximum SBC packet bounds, unchanged flush and PCM-underflow paths");
}
