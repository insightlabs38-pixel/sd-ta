#include <algorithm>
#include <atomic>
#include <charconv>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <immintrin.h>
#include <memory>
#include <mutex>
#include <sched.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {
constexpr uint32_t kHours = 72, kIds = 2048, kBlock = 256 << 10;
constexpr uint32_t kRunCap = kBlock / 40 + 16, kLag = 2, kNever = 1000;

struct __attribute__((packed)) Rec { uint32_t lat; uint16_t tag; };  // tag: id | error << 11
struct Run { Rec *rec; uint32_t n; };
struct Part { std::unique_ptr<char[]> data; size_t size = 0; };

[[noreturn]] void fail(const char *message) {
  std::perror(message);
  std::exit(1);
}
inline __m256i load(const void *p) { return _mm256_loadu_si256(static_cast<const __m256i *>(p)); }
inline uint32_t spaces(const char *p) {
  return static_cast<uint32_t>(_mm256_movemask_epi8(_mm256_cmpeq_epi8(load(p), _mm256_set1_epi8(' '))));
}
// First n bytes (n <= 32) of v, rest zero.
inline __m256i head(const char *p, uint32_t n) {
  static const uint8_t ones[64] = {255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
                                   255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255};
  return _mm256_and_si256(load(p), load(ones + 32 - n));
}

// Concurrent exact endpoint dictionary; lock-free hits, locked inserts.
struct Names {
  std::atomic<uint64_t> slots[4096] {};
  alignas(64) char img[kIds][128] {};
  uint32_t len[kIds] {};
  uint32_t size = 0;
  std::vector<uint16_t> sorted;
  std::mutex mu;

  bool same(uint32_t id, const char *ep, uint32_t n, __m256i a, __m256i b) const {
    const __m256i eq = _mm256_and_si256(_mm256_cmpeq_epi8(a, load(img[id])), _mm256_cmpeq_epi8(b, load(img[id] + 32)));
    return _mm256_movemask_epi8(eq) == -1 && (n < 64 || (len[id] == n && !std::memcmp(img[id] + 64, ep + 64, n - 64)));
  }
  uint32_t find(const char *ep, uint32_t n, __m256i a, __m256i b) {
    const __m256i r = _mm256_xor_si256(a, _mm256_or_si256(_mm256_slli_epi64(b, 29), _mm256_srli_epi64(b, 35)));
    const __m128i f = _mm_xor_si128(_mm256_castsi256_si128(r), _mm256_extracti128_si256(r, 1));
    uint64_t h = _mm_crc32_u64(n, static_cast<uint64_t>(_mm_cvtsi128_si64(f)));
    h = _mm_crc32_u64(h, static_cast<uint64_t>(_mm_extract_epi64(f, 1)));
    if (n > 64) { uint64_t t; std::memcpy(&t, ep + n - 8, 8); h = _mm_crc32_u64(h, t); }
    for (uint32_t i = static_cast<uint32_t>(h);; ++i) {
      const uint64_t s = slots[i & 4095].load(std::memory_order_acquire);
      if (!s) return insert(ep, n, a, b, static_cast<uint32_t>(h));
      if (static_cast<uint32_t>(s >> 32) == h && same(static_cast<uint32_t>(s) - 1, ep, n, a, b))
        return static_cast<uint32_t>(s) - 1;
    }
  }
  uint32_t insert(const char *ep, uint32_t n, __m256i a, __m256i b, uint32_t h) {
    std::lock_guard<std::mutex> lock(mu);
    for (uint32_t i = h;; ++i) {
      const uint64_t s = slots[i & 4095].load(std::memory_order_relaxed);
      if (s && (static_cast<uint32_t>(s >> 32) != h || !same(static_cast<uint32_t>(s) - 1, ep, n, a, b))) continue;
      if (s) return static_cast<uint32_t>(s) - 1;
      const uint32_t id = size++;
      if (id >= kIds) fail("too many endpoints");
      _mm256_storeu_si256(reinterpret_cast<__m256i *>(img[id]), a);
      _mm256_storeu_si256(reinterpret_cast<__m256i *>(img[id] + 32), b);
      if (n > 64) std::memcpy(img[id] + 64, ep + 64, n - 64);
      len[id] = n;
      sorted.insert(std::lower_bound(sorted.begin(), sorted.end(), id,
                                     [&](uint32_t x, uint32_t y) { return std::memcmp(img[x], img[y], 128) < 0; }),
                    static_cast<uint16_t>(id));
      slots[i & 4095].store(static_cast<uint64_t>(h) << 32 | (id + 1), std::memory_order_release);
      return id;
    }
  }
};

