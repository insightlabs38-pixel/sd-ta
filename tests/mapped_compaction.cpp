// Exercise the production compactor with overlapping spans and page fringes.
#define main submission_main
#include "../track-a/main.cpp"
#undef main
#include <cassert>
#include <sys/stat.h>

static size_t cgroup_value(const char *path) {
  FILE *f = std::fopen(path, "r");
  assert(f);
  size_t n;
  assert(std::fscanf(f, "%zu", &n) == 1);
  std::fclose(f);
  return n;
}
int main() {
  const size_t page = sysconf(_SC_PAGESIZE);
  int fd = open("/out/summary.txt", O_RDWR | O_CREAT | O_TRUNC, 0644);
  assert(fd >= 0);
  auto job = new Job(nullptr, 0, kLag, fd, 1);
  size_t total = 0;
  for (uint32_t h = 0; h < kHours; ++h) {
    size_t n = kPartCap / 2 + h * 17;
    if (h < 2) n = kPartCap - 13 - h * 16;
    if (h == 3) n = 1;
    if (h == 4) n = 0;
    if (h == 35) n = page - 1;
    if (h == 36) n = page + 1;
    if (h == 70) n = kPartCap - 1;
    if (h == 71) n = 2 * page + 17;
    job->length[h] = n;
    total += n;
  }
  std::vector<char> expected(total);
  size_t at = 0;
  for (uint32_t h = 0; h < kHours; ++h) {
    for (size_t i = 0; i < job->length[h]; ++i)
      expected[at + i] = job->output[size_t{h} * kPartCap + i] =
          char(1 + (h * 29 + i * 17) % 126);
    at += job->length[h];
  }
  const size_t before = cgroup_value("/sys/fs/cgroup/memory.current");
  const size_t previous_peak = cgroup_value("/sys/fs/cgroup/memory.peak");
  job->compact();
  const size_t after_peak = cgroup_value("/sys/fs/cgroup/memory.peak");
  // Only the next hour's destination and page fringes can increase backing.
  // A second whole output allocation would exceed this bound by ~18 MiB.
  assert(after_peak <= std::max(previous_peak, before + kPartCap + (1 << 20)));
  struct stat st;
  assert(!fstat(fd, &st));
  assert(size_t(st.st_size) == total);
  assert(size_t(st.st_blocks) * 512 == (total + page - 1) / page * page);
  std::printf("COMPACTION before=%zu previous_peak=%zu after_peak=%zu bytes=%zu blocks=%zu\n",
              before, previous_peak, after_peak, total, size_t(st.st_blocks) * 512);
  // Allocate readback only after checking the compaction peak.
  std::vector<char> actual(total);
  assert(pread(fd, actual.data(), total, 0) == ssize_t(total));
  assert(actual == expected);
  delete job;
  close(fd);
}
