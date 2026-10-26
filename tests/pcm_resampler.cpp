#include "pcm_resampler.h"
#include <cassert>
#include <cstdio>
#include <vector>
#include <cmath>

static std::vector<int16_t> run(PcmResampler& r, const std::vector<int16_t>& in, int ch, unsigned hz, unsigned chunk) {
  std::vector<int16_t> out;
  for (size_t i=0; i<in.size(); i+=chunk*ch) {
    size_t n = std::min<size_t>(chunk*ch,in.size()-i);
    assert(r.process(in.data()+i,n,ch,hz,[&](int16_t l,int16_t right) { out.push_back(l);out.push_back(right);return true; }));
  }
  return out;
}
int main() {
  PcmResampler reused;
  for (unsigned hz : {48000,32000,22050,44100,16000,24000,8000,12000,11025,48000}) {
    for (int ch : {1,2}) {
      std::vector<int16_t> in(hz*ch);
      for (unsigned i=0;i<hz;++i) for(int c=0;c<ch;++c)
        in[i*ch+c]=int16_t(32767*std::sin(2*3.141592653589793*440*i/hz));
      reused.reset(); auto a=run(reused,in,ch,hz,576);
      PcmResampler fresh; auto b=run(fresh,in,ch,hz,1152);
      assert(a==b); assert(std::abs(int(a.size()/2)-44100)<=2);
      // Positive zero crossings verify duration AND pitch, not just frame count.
      int crossings=0;for(size_t i=2;i<a.size();i+=2) if(a[i-2]<=0 && a[i]>0) ++crossings;
      assert(std::abs(crossings-440)<=1);
      // An interrupted PCM block must not poison the next reset/track.
      int accepted=0;
      reused.process(in.data(),in.size(),ch,hz,[&](int16_t,int16_t){return ++accepted<17;});
      reused.reset(); assert(run(reused,in,ch,hz,37)==b);
    }
  }
  // Reproduce the old signed-product overflow with full-scale transitions.
  int16_t extreme[]={-32768,32767,-32768,32767,-32768,32767};
  reused.reset(); assert(reused.process(extreme,6,1,32000,[](int16_t,int16_t){return true;}));
  assert(!reused.process(extreme,6,0,44100,[](int16_t,int16_t){return true;}));
  puts("PASS: 9 source rates, mono/stereo, pitch/count, chunk boundaries, abort/reset and full-scale interpolation");
}
