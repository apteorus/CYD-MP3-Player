#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <cctype>
using std::min; using std::max;
template<class T> T constrain(T v, T lo, T hi) { return min(hi, max(lo, v)); }
class String : public std::string {
 public:
  using std::string::string;
  String() = default;
  String(const std::string& s) : std::string(s) {}
  int indexOf(char c, unsigned start=0) const { auto p=find(c,start);return p==npos?-1:int(p); }
  int lastIndexOf(char c) const { auto p=rfind(c);return p==npos?-1:int(p); }
  bool startsWith(const String& p) const {return compare(0,p.size(),p)==0;}
  bool endsWith(const String& p) const {return size()>=p.size()&&compare(size()-p.size(),p.size(),p)==0;}
  bool concat(char c){push_back(c);return true;}
  bool isEmpty() const { return empty(); }
  void toLowerCase(){std::transform(begin(),end(),begin(),[](unsigned char c){return std::tolower(c);});}
  void trim() { auto a=find_first_not_of(" \r\n\t"); if(a==npos)clear();else *this=substr(a,find_last_not_of(" \r\n\t")-a+1); }
  void replace(char a,char b) { std::replace(begin(),end(),a,b); }
  String substring(unsigned a, unsigned b=UINT32_MAX) const { return a>=size()?String():String(substr(a,b==UINT32_MAX?npos:b-a)); }
};
struct SerialMock { template<class... T> void printf(const char*,T...) {} void println(const char*) {} };
inline SerialMock Serial;
struct EspMock { unsigned getFreeHeap(){return 100000;} unsigned getMaxAllocHeap(){return 4000;} };
inline EspMock ESP;
inline void yield() {}

using SemaphoreHandle_t=void*;
inline SemaphoreHandle_t xSemaphoreCreateMutex(){return reinterpret_cast<void*>(1);}
inline void xSemaphoreTake(SemaphoreHandle_t,unsigned){}
inline void xSemaphoreGive(SemaphoreHandle_t){}
static const unsigned portMAX_DELAY=0;
inline unsigned millis(){static unsigned n=0;return ++n;}

inline unsigned hostDelays=0;
inline void delay(unsigned){++hostDelays;}