uint32_t hour_index(const char *p) {
  return ((p[9] - '0') + (p[6] == '1')) * 24 + (p[11] - '0') * 10 + (p[12] - '0') - 18;
}
uint64_t digits8(uint64_t v) {
  v = (v & 0x0F0F0F0F0F0F0F0F) * 2561 >> 8;
  v = (v & 0x00FF00FF00FF00FF) * 6553601 >> 16;
  return (v & 0x0000FFFF0000FFFF) * 42949672960001 >> 32;
}
void append(char *&out, uint64_t value) { out = std::to_chars(out, out + 20, value).ptr; }

struct Scratch {
  uint64_t count[kIds];  // low 32 bits requests, high 32 bits 5xx
  uint32_t pos[kIds];
  std::vector<uint32_t> lat;
  std::vector<uint16_t> order;
};

struct Job {
  const char *base;
  size_t size;
  uint32_t lag, nblocks, threads;
  int fd;
  Names names;
  std::mutex mu, out_mu;
  std::condition_variable cv;
  std::atomic<uint32_t> next_block {0};
  std::vector<Run> runs[kHours];
  std::vector<Rec *> pool;
  std::vector<uint8_t> done, block_max;
  uint32_t watermark = 0, prefix_max = 0, registered = 0, next_hour = 0, written = 0;
  bool bad = false;
  Part parts[kHours];
  bool ready[kHours] {};

  Job(const char *b, size_t n, uint32_t l, int f, uint32_t t)
      : base(b), size(n), lag(l), nblocks(static_cast<uint32_t>((n + kBlock - 1) / kBlock)), threads(t), fd(f),
        done(nblocks), block_max(nblocks) {}
  ~Job() { for (Rec *r : pool) std::free(r); }

  const char *align(uint32_t block) const {
    const char *p = base + std::min<size_t>(static_cast<size_t>(block) * kBlock, size);
    while (block && p < base + size && p[-1] != '\n') ++p;
    return p;
  }
  Rec *take_run() {
    { std::lock_guard<std::mutex> lock(mu);
      if (!pool.empty()) { Rec *r = pool.back(); pool.pop_back(); return r; } }
    return static_cast<Rec *>(std::malloc(kRunCap * sizeof(Rec)));
  }

  // Parses whole lines of [p, end); at least 256 readable bytes must follow each line.
  const char *lines(const char *p, const char *end, Run (&cur)[kHours], uint32_t &hi) {
    uint32_t h = kHours;
    Rec *wp = nullptr;
    while (p < end) {
      const uint32_t hour = hour_index(p);
      if (hour != h) {
        if (h < kHours) cur[h].n = static_cast<uint32_t>(wp - cur[h].rec);
        if (!cur[hour].rec) cur[hour].rec = take_run();
        h = hour;
        wp = cur[h].rec + cur[h].n;
        hi = std::max(hi, h);
      }
      const char *q = p + 28;
      uint64_t m = spaces(q) | static_cast<uint64_t>(spaces(q + 32)) << 32;
      const uint32_t k1 = static_cast<uint32_t>(__builtin_ctzll(m));
      m &= m - 1;
      uint32_t k2 = 64;
      if (m) {
        k2 = static_cast<uint32_t>(__builtin_ctzll(m));
      } else {
        uint32_t t;
        while (!(t = spaces(q + k2))) k2 += 32;
        k2 += static_cast<uint32_t>(__builtin_ctz(t));
      }
      const char *ep = q + k1 + 1;
      const uint32_t n = k2 - k1 - 1;
      const uint32_t id = names.find(ep, n, head(ep, std::min(n, 32u)), head(ep + 32, n > 32 ? std::min(n - 32, 32u) : 0));
      const char *s = q + k2 + 1;
      const uint32_t error = s[0] == '5';
      s += 4;
      uint64_t v;
      std::memcpy(&v, s, 8);
      v ^= 0x3030303030303030;
      const uint64_t bad_byte = ((v + 0x7676767676767676) | v) & 0x8080808080808080;
      uint64_t lat;
      if (bad_byte) {
        const uint32_t digits = static_cast<uint32_t>(__builtin_ctzll(bad_byte)) >> 3;
        lat = digits8(v << (64 - 8 * digits));
        s += digits;
      } else {
        lat = digits8(v);
        for (s += 8; *s != '\n'; ++s) lat = lat * 10 + static_cast<uint64_t>(*s - '0');
      }
      *wp++ = {static_cast<uint32_t>(lat), static_cast<uint16_t>(id | error << 11)};
      p = s + 1;
    }
    if (h < kHours) cur[h].n = static_cast<uint32_t>(wp - cur[h].rec);
    return p;
  }

