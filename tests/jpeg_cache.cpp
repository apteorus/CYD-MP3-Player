#include <cassert>
#include <filesystem>
#include <fstream>
#include <vector>
#include "jpeg_cache.h"
#include <new>
static bool failWorkspace=false;
void* operator new[](size_t n,const std::nothrow_t&) noexcept {if(failWorkspace)return nullptr;try{return ::operator new[](n);}catch(...){return nullptr;}}
int main(int argc,char**argv){
 assert(argc>=4);SD.root=argv[1];SD.mkdir("/.sys");
 std::filesystem::copy_file(argv[2],SD.root+"/cover.jpg",std::filesystem::copy_options::overwrite_existing);
 auto in=SD.open("/cover.jpg");failWorkspace=true;assert(!jpeg_cacheBuild(in,"/.sys/oom.rgb"));failWorkspace=false;assert(jpeg_cacheBuild(in,"/.sys/cover.rgb"));in.close();
 assert(jpeg_cacheValid("/.sys/cover.rgb"));
 auto out=SD.open("/.sys/cover.rgb");uint16_t pixel;
 assert(out.seek(ART_HEADER));assert(out.read((uint8_t*)&pixel,2)==2&&pixel==0); // letterbox
 assert(out.seek(ART_HEADER+(80*160+80)*2));assert(out.read((uint8_t*)&pixel,2)==2&&(pixel&0xf800)>0xe000);out.close();
 std::filesystem::copy_file(argv[3],SD.root+"/progressive.jpg",std::filesystem::copy_options::overwrite_existing);
 in=SD.open("/progressive.jpg");assert(!jpeg_cacheBuild(in,"/.sys/rejected.rgb"));assert(!SD.exists("/.sys/rejected.rgb"));in.close();
 for(int i=4;i<argc;++i){
  std::filesystem::copy_file(argv[i],SD.root+"/scaled.jpg",std::filesystem::copy_options::overwrite_existing);
  in=SD.open("/scaled.jpg");assert(jpeg_cacheBuild(in,"/.sys/scaled.rgb"));in.close();
  assert(jpeg_cacheValid("/.sys/scaled.rgb"));
  auto scaled=SD.open("/.sys/scaled.rgb");assert(scaled.seek(ART_HEADER+(80*160+80)*2));
  assert(scaled.read((uint8_t*)&pixel,2)==2&&(pixel&0xf800)>0xe000);
 }
 std::filesystem::resize_file(SD.root+"/.sys/cover.rgb",11);assert(!jpeg_cacheValid("/.sys/cover.rgb"));
 puts("PASS: real JPEG decode, RGB565 color/letterbox, progressive rejection and truncated cache");
}
