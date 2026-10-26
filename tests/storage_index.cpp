#include <cassert>
#include <fstream>
#include <filesystem>
#include <vector>
#include "storage.h"
using Bytes=std::vector<uint8_t>;
static void word(Bytes& b,uint32_t n,bool sync=false){for(int s=sync?21:24;s>=0;s-=sync?7:8)b.push_back((n>>s)&(sync?127:255));}
static void song(const String& path,const char* album){
 Bytes tag;
 for(auto field:std::vector<std::pair<const char*,const char*>>{{"TIT2","Title"},{"TPE1","Artist"},{"TALB",album},{"TRCK","2/9"}}){
  tag.insert(tag.end(),field.first,field.first+4);word(tag,strlen(field.second)+1);tag.push_back(0);tag.push_back(0);tag.push_back(3);tag.insert(tag.end(),field.second,field.second+strlen(field.second));
 }
 Bytes all{'I','D','3',3,0,0};word(all,tag.size(),true);all.insert(all.end(),tag.begin(),tag.end());
 std::ofstream f(SD.root+path,std::ios::binary);f.write((char*)all.data(),all.size());
}
static int visited;
static void label(int,const String&,const String& text){assert(text=="Title - Artist");++visited;}
int main(int argc,char**argv){
 assert(argc==2);SD.root=argv[1];SD.mkdir("/music/A/B");SD.mkdir("/.sys");
 song("/music/A/B/deep.mp3","Old");song("/music/root.mp3","Root");
 {std::ofstream f(SD.root+"/.sys/library.idx");f<<"CYDLIB2 0 00000000\n";}
 assert(storage_begin(nullptr)&&storage_trackCount()==2);
 Id3Info info;assert(storage_trackInfo(0,info));assert(info.title=="Title"&&info.artist=="Artist"&&info.album=="Old"&&info.track=="2/9");
 assert(storage_trackPath(0)=="/music/A/B/deep.mp3");
 visited=0;storage_forEach(label);assert(visited==2);
 {auto f=SD.open("/.sys/library.idx");assert(f.readStringUntil('\n').startsWith("CYDLIB3 "));}
 uint32_t crc=storage_libraryCrc();
 auto timestamp=std::filesystem::last_write_time(SD.root+"/music/A/B/deep.mp3");
 song("/music/A/B/deep.mp3","New");
 std::filesystem::last_write_time(SD.root+"/music/A/B/deep.mp3",timestamp);
 // Force rescan despite a metadata edit that could keep FAT size/time unchanged.
 SD.remove("/.sys/library.idx");assert(storage_begin(nullptr));
 assert(storage_libraryCrc()!=crc);assert(storage_trackInfo(0,info)&&info.album=="New");
 assert(SD.rename("/.sys/library.idx","/.sys/library.old"));
 assert(storage_begin(nullptr)&&storage_trackCount()==2&&SD.exists("/.sys/library.idx"));
 // A failed replacement rename must roll the old index back intact.
 std::ifstream oldFile(SD.root+"/.sys/library.idx",std::ios::binary);
 std::string oldIndex((std::istreambuf_iterator<char>(oldFile)),{});oldFile.close();
 song("/music/A/B/deep.mp3","Changed");fs::renameCalls=0;fs::failRenameAt=1;
 assert(!storage_begin(nullptr));fs::failRenameAt=-1;
 std::ifstream keptFile(SD.root+"/.sys/library.idx",std::ios::binary);
 std::string kept((std::istreambuf_iterator<char>(keptFile)),{});assert(kept==oldIndex);keptFile.close();
 assert(storage_begin(nullptr)&&storage_trackInfo(0,info)&&info.album=="Changed");
 uint32_t art=storage_artworkCrc(), lib=storage_libraryCrc();
 {std::ofstream f(SD.root+"/music/A/cover.jpg");f<<"cover";}
 assert(storage_begin(nullptr));assert(storage_libraryCrc()==lib&&storage_artworkCrc()!=art);
 art=storage_artworkCrc();assert(storage_begin(nullptr)&&storage_artworkCrc()==art);
 {std::ofstream f(SD.root+"/music/A/cover.jpg",std::ios::app);f<<"changed";}
 assert(storage_begin(nullptr)&&storage_artworkCrc()!=art);
 SD.remove("/music/A/cover.jpg");assert(storage_begin(nullptr)&&storage_libraryCrc()==lib);
 assert(fs::openFiles==0);
 puts("PASS: CYDLIB2 migration, full metadata roundtrip, two folder levels at 4-file limit, metadata CRC and index recovery");
}
