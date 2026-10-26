#include <cassert>
#include <fstream>
#include <filesystem>
#include "titles.h"
#include "storage.h"
#include "i18n.h"
#include "theme.h"
#include "style.h"
#include "playlist.h"
static uint32_t generation=1;
uint32_t storage_libraryCrc(){return generation;}
bool storage_forEach(StorageEachFn fn){auto index=SD.open("/.sys/mock.idx");if(!index)return false;fn(0,"/music/a.mp3","A中");return true;}
bool storage_forEachInfo(StorageInfoFn fn){auto index=SD.open("/.sys/mock.idx");if(!index)return false;Id3Info i;i.album="中";fn(0,"/music/a.mp3",i);return true;}
void i18n_forEachChinese(void(*fn)(const char*)){fn("中");}
int theme_count(){return 0;} const Palette& theme_at(int){static Palette p={};return p;}
int style_count(){return 0;} const Style& style_at(int){static Style p={};return p;}
uint32_t playlist_namesCrc(){return 0;}void playlist_forEachName(void(*)(const char*)){}
static void put32(std::ofstream& f,uint32_t n){for(int s=24;s>=0;s-=8)f.put(n>>s);}
static void makeFont(){std::ofstream f(SD.root+"/.sys/cjk16.vlw",std::ios::binary);for(int n:{3,11,16,0,2,0})put32(f,n);for(int cp:{63,65,0x4e2d})for(int n:{cp,2,2,3,2,0,0})put32(f,n);for(int i=0;i<12;++i)f.put(char(255));}
int main(int argc,char**argv){
 assert(argc==2);SD.root=argv[1];SD.mkdir("/.sys");makeFont();{auto f=SD.open("/.sys/mock.idx",FILE_WRITE);f.write(uint8_t(1));}
 assert(titles_prepareAll(nullptr));assert(!SD.exists("/.sys/font.busy"));assert(titles_load("A中"));assert(titles_width()==6);
 auto cached=SD.root+"/.sys/t";size_t before=0;for(auto& p:std::filesystem::directory_iterator(cached))if(p.path().extension()==".ttl")++before;
 // Model reset/power loss with a persisted marker: no render retry on next boot.
 ++generation;{auto marker=SD.open("/.sys/font.busy",FILE_WRITE);marker.write(uint8_t(1));}
 assert(!titles_prepareAll(nullptr));assert(SD.exists("/.sys/font.busy"));assert(titles_load("A中"));
 SD.remove("/.sys/font.busy");std::filesystem::resize_file(SD.root+"/.sys/cjk16.vlw",10);
 assert(!titles_prepareAll(nullptr));assert(!SD.exists("/.sys/font.busy"));assert(titles_load("A中"));
 SD.remove("/.sys/cjk16.vlw");assert(!titles_prepareAll(nullptr));assert(titles_load("A中"));
 makeFont();assert(titles_prepareAll(nullptr));size_t after=0;for(auto& p:std::filesystem::directory_iterator(cached))if(p.path().extension()==".ttl")++after;assert(before==after);
 // Index I/O failure must not incorrectly mark title preparation complete.
 ++generation;SD.remove("/.sys/mock.idx");
 assert(!titles_prepareAll(nullptr)&&!SD.exists("/.sys/font.busy"));assert(titles_load("A中"));
 {auto f=SD.open("/.sys/mock.idx",FILE_WRITE);f.write(uint8_t(1));}
 assert(titles_prepareAll(nullptr));assert(fs::openFiles==0);
 // A partial legacy strip is rebuilt; all other cached title strips survive.
 std::string damaged;
 for(const auto& entry:std::filesystem::directory_iterator(cached)){
  if(entry.path().extension()!=".ttl")continue;
  std::ifstream f(entry.path(),std::ios::binary);if(f.get()==6){damaged=entry.path();break;}
 }
 assert(!damaged.empty());std::filesystem::resize_file(damaged,2);
 assert(titles_load("中"));assert(!titles_load("A中"));assert(!SD.exists("/.sys/t/done"));
 assert(titles_prepareAll(nullptr)&&titles_load("A中"));
 puts("PASS: production title preparation, reset recovery marker, invalid/missing font fallback and existing cache retained");
}
