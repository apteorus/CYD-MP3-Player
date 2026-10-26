#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <cstdio>
#include <cstdlib>
using std::min; using std::max;
template<class T> T constrain(T v, T lo, T hi) { return min(hi, max(lo, v)); }
class String : public std::string {
 public:
  using std::string::string;
  String() = default;
  String(const std::string& s) : std::string(s) {}
  bool isEmpty() const { return empty(); }
  void trim() { auto a=find_first_not_of(" \r\n\t"); if(a==npos)clear();else *this=substr(a,find_last_not_of(" \r\n\t")-a+1); }
  void replace(char a,char b) { std::replace(begin(),end(),a,b); }
  String substring(unsigned a, unsigned b=UINT32_MAX) const { return a>=size()?String():String(substr(a,b==UINT32_MAX?npos:b-a)); }
};
struct SerialMock { template<class... T> void printf(const char*,T...) {} void println(const char*) {} };
inline SerialMock Serial;
inline void yield() {}
