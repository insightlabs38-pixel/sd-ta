// Track A research: two-pass, hour-owned, exact independent aggregation.
// First scan indexes contiguous hour spans. Owners independently parse,
// aggregate, select and format complete hours. No shared name dictionary.
#include <algorithm>
#include <atomic>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <immintrin.h>
#include <sched.h>
#include <string>
#include <string_view>
#include <sys/mman.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {
constexpr int H = 72, NSLOT = 4096, BLOCK = 256 << 10, MAXLINE = 178;
const char *days[] = {"2026-10-30", "2026-10-31", "2026-11-01", "2026-11-02"};
[[noreturn]] void die(const char *s) { perror(s); _exit(2); }
int hour(const char *p) {
  return (p[9] + (p[6] & 1)) * 24 + (p[11] - '0') * 10 +
         (p[12] - '0') - 1170;
}
struct Span { const char *a, *b; uint8_t h; };
struct Block { std::vector<Span> spans; };
struct Entry { const char *s; uint16_t len; uint32_t errors; };
struct Rec { uint32_t latency; uint16_t id; uint8_t error; };
struct Local {
  uint16_t slot[NSLOT]{};
  std::vector<Entry> names;
  std::vector<Rec> records;
  Local() { names.reserve(1900); records.reserve(50000); }
  static uint64_t load64(const char *p) {
    uint64_t v; memcpy(&v, p, sizeof v); return v;
  }
  uint16_t intern(const char *p, uint32_t n) {
    uint64_t lo = 0, hi = 0;
    memcpy(&lo, p, std::min<uint32_t>(8, n));
    memcpy(&hi, p + (n > 8 ? n - 8 : 0), std::min<uint32_t>(8, n));
    const uint32_t h = _mm_crc32_u64(_mm_crc32_u64(n, lo), hi);
    uint32_t j = h & (NSLOT - 1);
    while (slot[j]) {
      auto &e = names[slot[j] - 1];
      if (e.len == n && !memcmp(e.s, p, n)) return slot[j] - 1;
      j = (j + 1) & (NSLOT - 1);
    }
    if (names.size() >= 2048) die("endpoint overflow");
    const uint16_t id = names.size();
    names.push_back({p, uint16_t(n), 0});
    slot[j] = id + 1;
    return id;
  }
  void parse(const char *p, const char *end) {
    while (p < end) {
      const char *q = (const char *)memchr(p + 28, ' ', 8);
      const char *ep = q + 1;
      q = (const char *)memchr(ep, ' ', 126);
      const uint16_t id = intern(ep, uint32_t(q - ep));
      const bool error = q[1] == '5';
      const char *s = q + 5;
      uint32_t latency = 0;
      while (*s >= '0' && *s <= '9') latency = 10 * latency + (*s++ - '0');
      records.push_back({latency, id, uint8_t(error)});
      names[id].errors += error;
      p = s + 1;
    }
  }
  static void number(std::string &out, uint64_t n) {
    char b[32]; auto e = std::to_chars(b, b + sizeof b, n);
    out.push_back(' ');
    out.append(b, e.ptr);
  }
  std::string finish(int h) {
    std::vector<uint32_t> count(names.size()), pos(names.size());
    for (const Rec &r : records) ++count[r.id];
    uint32_t total = 0;
    for (uint32_t id = 0; id < names.size(); ++id) {
      pos[id] = total; total += count[id];
    }
    std::vector<uint32_t> values(total);
    for (const Rec &r : records) values[pos[r.id]++] = r.latency;
    std::vector<uint16_t> ordered;
    for (uint16_t id = 0; id < names.size(); ++id) ordered.push_back(id);
    std::sort(ordered.begin(), ordered.end(), [&](uint16_t a, uint16_t b) {
      return std::string_view(names[a].s, names[a].len) <
             std::string_view(names[b].s, names[b].len);
    });
    std::string out;
    out.reserve(160000);
    char label[32];
    snprintf(label, sizeof label, "%sT%02u:00:00Z ", days[(h+18)/24], (h+18)%24);
    for (uint16_t id : ordered) {
      auto &e = names[id];
      const uint32_t n = count[id], start = pos[id] - n;
      uint32_t *a = values.data() + start;
      const uint32_t i50 = (50ull * n + 99)/100 - 1;
      const uint32_t i95 = (95ull * n + 99)/100 - 1;
      const uint32_t i99 = (99ull * n + 99)/100 - 1;
      uint32_t p50, p95, p99;
      if (n <= 16) {
        std::sort(a, a+n);
        p50=a[i50]; p95=a[i95]; p99=a[i99];
      } else {
        std::nth_element(a, a+i99, a+n);
        p99=a[i99];
        std::nth_element(a, a+i95, a+i99+1);
        p95=a[i95];
        std::nth_element(a, a+i50, a+i95+1);
        p50=a[i50];
      }
      out.append(label,21);
      out.append(e.s,e.len);
      number(out,n); number(out,p50); number(out,p95); number(out,p99);
      number(out,e.errors);
      number(out, uint64_t(e.errors)*1000000/n);
      out.push_back('\n');
    }
    return out;
  }
};
const char *base;
size_t nbytes, nblocks;
std::vector<Block> blocks;
std::string output[H];
std::atomic<size_t> next_block{0}, next_hour{0};
const char *aligned(size_t k) {
  const size_t off = std::min(nbytes,k*size_t(BLOCK));
  const char *p = base + off;
  if (!off || off==nbytes) return p;
  while (p[-1]!='\n') ++p;
  return p;
}
void build_index() {
  for (size_t b; (b = next_block.fetch_add(1)) < nblocks;) {
    const char *p = aligned(b), *end = aligned(b+1);
    auto &v = blocks[b].spans;
    while (p<end) {
      int h=hour(p);
      const char *start=p;
      do {
        p = (const char *)memchr(p, '\n', MAXLINE) + 1;
      } while (p<end && hour(p)==h);
      v.push_back({start,p,uint8_t(h)});
    }
  }
}
void run_hours() {
  for (size_t h; (h = next_hour.fetch_add(1)) < H;) {
    Local state;
    for (const Block &b : blocks)
      for (const Span &s : b.spans)
        if (s.h==h) state.parse(s.a,s.b);
    if (!state.records.empty()) output[h]=state.finish(h);
  }
}
template <class F> void parallel(uint32_t threads, F f) {
  std::vector<std::thread> pool;
  for (uint32_t i=1;i<threads;++i) pool.emplace_back(f);
  f();
  for (auto &t:pool) t.join();
}
}
int main() {
  const char *const d=getenv("SD_DATA_DIR"), *const o=getenv("SD_OUT_DIR");
  std::string path=std::string(d?d:"/data")+"/requests.log";
  int in=open(path.c_str(),O_RDONLY);
  if(in<0) die("open requests");
  nbytes=lseek(in,0,SEEK_END);
  base=(const char*)mmap(nullptr,nbytes+8192,PROT_READ,
                          MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
  if(base==MAP_FAILED || (nbytes && mmap((void*)base,nbytes,PROT_READ,
                       MAP_PRIVATE|MAP_FIXED,in,0)==MAP_FAILED)) die("mmap");
  nblocks=(nbytes+BLOCK-1)/BLOCK;
  blocks.resize(nblocks);
  cpu_set_t cpus{};sched_getaffinity(0,sizeof cpus,&cpus);
  const char *forced=getenv("SD_THREADS");
  const uint32_t threads=std::max(1u,std::min(16u,forced?
              uint32_t(atoi(forced)):uint32_t(CPU_COUNT(&cpus))));
  parallel(threads,build_index);
  parallel(threads,run_hours);
  std::string dest=std::string(o?o:"/out")+"/summary.txt";
  int fd=open(dest.c_str(),O_WRONLY|O_CREAT|O_TRUNC,0644);
  if(fd<0) die("open summary");
  for(auto &s:output) {
    size_t off=0;
    while(off<s.size()) {
      ssize_t n=write(fd,s.data()+off,s.size()-off);
      if(n<=0) die("write summary");
      off+=n;
    }
  }
  _exit(0);
}
