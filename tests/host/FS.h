#pragma once
#include "Arduino.h"
#include <memory>
#include <filesystem>
#include <cstdarg>
#include <type_traits>
namespace fs {
inline int openFiles=0;
inline constexpr int maxFiles=4;
inline int failRenameAt=-1,renameCalls=0;
class File {
 std::shared_ptr<FILE> handle;
 std::string filename, shortName;
 std::shared_ptr<std::filesystem::directory_iterator> entries;
 public:
 File()=default;
 explicit File(FILE* f,std::string path=""):handle(f,[](FILE* p){if(p){fclose(p);--openFiles;}}),filename(path){if(f)++openFiles;}
 bool isDirectory(){return handle&&std::filesystem::is_directory(filename);}
 const char* name(){shortName=std::filesystem::path(filename).filename().string();return shortName.c_str();}
 File openNextFile(){
  if(!isDirectory())return File();
  if(!entries)entries=std::make_shared<std::filesystem::directory_iterator>(filename);
  if(*entries==std::filesystem::directory_iterator{})return File();
  auto p=(**entries).path().string();++*entries;
  return openFiles>=maxFiles?File():File(fopen(p.c_str(),"r"),p);
 }
 explicit operator bool() const {return handle!=nullptr;}
 int read(){return handle?fgetc(handle.get()):-1;}
 size_t read(uint8_t* p,size_t n){return handle?fread(p,1,n,handle.get()):0;}
 size_t write(const uint8_t* p,size_t n){return handle?fwrite(p,1,n,handle.get()):0;}
 size_t write(uint8_t b){return write(&b,1);}
 bool seek(uint32_t p){return handle&&fseek(handle.get(),p,SEEK_SET)==0;}
 uint32_t position(){return handle?ftell(handle.get()):0;}
 uint32_t size(){if(!handle)return 0;auto p=ftell(handle.get());fseek(handle.get(),0,SEEK_END);auto s=ftell(handle.get());fseek(handle.get(),p,SEEK_SET);return s;}
 uint32_t getLastWrite(){return uint32_t(std::filesystem::last_write_time(filename).time_since_epoch().count());}
 String readString(){String out;int c;while((c=read())>=0)out+=char(c);return out;}
 String readStringUntil(char stop){String out;int c;while((c=read())>=0&&c!=stop)out+=char(c);return out;}
 size_t print(const String& text){return write((const uint8_t*)text.data(),text.size());}
 size_t print(char c){return write(uint8_t(c));}
 template<class T,typename=std::enable_if_t<std::is_arithmetic_v<T>>> size_t print(T value){return print(String(std::to_string(value)));}
 size_t printf(const char* format,...){char b[512];va_list args;va_start(args,format);int n=vsnprintf(b,sizeof(b),format,args);va_end(args);return n<0||size_t(n)>=sizeof(b)?0:print(String(b));}
 int getWriteError(){return handle?ferror(handle.get()):1;}
 void close(){handle.reset();entries.reset();}
};
class FS {
 public:
 std::string root;
 template<class...T> bool begin(T&&...){return true;}
 void end(){}
 uint64_t cardSize(){return 32*1024*1024;}
 File open(const String& path,const char* mode="r"){if(openFiles>=maxFiles)return File();return File(fopen((root+path).c_str(),mode),root+path);}
 bool exists(const String& p){return std::filesystem::exists(root+p);}
 bool remove(const String& p){return std::remove((root+p).c_str())==0;}
 bool rename(const String& a,const String& b){if(renameCalls++==failRenameAt)return false;return std::rename((root+a).c_str(),(root+b).c_str())==0;}
 bool mkdir(const String& p){return std::filesystem::create_directories(root+p);}
};
}
using File=fs::File;
#define FILE_WRITE "w"
