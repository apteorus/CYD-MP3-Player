#include <cassert>
#include <fstream>
#include <filesystem>
#include <vector>
#include "cover_cache.h"
#include "jpeg_cache.h"
using Bytes=std::vector<uint8_t>;
static Bytes load(const char* file){std::ifstream f(file,std::ios::binary);return Bytes((std::istreambuf_iterator<char>(f)),{});}
static void word(Bytes& b,uint32_t n,bool sync=false){for(int s=sync?21:24;s>=0;s-=sync?7:8)b.push_back((n>>s)&(sync?127:255));}
static void mp3(const String& path,const Bytes& jpeg){
 Bytes body{0};const char* mime="image/jpeg";body.insert(body.end(),mime,mime+strlen(mime)+1);body.push_back(3);body.push_back(0);body.insert(body.end(),jpeg.begin(),jpeg.end());
 Bytes tag{'A','P','I','C'};word(tag,body.size());tag.push_back(0);tag.push_back(0);tag.insert(tag.end(),body.begin(),body.end());
 Bytes all{'I','D','3',3,0,0};word(all,tag.size(),true);all.insert(all.end(),tag.begin(),tag.end());
 std::ofstream f(SD.root+path,std::ios::binary);f.write((char*)all.data(),all.size());
}
static Id3Info tags(const String& path){auto f=SD.open(path);Id3Info info;assert(id3_read(f,info));return info;}
static size_t countAssets(){size_t count=0;for(const auto& p:std::filesystem::directory_iterator(SD.root+"/.sys/art"))if(p.path().extension()==".rgb")++count;return count;}
int main(int argc,char**argv){
 assert(argc==5);SD.root=argv[1];SD.mkdir("/music/album/disc1");SD.mkdir("/.sys");
 const auto red=load(argv[2]),progressive=load(argv[4]);
 std::filesystem::copy_file(argv[3],SD.root+"/music/album/cover.jpg",std::filesystem::copy_options::overwrite_existing);
 const String a="/music/album/disc1/a.mp3",b="/music/album/disc1/b.mp3",c="/music/album/disc1/c.mp3";
 mp3(a,red);auto info=tags(a);assert(cover_prepare(a,info));String first=cover_asset(a);assert(!first.empty());assert(countAssets()==1);
 // Different tracks with byte-identical embedded images share one asset.
 mp3(b,red);assert(cover_prepare(b,tags(b)));assert(cover_asset(b)==first&&countAssets()==1);
 // Unsupported embedded progressive JPEG uses the ancestor folder cover.
 mp3(c,progressive);assert(cover_prepare(c,tags(c)));String folder=cover_asset(c);assert(!folder.empty()&&folder!=first&&countAssets()==2);
 // Unchanged sources avoid conversion; damaged cache is regenerated.
 assert(cover_prepare(a,info)&&cover_asset(a)==first);
 std::filesystem::resize_file(SD.root+first,12);
 bool stale=false;assert(cover_asset(a,&stale).isEmpty()&&stale);
 assert(cover_prepare(a,info)&&jpeg_cacheValid(first));
 stale=true;assert(cover_asset(a,&stale)==first&&!stale);
 // Updating a folder cover invalidates its reference.
 std::filesystem::copy_file(argv[2],SD.root+"/music/album/cover.jpg",std::filesystem::copy_options::overwrite_existing);
 assert(cover_prepare(c,tags(c))&&cover_asset(c)==first);
 // An interrupted track conversion never retries automatically at boot.
 uint64_t h=14695981039346656037ULL;for(auto ch:a){h^=uint8_t(ch);h*=1099511628211ULL;}
 char stem[100];snprintf(stem,sizeof(stem),"/.sys/art/%016llx.ref.busy",(unsigned long long)h);
 {auto marker=SD.open(stem,FILE_WRITE);assert(marker.write(uint8_t(1))==1);}
 assert(!cover_prepare(a,info)&&cover_asset(a).empty());SD.remove(stem);
 assert(cover_prepare(a,info)&&cover_asset(a)==first);
 const String missing="/music/no-cover/song.mp3";SD.mkdir("/music/no-cover");{auto f=SD.open(missing,FILE_WRITE);f.write(uint8_t(0));}
 assert(cover_prepare(missing,Id3Info())&&cover_asset(missing).empty());
 puts("PASS: APIC extraction/cache/dedup, folder fallback, source invalidation, interrupted conversion and placeholder");
}