  void parse(uint32_t block) {
    Run cur[kHours] {};
    uint32_t hi = 0;
    const char *end = align(block + 1);
    const char *tail = lines(align(block), std::min(end, base + size - std::min<size_t>(size, 256)), cur, hi);
    if (tail < end) {  // the last lines are parsed from a zero-padded copy
      char copy[1024] {};
      std::memcpy(copy, tail, static_cast<size_t>(end - tail));
      lines(copy, copy + (end - tail), cur, hi);
    }
    std::lock_guard<std::mutex> lock(mu);
    for (uint32_t h = 0; h < kHours; ++h) {
      if (!cur[h].rec) continue;
      if (h < next_hour) { bad = true; pool.push_back(cur[h].rec); }
      else runs[h].push_back(cur[h]);
    }
    done[block] = 1;
    block_max[block] = static_cast<uint8_t>(hi);
    ++registered;
    for (; watermark < nblocks && done[watermark]; ++watermark) prefix_max = std::max<uint32_t>(prefix_max, block_max[watermark]);
    cv.notify_all();
  }

  void finalize(uint32_t hour, Scratch &s) {
    std::vector<Run> mine;
    { std::lock_guard<std::mutex> lock(mu);
      mine.swap(runs[hour]);
      std::lock_guard<std::mutex> names_lock(names.mu);
      s.order = names.sorted; }
    std::memset(s.count, 0, sizeof(s.count));
    size_t total = 0;
    for (const Run &r : mine) {
      total += r.n;
      for (uint32_t i = 0; i < r.n; ++i) s.count[r.rec[i].tag & (kIds - 1)] += 1 + (static_cast<uint64_t>(r.rec[i].tag >> 11) << 32);
    }
    uint32_t groups = 0, offset = 0;
    size_t bytes = 0;
    for (const uint16_t id : s.order) {
      s.pos[id] = offset;
      offset += static_cast<uint32_t>(s.count[id]);
      if (s.count[id]) { ++groups; bytes += names.len[id] + 130; }
    }
    s.lat.resize(total);
    for (const Run &r : mine)
      for (uint32_t i = 0; i < r.n; ++i) s.lat[s.pos[r.rec[i].tag & (kIds - 1)]++] = r.rec[i].lat;
    Part part;
    part.data.reset(new char[bytes]);
    char *out = part.data.get();
    char label[22];
    static const char *const days[4] = {"2026-10-30", "2026-10-31", "2026-11-01", "2026-11-02"};
    std::snprintf(label, sizeof(label), "%sT%02u:00:00Z ", days[(hour + 18) / 24], (hour + 18) % 24);
    for (const uint16_t id : s.order) {
      const uint32_t n = static_cast<uint32_t>(s.count[id]), errors = static_cast<uint32_t>(s.count[id] >> 32);
      if (!n) continue;
      uint32_t *const first = s.lat.data() + s.pos[id] - n;
      const uint32_t r50 = (50 * n + 99) / 100 - 1, r95 = (95 * n + 99) / 100 - 1, r99 = (99 * n + 99) / 100 - 1;
      if (n <= 32) {
        std::sort(first, first + n);
      } else {
        std::nth_element(first, first + r50, first + n);
        std::nth_element(first + r50 + 1, first + r95, first + n);
        std::nth_element(first + r95 + 1, first + r99, first + n);
      }
      std::memcpy(out, label, 21);
      out += 21;
      std::memcpy(out, names.img[id], names.len[id]);
      out += names.len[id];
      for (const uint64_t v : {uint64_t{n}, uint64_t{first[r50]}, uint64_t{first[r95]}, uint64_t{first[r99]}, uint64_t{errors},
                               uint64_t{errors} * 1000000 / n}) {
        *out++ = ' ';
        append(out, v);
      }
      *out++ = '\n';
    }
    part.size = static_cast<size_t>(out - part.data.get());
    { std::lock_guard<std::mutex> lock(mu);
      for (const Run &r : mine) pool.push_back(r.rec); }
    std::lock_guard<std::mutex> lock(out_mu);
    parts[hour] = std::move(part);
    ready[hour] = true;
    for (; written < kHours && ready[written]; ++written) {
      for (size_t off = 0; off < parts[written].size;) {
        const ssize_t w = write(fd, parts[written].data.get() + off, parts[written].size - off);
        if (w <= 0) fail("write summary");
        off += static_cast<size_t>(w);
      }
      parts[written].data.reset();
    }
  }

