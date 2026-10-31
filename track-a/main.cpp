// Track A: per-hour, per-endpoint request counts, p50/p95/p99 and 5xx rates.
// The log is nearly time-ordered: blocks are parsed into per-hour runs and an
// hour is finalized once the finished file prefix is kLag hours past it.
// A late line that breaks this is detected and the run is repeated exactly.
#include <algorithm>
#include <atomic>
#include <charconv>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <immintrin.h>
#include <memory>
#include <mutex>
#include <sched.h>
#include <sys/mman.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {
constexpr uint32_t kHours = 72, kIds = 2048, kBlock = 256 << 10, kStreams = 4;
constexpr uint32_t kLag = 2, kNever = 1000, kAhead = 48, kKeep = 2;
constexpr uint32_t kRunCap = kBlock / 40 + 16, kPartCap = kIds * 256;
const char *const kDays[] = {"2026-10-30", "2026-10-31", "2026-11-01",
                             "2026-11-02"};

struct __attribute__((packed)) Rec {
  uint32_t lat;
  uint16_t tag;
}; // tag: id | 5xx << 11
using Run = std::pair<Rec *, uint32_t>;
using Part =
    std::pair<std::unique_ptr<char[]>, size_t>; // output text, its length

[[noreturn]] void fail(const char *what) {
  std::perror(what);
  std::exit(1);
}
__m256i load(const void *p) {
  return _mm256_loadu_si256(static_cast<const __m256i *>(p));
}
uint64_t load64(const void *p) {
  uint64_t v;
  std::memcpy(&v, p, 8);
  return v;
}
uint32_t spaces(const char *p) {
  return _mm256_movemask_epi8(
      _mm256_cmpeq_epi8(load(p), _mm256_set1_epi8(' ')));
}
// Index of the first space at or after q + 64.
uint32_t far_space(const char *q) {
  uint32_t k = 64, m;
  while (!(m = spaces(q + k)))
    k += 32;
  return k + __builtin_ctz(m);
}
// The value of eight decimal digits (0..9 per byte, most significant first).
uint64_t digits8(uint64_t v) {
  __m128i x = _mm_maddubs_epi16(_mm_cvtsi64_si128(v), _mm_set1_epi16(0x010a));
  x = _mm_madd_epi16(x, _mm_set1_epi32(0x00010064));
  const uint64_t r = _mm_cvtsi128_si64(x);
  return (r & 0xffffffff) * 10000 + (r >> 32);
}

// Places the k-th smallest of a[lo, hi) at a[k], smaller values before it and
// larger ones after (branchless Lomuto partitions; ranges <= 8 are sorted).
void select(uint32_t *a, uint32_t lo, uint32_t hi, uint32_t k) {
  for (int depth = 0; hi - lo > 8; ++depth) {
    if (depth > 48)
      return std::nth_element(a + lo, a + k, a + hi);
    const uint32_t x = a[lo], y = a[lo + (hi - lo) / 2], z = a[hi - 1];
    const uint32_t pivot =
        std::max(std::min(x, y), std::min(std::max(x, y), z));
    auto split = [&](auto before) {
      uint32_t j = lo;
      for (uint32_t i = lo; i < hi; ++i) {
        const uint32_t v = a[i];
        a[i] = a[j];
        a[j] = v;
        j += before(v);
      }
      return j;
    };
    uint32_t j = split([&](uint32_t v) { return v < pivot; });
    if (j == lo && k < (j = split([&](uint32_t v) { return v == pivot; })))
      return; // all duplicates
    (k < j ? hi : lo) = j;
  }
  for (uint32_t i = lo + 1; i < hi; ++i) {
    const uint32_t v = a[i];
    uint32_t m = i;
    for (; m > lo && a[m - 1] > v; --m)
      a[m] = a[m - 1];
    a[m] = v;
  }
}

// Dictionary inserts are tiny and bursty at startup, so waiters spin instead of
// sleeping.
struct SpinLock {
  std::atomic_flag held = ATOMIC_FLAG_INIT;
  void lock() {
    for (int i = 0; held.test_and_set(std::memory_order_acquire); ++i)
      if (i < 64)
        _mm_pause();
      else
        sched_yield();
  }
  void unlock() { held.clear(std::memory_order_release); }
};

// Exact endpoint dictionary: 8-way buckets, lock-free hits, locked inserts
// published by a release store of the tag (relies on x86 load ordering).
// Keys are stored zero-padded after a space, so n + 1 equal bytes prove
// equality.
struct Names {
  struct alignas(64) Bucket {
    uint32_t tag[8];
    uint16_t id[8];
  };
  Bucket buckets[512]{};
  alignas(64) char img[kIds][128]{};
  uint32_t len[kIds]{}, size = 0;
  std::vector<uint16_t> sorted; // ids in byte order of their names
  SpinLock mu;

