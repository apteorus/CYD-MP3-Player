#include <cassert>
#include <fstream>
#include <vector>
#include "vlw_stream.h"
#include <new>
static bool failStrip=false;
void* operator new[](size_t n,const std::nothrow_t&) noexcept { if(failStrip)return nullptr;try{return ::operator new[](n);}catch(...){return nullptr;} }
static void put32(std::vector<uint8_t>& b,uint32_t n){for(int s=24;s>=0;s-=8)b.push_back(n>>s);}
static void save(const char* path,const std::vector<uint8_t>& b){std::ofstream f(SD.root+path,std::ios::binary);f.write((const char*)b.data(),b.size());}
int main(int argc,char** argv){
 assert(argc>=2);SD.root=argv[1];SD.mkdir("/.sys");
 std::vector<uint8_t> b;
 for(auto n:{3,11,16,0,2,0})put32(b,n);
 for(auto cp:{63,65,0x4e2d}){for(auto n:{cp,2,2,3,2,0,0})put32(b,n);}
 b.insert(b.end(),12,255);save("/font.vlw",b);
 {VlwStream font;assert(font.begin("/font.vlw"));assert(font.render("A中A","/.sys/test.ttl"));failStrip=true;assert(!font.render("A","/.sys/no-memory.ttl"));failStrip=false;}
 {auto out=SD.open("/.sys/test.ttl");uint8_t h[4];assert(out.read(h,4)==4);assert(h[0]==9&&h[2]==18&&h[3]==2);uint8_t row[3];assert(out.read(row,3)==3);assert(row[0]!=0);}
 auto truncated=b;truncated.pop_back();save("/bad.vlw",truncated);
 {VlwStream font;assert(!font.begin("/bad.vlw"));}
 auto bad=b;bad[0]=0x7f;save("/bad.vlw",bad);
 {VlwStream font;assert(!font.begin("/bad.vlw"));}
 bad=b;bad[31]=255;save("/bad.vlw",bad); // first glyph height out of bounds
 {VlwStream font;assert(!font.begin("/bad.vlw"));}
 {VlwStream font;assert(!font.begin("/missing.vlw"));}
 if(argc==3){SD.remove("/real.vlw");std::filesystem::copy_file(argv[2],SD.root+"/real.vlw");VlwStream font;assert(font.begin("/real.vlw"));assert(font.render("AIR 中日文 test","/.sys/real.ttl"));}
 puts("PASS: streamed VLW rendering, CJK, malformed/truncated/missing font rejection");
}
