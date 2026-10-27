#include "bt_feed_stats.h"
#include <cassert>
#include <cstdio>
#include <thread>

int main() {
  BtFeedStats s;
  uint32_t now=0xffff0000U; // verify micros wrap across a live group
  for (int tick=0;tick<100;++tick) {
    unsigned count=tick<50?(tick%2?11:10):7;
    for (unsigned i=0;i<count;++i) s.pull(now+i*55,128);
    now+=30000;
  }
  s.pull(now,128); // closes last group, leaves new group open
  auto a=s.snapshot();
  assert(a.bursts==100 && a.seven==50 && a.ten==25 && a.eleven==25 && !a.other);
  assert(a.calls==876 && a.frames==876*128 && a.maxGapUs<30001);
  s.work(57); s.work(12); assert(s.snapshot().maxWorkUs==57);
  auto fail=[&](unsigned core) {for(int i=0;i<10000;++i)s.allocationFailed(i%2?4112:64,0x1000,core);};
  std::thread t(fail,0);fail(1);t.join();
  a=s.snapshot();assert(a.failures==20000 && a.packetFailures==10000 && a.lastCaps==0x1000);
  puts("PASS: normal 10/11 to seven-call groups, paused-feed accounting, micros wrap, concurrent heap failure counters");
}