  bool claimable() const { return next_hour < kHours && (registered == nblocks || next_hour + lag <= prefix_max); }

  void work() {
    auto s = std::make_unique<Scratch>();
    for (;;) {
      int hour = -1;
      { std::lock_guard<std::mutex> lock(mu);
        if (claimable()) hour = static_cast<int>(next_hour++);
        if (next_hour >= kHours) cv.notify_all(); }
      if (hour >= 0) { finalize(static_cast<uint32_t>(hour), *s); continue; }
      const uint32_t block = next_block.fetch_add(1);
      if (block < nblocks) { parse(block); continue; }
      std::unique_lock<std::mutex> lock(mu);
      cv.wait(lock, [&] { return next_hour >= kHours || claimable(); });
      if (next_hour >= kHours) return;
    }
  }
  bool run() {
    std::vector<std::thread> pool_threads;
    for (uint32_t i = 1; i < threads; ++i) pool_threads.emplace_back([this] { work(); });
    work();
    for (auto &t : pool_threads) t.join();
    return !bad;
  }
};
} // namespace

int main() {
  const char *const data_dir = std::getenv("SD_DATA_DIR");
  const std::string input_path = std::string(data_dir ? data_dir : "/data") + "/requests.log";
  const int input = open(input_path.c_str(), O_RDONLY);
  if (input < 0) fail("open requests");
  struct stat statbuf {};
  if (fstat(input, &statbuf) != 0) fail("stat requests");
  const size_t bytes = static_cast<size_t>(statbuf.st_size);
  const char *mapped = bytes ? static_cast<const char *>(mmap(nullptr, bytes, PROT_READ, MAP_PRIVATE, input, 0)) : nullptr;
  if (bytes && mapped == MAP_FAILED) fail("mmap requests");
  close(input);

  const char *const out_dir = std::getenv("SD_OUT_DIR");
  const std::string output_path = std::string(out_dir ? out_dir : "/out") + "/summary.txt";
  const int fd = open(output_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0) fail("open summary");
  cpu_set_t set;
  CPU_ZERO(&set);
  sched_getaffinity(0, sizeof(set), &set);
  const char *const forced = std::getenv("SD_THREADS");
  const uint32_t threads = forced ? static_cast<uint32_t>(std::atoi(forced)) : std::max(1, CPU_COUNT(&set));
  if (!std::make_unique<Job>(mapped, bytes, kLag, fd, threads)->run()) {
    if (ftruncate(fd, 0) != 0 || lseek(fd, 0, SEEK_SET) != 0) fail("rewind summary");
    std::make_unique<Job>(mapped, bytes, kNever, fd, threads)->run();  // exact rerun without early finalization
  }
  if (close(fd) != 0) fail("close summary");
}