  bool same(uint32_t id, const char *ep, uint32_t n) const {
    if (n < 32) {
      const uint32_t match = _mm256_movemask_epi8(
          _mm256_cmpeq_epi8(load(ep), load(img[id])));
      return !_bzhi_u32(~match, n + 1);
    }
    auto eq = [&](int i) {
      return uint64_t(uint32_t(_mm256_movemask_epi8(
                 _mm256_cmpeq_epi8(load(ep + i), load(img[id] + i)))))
             << i;
    };
    return !_bzhi_u64(~(eq(0) | eq(32)), n + 1) &&
           (n < 64 ||
            (len[id] == n && !std::memcmp(img[id] + 64, ep + 64, n - 64)));
  }
  // The id of the key, or -1 with b set to the bucket it belongs in.
  int lookup(const char *ep, uint32_t n, uint32_t h, Bucket *&b) {
    for (uint32_t i = h >> 16;; ++i) {
      b = buckets + (i & 511);
      for (uint32_t lane = 0; lane < 8; ++lane) {
        const uint32_t tag = __atomic_load_n(&b->tag[lane], __ATOMIC_ACQUIRE);
        if (tag == h && same(b->id[lane], ep, n))
          return b->id[lane];
        if (!tag) return -1;
      }
    }
  }
  __attribute__((always_inline)) uint32_t find(const char *ep, uint32_t n) {
    const uint64_t keep = n < 8 ? n * 8 : 64;
    const uint64_t x = _bzhi_u64(load64(ep), keep),
                   y = _bzhi_u64(load64(ep + std::max(n, 8u) - 8), keep);
    const uint32_t h = _mm_crc32_u64(_mm_crc32_u64(n, x), y) | 1;
    Bucket *b;
    const int id = lookup(ep, n, h, b);
    return id >= 0 ? id : add(ep, n, h);
  }
  uint32_t add(const char *ep, uint32_t n, uint32_t h) {
    std::lock_guard<SpinLock> lock(mu);
    Bucket *b;
    if (const int found = lookup(ep, n, h, b); found >= 0)
      return found;
    const uint32_t id = size++;
    uint32_t slot = 0;
    while (__atomic_load_n(&b->tag[slot], __ATOMIC_RELAXED))
      ++slot;
    if (id >= kIds)
      fail("too many endpoints");
    std::memcpy(img[id], ep, n);
    img[id][n] = ' ';
    len[id] = n;
    sorted.insert(std::lower_bound(sorted.begin(), sorted.end(), id,
                                   [&](uint32_t x, uint32_t y) {
                                     return std::memcmp(img[x], img[y], 128) <
                                            0;
                                   }),
                  id);
    b->id[slot] = id;
    __atomic_store_n(&b->tag[slot], h, __ATOMIC_RELEASE);
    return id;
  }
};

struct Job {
  const char *base;
  size_t size;
  uint32_t lag, nblocks, threads;
  int fd;
  Names names;
  std::mutex mu, out_mu;
  std::condition_variable cv;
  std::vector<Run> runs[kHours]; // per hour, from the registered blocks
  std::vector<int> top; // per block: its last hour, or -1 until registered
  uint32_t next_block = 0, watermark = 0, prefix_max = 0, registered = 0,
           next_hour = 0;
  bool bad = false; // a line arrived after its hour was finalized
  Part parts[kHours];
  off_t end[kHours + 1]{}; // output offset after each hour
  uint32_t written = 0;    // hours written so far
  bool writing = false;
  std::vector<Rec *> runbufs; // free buffers, guarded by mu
  std::vector<std::vector<uint32_t>> latbufs;
  std::vector<std::unique_ptr<char[]>> outbufs;

  Job(const char *b, size_t n, uint32_t l, int f, uint32_t t)
      : base(b), size(n), lag(l), nblocks((n + kBlock - 1) / kBlock),
        threads(t), fd(f), top(nblocks, -1) {}
  ~Job() {
    for (Rec *r : runbufs)
      std::free(r);
  }

  template <class T> T take(std::vector<T> &pool) {
    std::lock_guard<std::mutex> lock(mu);
    if (pool.empty())
      return T();
    T t = std::move(pool.back());
    pool.pop_back();
    return t;
  }
  static const char *next_line(const char *p) {
    while (p[-1] != '\n')
      ++p;
    return p;
  }
  const char *align(uint32_t block) const {
    return block ? next_line(base +
                             std::min<size_t>(size_t{block} * kBlock, size))
                 : base;
  }

