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
constexpr uint32_t kStreams = 4;
constexpr uint32_t kRunCap = kBlock / 40 + 16, kLag = 2, kNever = 1000, kKeep = 2, kAhead = 48, kPartCap = kIds * 256;  // an hour has < kIds lines

struct __attribute__((packed)) Rec { uint32_t lat; uint16_t tag; };  // tag: id | error << 11
struct Run { Rec *rec; uint32_t n; };
struct Part {  // output text of one hour; the buffer is recycled once written
  std::unique_ptr<char[]> data;
  size_t size = 0;
};

[[noreturn]] void fail(const char *message) {
  std::perror(message);
  std::exit(1);
}
inline __m256i load(const void *p) { return _mm256_loadu_si256(static_cast<const __m256i *>(p)); }
inline uint64_t load64(const void *p) {
  uint64_t v;
  std::memcpy(&v, p, 8);
  return v;
}
inline uint32_t spaces(const char *p) {
  return static_cast<uint32_t>(_mm256_movemask_epi8(_mm256_cmpeq_epi8(load(p), _mm256_set1_epi8(' '))));
}
// 32 bytes with the first n (<= 32) set.
struct Masks {
  uint8_t ones[65][64];  // row k: the first k bytes of 64 set
  constexpr Masks() : ones() {
    for (int k = 0; k <= 64; ++k)
      for (int i = 0; i < k; ++i) ones[k][i] = 255;
  }
};
constexpr Masks kMasks;

// Concurrent exact endpoint dictionary: 8-way buckets, lock-free hits, locked
// inserts. Keys are stored zero-padded with a space after the last byte, so
// comparing n + 1 bytes also proves equal length.
struct Bucket { uint32_t tag[8]; uint16_t id[8]; };
struct Names {
  alignas(64) Bucket buckets[512] {};
  alignas(64) char img[kIds][128] {};
  uint32_t len[kIds] {};
  uint32_t size = 0;
  std::vector<uint16_t> sorted;
  std::mutex mu;

