// Host regression against the real pinned arduino-libhelix sources.
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <vector>
#include "resettable_mp3.h"

static unsigned allocations, releases;
extern "C" void* __real_malloc(size_t);
extern "C" void* __real_calloc(size_t, size_t);
extern "C" void* __real_realloc(void*, size_t);
extern "C" void __real_free(void*);
extern "C" void* __wrap_malloc(size_t n) { ++allocations; return __real_malloc(n); }
extern "C" void* __wrap_calloc(size_t n, size_t s) { ++allocations; return __real_calloc(n, s); }
extern "C" void* __wrap_realloc(void* p, size_t n) { ++allocations; return __real_realloc(p, n); }
extern "C" void __wrap_free(void* p) { ++releases; __real_free(p); }
// C++ vector storage in Helix uses throwing new[]. Count it too.
void* operator new(size_t n) { ++allocations; if (void* p = __real_malloc(n)) return p; throw std::bad_alloc(); }
void* operator new[](size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { ++releases; __real_free(p); }
void operator delete[](void* p) noexcept { ::operator delete(p); }
void operator delete(void* p, size_t) noexcept { ::operator delete(p); }
void operator delete[](void* p, size_t) noexcept { ::operator delete(p); }
static uint64_t hash;
static size_t samples;
static void pcm(MP3FrameInfo&, short* data, size_t count, void*) {
  for (size_t i = 0; i < count; ++i) { hash ^= static_cast<uint16_t>(data[i]); hash *= 1099511628211ULL; }
  samples += count;
}
static std::pair<uint64_t, size_t> decode(ResettableMp3& decoder,
                                        const std::vector<char>& bytes, size_t start) {
  hash = 14695981039346656037ULL; samples = 0;
  for (size_t p = start; p < bytes.size(); p += 1024)
    decoder.write(bytes.data() + p, std::min<size_t>(1024, bytes.size() - p));
  return {hash, samples};
}
int main(int argc, char** argv) {
  assert(argc == 2);
  std::ifstream input(argv[1], std::ios::binary);
  std::vector<char> bytes((std::istreambuf_iterator<char>(input)), {});
  assert(bytes.size() > 16000);
  ResettableMp3 reused(pcm);
  assert(reused.resetStream());
  for (int i = 0; i < 100; ++i) {
    // Leave previous track/bit-reservoir data in the decoder before resetting.
    const unsigned a = allocations, f = releases;
    assert(reused.resetStream());
    assert(allocations == a && releases == f);
    const size_t offset = (i % 3) * 5000;
    const auto actual = decode(reused, bytes, offset);
    ResettableMp3 fresh(pcm);
    assert(fresh.resetStream());
    const auto expected = decode(fresh, bytes, offset);
    assert(actual.second > 0 && actual == expected);
  }
  std::puts("PASS: 100 resets allocate/free nothing; PCM matches fresh decoder at 3 offsets");
}