  // Parses one line and returns the next; 256 readable bytes must follow p.
  __attribute__((always_inline)) const char *
  line(const char *p, Rec *(&wp)[kHours], Rec *(&bp)[kHours]) {
    const uint32_t hour = (p[9] + (p[6] & 1)) * 24 + p[11] * 10 + p[12] - 1698;
    const char *q = p + 28;
    uint64_t m = spaces(q) | uint64_t{spaces(q + 32)} << 32;
    const uint32_t k1 = __builtin_ctzll(m);
    m &= m - 1;
    const uint32_t k2 = m ? __builtin_ctzll(m) : far_space(q);
    const uint32_t id = names.find(q + k1 + 1, k2 - k1 - 1);
    const char *s = q + k2 + 1;
    const uint32_t error = *s == '5';
    s += 4;
    uint64_t v = load64(s) ^ 0x3030303030303030, lat;
    const uint64_t other = ((v + 0x7676767676767676) | v) & 0x8080808080808080;
    if (other) {
      const uint32_t digits = __builtin_ctzll(other) >> 3;
      lat = digits8(v << (64 - 8 * digits));
      s += digits;
    } else {
      for (lat = digits8(v), s += 8; *s != '\n'; ++s)
        lat = lat * 10 + (*s - '0');
    }
    Rec *&w = wp[hour];
    if (!w) {
      Rec *r = take(runbufs);
      w = bp[hour] =
          r ? r : static_cast<Rec *>(std::malloc(kRunCap * sizeof(Rec) + sizeof(uint64_t)));
    }
    const uint64_t rec = lat | uint64_t{id | error << 11} << 32;
    std::memcpy(w++, &rec, sizeof(Rec));
    return s + 1;
  }

  void parse(uint32_t block) {
    Rec *wp[kHours]{}, *bp[kHours]{};
    const char *pos[kStreams],
        *stop[kStreams]; // interleaved streams overlap their latencies
    const char *p = align(block), *end = align(block + 1);
    for (uint32_t i = 0; i < kStreams; ++i)
      pos[i] = i ? next_line(p + (end - p) * i / kStreams) : p;
    std::copy(pos + 1, pos + kStreams, stop);
    stop[kStreams - 1] = end;
    auto live = [&] {
      return std::equal(pos, pos + kStreams, stop, std::less<>());
    };
    while (live())
      for (uint32_t i = 0; i < kStreams; ++i)
        pos[i] = line(pos[i], wp, bp);
    for (uint32_t i = 0; i < kStreams; ++i)
      while (pos[i] < stop[i])
        pos[i] = line(pos[i], wp, bp);

    std::lock_guard<std::mutex> lock(mu);
    int last = 0;
    for (uint32_t h = 0; h < kHours; ++h) {
      if (!bp[h])
        continue;
      last = h;
      if (h < next_hour) {
        bad = true;
        runbufs.push_back(bp[h]);
      } else
        runs[h].push_back({bp[h], static_cast<uint32_t>(wp[h] - bp[h])});
    }
    top[block] = last;
    for (++registered; watermark < nblocks && top[watermark] >= 0; ++watermark)
      prefix_max = std::max<int>(prefix_max, top[watermark]);
    cv.notify_all();
  }

  void finalize(uint32_t hour) {
    std::vector<Run> mine;
    std::vector<uint16_t> order;
    {
      std::lock_guard<std::mutex> lock(mu);
      mine.swap(runs[hour]);
      std::lock_guard<SpinLock> names_lock(names.mu);
      order = names.sorted;
    }
    uint32_t count[2 * kIds] = {}, pos[kIds],
                       total = 0; // by tag: ids, then their 5xx
    for (const auto &[rec, n] : mine)
      for (uint32_t i = 0; i < n; ++i)
        ++count[rec[i].tag];
    for (const uint16_t id : order) {
      pos[id] = total;
      total += count[id] + count[id + kIds];
    }
    std::vector<uint32_t> lat = take(latbufs);
    lat.reserve(1 << 17);
    lat.resize(total);
    for (const auto &[rec, n] : mine)
      for (uint32_t i = 0; i < n; ++i)
        lat[pos[rec[i].tag % kIds]++] = rec[i].lat;

    std::unique_ptr<char[]> text = take(outbufs);
    if (!text)
      text.reset(new char[kPartCap]);
    char *out = text.get(), label[22];
    std::snprintf(label, sizeof label, "%sT%02u:00:00Z ",
                  kDays[(hour + 18) / 24], (hour + 18) % 24);
    auto put = [&](uint64_t v) {
      *out++ = ' ';
      out = std::to_chars(out, out + 20, v).ptr;
    };
    for (const uint16_t id : order) {
      const uint32_t errors = count[id + kIds], n = count[id] + errors;
      if (!n)
        continue;
      uint32_t *const a = lat.data() + pos[id] - n;
      const uint32_t r50 = (50 * n + 99) / 100 - 1,
                     r95 = (95 * n + 99) / 100 - 1,
                     r99 = (99 * n + 99) / 100 - 1;
      select(a, 0, n, r50);
      select(a, r50 + 1, n, r95);
      select(a, r95 + 1, n, r99);
      std::memcpy(out, label, 21);
      std::memcpy(out + 21, names.img[id], names.len[id]);
      out += 21 + names.len[id];
      for (const uint64_t v : {uint64_t{n}, uint64_t{a[r50]}, uint64_t{a[r95]},
                               uint64_t{a[r99]}, uint64_t{errors}})
        put(v);
      put(errors ? uint64_t{errors} * 1000000 / n : 0);
      *out++ = '\n';
    }
    const size_t length = out - text.get();
    {
      std::lock_guard<std::mutex> lock(mu);
      latbufs.push_back(std::move(lat));
      for (const auto &[rec, n] : mine)
        runbufs.push_back(rec);
    }
    emit(hour, {std::move(text), length});
  }