  bool same(uint32_t id, const char *ep, uint32_t n) const {
    const uint8_t *mask = kMasks.ones[std::min(n + 1, 64u)];
    const __m256i d0 = _mm256_xor_si256(load(ep), load(img[id]));
    const __m256i d1 = _mm256_xor_si256(load(ep + 32), load(img[id] + 32));
    return _mm256_testz_si256(d0, load(mask)) && _mm256_testz_si256(d1, load(mask + 32)) &&
           (n < 64 || (len[id] == n && !std::memcmp(img[id] + 64, ep + 64, n - 64)));
  }
  __attribute__((always_inline)) uint32_t find(const char *ep, uint32_t n) {
    const uint64_t keep = n < 8 ? n * 8 : 64;
    const uint64_t x = _bzhi_u64(load64(ep), keep), y = _bzhi_u64(load64(ep + std::max(n, 8u) - 8), keep);
    const uint32_t h = static_cast<uint32_t>(_mm_crc32_u64(_mm_crc32_u64(n, x), y)) | 1;
    for (uint32_t i = h >> 16;; ++i) {
      const Bucket &k = buckets[i & 511];
      uint32_t m = static_cast<uint32_t>(_mm256_movemask_ps(_mm256_castsi256_ps(
          _mm256_cmpeq_epi32(load(k.tag), _mm256_set1_epi32(static_cast<int>(h))))));
      for (; m; m &= m - 1)
        if (same(k.id[__builtin_ctz(m)], ep, n)) return k.id[__builtin_ctz(m)];
      if (!k.tag[7]) return insert(ep, n, h);
    }
  }
  uint32_t insert(const char *ep, uint32_t n, uint32_t h) {
    std::lock_guard<std::mutex> lock(mu);
    for (uint32_t i = h >> 16;; ++i) {
      Bucket &k = buckets[i & 511];
      uint32_t slot = 0;
      for (; slot < 8 && k.tag[slot]; ++slot)
        if (k.tag[slot] == h && same(k.id[slot], ep, n)) return k.id[slot];
      if (slot == 8) continue;
      const uint32_t id = size++;
      if (id >= kIds) fail("too many endpoints");
      std::memcpy(img[id], ep, n);
      img[id][n] = ' ';
      len[id] = n;
      sorted.insert(std::lower_bound(sorted.begin(), sorted.end(), id,
                                     [&](uint32_t x, uint32_t y) { return std::memcmp(img[x], img[y], 128) < 0; }),
                    static_cast<uint16_t>(id));
      k.id[slot] = static_cast<uint16_t>(id);
      __atomic_store_n(&k.tag[slot], h, __ATOMIC_RELEASE);
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

// Places the k-th smallest of a[lo, hi) at a[k], smaller values before it and
// larger ones after (branchless Lomuto partitions; ranges <= 16 are sorted).
void select(uint32_t *a, uint32_t lo, uint32_t hi, uint32_t k) {
  for (int depth = 0; hi - lo > 16; ++depth) {
    if (depth > 48) return std::nth_element(a + lo, a + k, a + hi);
    const uint32_t x = a[lo], y = a[lo + (hi - lo) / 2], z = a[hi - 1];
    const uint32_t pivot = std::max(std::min(x, y), std::min(std::max(x, y), z));
    uint32_t j = lo;
    for (uint32_t i = lo; i < hi; ++i) {
      const uint32_t v = a[i];
      a[i] = a[j];
      a[j] = v;
      j += v < pivot;
    }
    if (j == lo) {  // the pivot is the minimum: split off its duplicates
      for (uint32_t i = lo; i < hi; ++i) {
        const uint32_t v = a[i];
        a[i] = a[j];
        a[j] = v;
        j += v == pivot;
      }
      if (k < j) return;
    }
    if (k < j) hi = j; else lo = j;
  }
  for (uint32_t i = lo + 1; i < hi; ++i) {
    const uint32_t v = a[i];
    uint32_t m = i;
    for (; m > lo && a[m - 1] > v; --m) a[m] = a[m - 1];
    a[m] = v;
  }
}

struct Scratch {
  uint64_t count[kIds];  // low 32 bits requests, high 32 bits 5xx
  uint32_t pos[kIds];
  std::vector<uint32_t> lat;  // one hour's latencies grouped by endpoint
  Scratch() { lat.reserve(1 << 17); }
  std::vector<uint16_t> order;
};

struct Job {
  const char *base;
  size_t size;
  uint32_t lag, nblocks, threads;
  int fd;
  Names names;
  std::mutex mu, out_mu;
  std::condition_variable cv, out_cv;
  uint32_t next_block = 0;
  std::vector<Run> runs[kHours];
  std::vector<Rec *> pool;
  std::vector<Part> spare;  // written output buffers
  std::vector<std::unique_ptr<Scratch>> scratch;  // few hours finalize at once
  std::vector<uint8_t> done, block_max;
  uint32_t watermark = 0, prefix_max = 0, registered = 0, next_hour = 0;
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

  static const char *next_line(const char *p) {
    while (p[-1] != '\n') ++p;
    return p;
  }
  // Parses one line and returns the next; at least 256 readable bytes must follow p.
  __attribute__((always_inline)) const char *line(const char *p, Run (&cur)[kHours]) {
    const uint32_t hour = hour_index(p);
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
    const uint32_t id = names.find(ep, n);
    const char *s = q + k2 + 1;
    const uint32_t error = s[0] == '5';
    s += 4;
    uint64_t v;
    std::memcpy(&v, s, 8);
    v ^= 0x3030303030303030;
    const uint64_t non_digit = ((v + 0x7676767676767676) | v) & 0x8080808080808080;
    uint64_t lat;
    if (non_digit) {
      const uint32_t digits = static_cast<uint32_t>(__builtin_ctzll(non_digit)) >> 3;
      lat = digits8(v << (64 - 8 * digits));
      s += digits;
    } else {
      lat = digits8(v);
      for (s += 8; *s != '\n'; ++s) lat = lat * 10 + static_cast<uint64_t>(*s - '0');
    }
    Run &run = cur[hour];
    if (!run.rec) run.rec = take_run();
    run.rec[run.n++] = {static_cast<uint32_t>(lat), static_cast<uint16_t>(id | error << 11)};
    return s + 1;
  }
  // Parses the lines starting in [p, end) as kStreams independent streams so their latencies overlap.
  const char *lines(const char *p, const char *end, Run (&cur)[kHours]) {
    const char *at[kStreams + 1], *pos[kStreams];
    at[0] = p;
    for (uint32_t i = 1; i < kStreams; ++i) at[i] = p < end ? std::min(end, next_line(p + (end - p) * i / kStreams)) : p;
    at[kStreams] = end;
    std::copy(at, at + kStreams, pos);
    auto all_live = [&] {
      bool live = true;
      for (uint32_t i = 0; i < kStreams; ++i) live &= pos[i] < at[i + 1];
      return live;
    };
    while (all_live())
      for (uint32_t i = 0; i < kStreams; ++i) pos[i] = line(pos[i], cur);
    for (uint32_t i = 0; i < kStreams; ++i)
      while (pos[i] < at[i + 1]) pos[i] = line(pos[i], cur);
    return *std::max_element(pos, pos + kStreams);
  }

  void parse(uint32_t block) {
    Run cur[kHours] {};
    const char *end = align(block + 1);
    const char *tail = lines(align(block), std::min(end, base + size - std::min<size_t>(size, 256)), cur);
    if (tail < end) {  // the last lines are parsed from a zero-padded copy
      char copy[1024] {};
      std::memcpy(copy, tail, static_cast<size_t>(end - tail));
      lines(copy, copy + (end - tail), cur);
    }
    std::lock_guard<std::mutex> lock(mu);
    uint32_t hi = 0;
    for (uint32_t h = 0; h < kHours; ++h) {
      if (!cur[h].rec) continue;
      hi = h;
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
    { std::lock_guard<std::mutex> lock(mu);
      if (!spare.empty()) { part = std::move(spare.back()); spare.pop_back(); } }
    if (!part.data) part.data.reset(new char[kPartCap]);
    char *out = part.data.get();
    char label[22];
    static const char *const days[4] = {"2026-10-30", "2026-10-31", "2026-11-01", "2026-11-02"};
    std::snprintf(label, sizeof(label), "%sT%02u:00:00Z ", days[(hour + 18) / 24], (hour + 18) % 24);
    for (const uint16_t id : s.order) {
      const uint32_t n = static_cast<uint32_t>(s.count[id]), errors = static_cast<uint32_t>(s.count[id] >> 32);
      if (!n) continue;
      uint32_t *const first = s.lat.data() + s.pos[id] - n;
      const uint32_t r50 = (50 * n + 99) / 100 - 1, r95 = (95 * n + 99) / 100 - 1, r99 = (99 * n + 99) / 100 - 1;
      select(first, 0, n, r50);
      select(first, r50 + 1, n, r95);
      select(first, r95 + 1, n, r99);
      std::memcpy(out, label, 21);
      out += 21;
      std::memcpy(out, names.img[id], names.len[id]);
      out += names.len[id];
      for (const uint64_t v : {uint64_t{n}, uint64_t{first[r50]}, uint64_t{first[r95]}, uint64_t{first[r99]}}) {
        *out++ = ' ';
        append(out, v);
      }
      if (errors) {
        *out++ = ' ';
        append(out, errors);
        *out++ = ' ';
        append(out, uint64_t{errors} * 1000000 / n);
        *out++ = '\n';
      } else {
        std::memcpy(out, " 0 0\n", 5);
        out += 5;
      }
    }
    part.size = static_cast<size_t>(out - part.data.get());
    { std::lock_guard<std::mutex> lock(mu);
      for (const Run &r : mine) pool.push_back(r.rec); }
    std::lock_guard<std::mutex> lock(out_mu);
    parts[hour] = std::move(part);
    ready[hour] = true;
    out_cv.notify_one();
  }

  // Writes the hours in order, starts their writeback and drops older cached
  // output pages (best effort) so the page cache stays small.
  void write_parts() {
    off_t end[kHours + 1] {};
    for (uint32_t h = 0; h < kHours; ++h) {
      Part part;
      { std::unique_lock<std::mutex> lock(out_mu);
        out_cv.wait(lock, [&] { return ready[h]; });
        part = std::move(parts[h]); }
      for (size_t off = 0; off < part.size;) {
        const ssize_t w = write(fd, part.data.get() + off, part.size - off);
        if (w <= 0) fail("write summary");
        off += static_cast<size_t>(w);
      }
      end[h + 1] = end[h] + static_cast<off_t>(part.size);
      { std::lock_guard<std::mutex> lock(mu);
        spare.push_back(std::move(part)); }
      sync_file_range(fd, end[h], end[h + 1] - end[h], SYNC_FILE_RANGE_WRITE);
      if (h >= kKeep) posix_fadvise(fd, 0, end[h - kKeep + 1], POSIX_FADV_DONTNEED);
    }
  }

  bool claimable() const { return next_hour < kHours && (registered == nblocks || next_hour + lag <= prefix_max); }

  // Finalizes hours once they are safe and otherwise parses blocks, but never
  // more than kAhead blocks past the oldest unfinished one.
  void work() {
    std::unique_lock<std::mutex> lock(mu);
    for (;;) {
      if (claimable()) {
        const uint32_t hour = next_hour++;
        if (next_hour >= kHours) cv.notify_all();
        if (scratch.empty()) scratch.push_back(std::make_unique<Scratch>());
        std::unique_ptr<Scratch> s = std::move(scratch.back());
        scratch.pop_back();
        lock.unlock();
        finalize(hour, *s);
        lock.lock();
        scratch.push_back(std::move(s));
      } else if (next_block < nblocks && next_block < watermark + kAhead) {
        const uint32_t block = next_block++;
        lock.unlock();
        parse(block);
        lock.lock();
      } else if (next_hour >= kHours) {
        return;
      } else {
        cv.wait(lock);
      }
    }
  }
  bool run() {
    std::vector<std::thread> pool_threads;
    for (uint32_t i = 0; i < threads; ++i) pool_threads.emplace_back([this] { work(); });
    write_parts();
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