  // Whoever completes an hour writes all hours that are ready in order (a
  // thread that finds a writer active just leaves its part), starts their
  // writeback and drops older cached output pages (best effort).
  void emit(uint32_t hour, Part ready) {
    std::unique_lock<std::mutex> lock(out_mu);
    parts[hour] = std::move(ready);
    if (writing)
      return;
    for (writing = true; written < kHours && parts[written].first; ++written) {
      Part part = std::move(parts[written]);
      lock.unlock();
      for (size_t done = 0; done < part.second;) {
        const ssize_t w =
            write(fd, part.first.get() + done, part.second - done);
        if (w <= 0)
          fail("write summary");
        done += w;
      }
      end[written + 1] = end[written] + part.second;
      sync_file_range(fd, end[written], part.second, SYNC_FILE_RANGE_WRITE);
      if (written >= kKeep)
        posix_fadvise(fd, 0, end[written - kKeep + 1], POSIX_FADV_DONTNEED);
      {
        std::lock_guard<std::mutex> pool_lock(mu);
        outbufs.push_back(std::move(part.first));
      }
      lock.lock();
    }
    writing = false;
  }

  bool claimable() const {
    return next_hour < kHours &&
           (registered == nblocks || next_hour + lag <= prefix_max);
  }

  // Finalizes hours once they are safe and otherwise parses blocks, but never
  // more than kAhead blocks past the oldest unfinished one.
  void work(uint32_t index) {
    std::unique_lock<std::mutex> lock(mu);
    for (;;) {
      if (claimable()) {
        const uint32_t hour = next_hour++;
        cv.notify_all();
        lock.unlock();
        finalize(hour);
        lock.lock();
      } else if (next_block < nblocks && next_block < watermark + kAhead) {
        const uint32_t block = next_block++;
        lock.unlock();
        parse(block);
        lock.lock();
      } else if (next_hour >= kHours) {
        lock.unlock(); // drop this thread's share of the file mapping so that
                       // exit is cheaper
        const size_t slice = (size / threads + 4095) / 4096 * 4096,
                     from = std::min(size, index * slice);
        madvise(const_cast<char *>(base) + from, std::min(slice, size - from),
                MADV_DONTNEED);
        return;
      } else {
        cv.wait(lock);
      }
    }
  }
  bool run() {
    std::vector<std::thread> pool;
    for (uint32_t i = 0; i < threads; ++i)
      pool.emplace_back([this, i] { work(i); });
    for (auto &t : pool)
      t.join();
    return !bad;
  }
};
} // namespace

int main() {
  const char *const dir = std::getenv("SD_DATA_DIR"),
                    *const out = std::getenv("SD_OUT_DIR"),
                    *const forced = std::getenv("SD_THREADS");
  const int in = open(
      (std::string(dir ? dir : "/data") + "/requests.log").c_str(), O_RDONLY);
  const size_t bytes = in < 0 ? 0 : lseek(in, 0, SEEK_END);
  // Zero pages follow the file so that the last lines can be scanned past its
  // end.
  char *map = static_cast<char *>(mmap(nullptr, bytes + 8192, PROT_READ,
                                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
  if (in < 0 || map == MAP_FAILED ||
      (bytes && mmap(map, bytes, PROT_READ, MAP_PRIVATE | MAP_FIXED, in, 0) ==
                    MAP_FAILED))
    fail("map requests");
  const int fd =
      open((std::string(out ? out : "/out") + "/summary.txt").c_str(),
           O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0)
    fail("open summary");
  cpu_set_t cpus{};
  sched_getaffinity(0, sizeof cpus, &cpus);
  const uint32_t threads = forced ? std::atoi(forced) : std::min(6, CPU_COUNT(&cpus));
  if (!std::make_unique<Job>(map, bytes, kLag, fd, threads)->run()) {
    if (ftruncate(fd, 0) || lseek(fd, 0, SEEK_SET))
      fail("rewind summary");
    std::make_unique<Job>(map, bytes, kNever, fd, threads)->run();
  }
}
