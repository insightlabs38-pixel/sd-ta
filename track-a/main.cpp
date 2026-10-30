#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <functional>
#include <string>
#include <string_view>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace {
constexpr uint32_t kHours = 72;
constexpr uint32_t kEndpoints = 1842;
constexpr uint32_t kGroups = kHours * kEndpoints;

struct Record {
  uint32_t group;
  uint32_t latency;
};
struct Group {
  uint32_t count = 0;
  uint32_t errors = 0;
  uint32_t cursor = 0;
};

[[noreturn]] void fail(const char *message) {
  std::perror(message);
  std::exit(1);
}

uint32_t hour_index(const char *p) {
  const uint32_t day = (p[8] - '0') * 10 + (p[9] - '0');
  const uint32_t hour = (p[11] - '0') * 10 + (p[12] - '0');
  return p[6] == '0' ? (day - 30) * 24 + hour - 18
                     : 30 + (day - 1) * 24 + hour;
}

void append_number(std::string &out, uint64_t value) {
  char buffer[24];
  const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
  out.append(buffer, result.ptr);
}

void write_all(int fd, const std::string &out) {
  size_t offset = 0;
  while (offset < out.size()) {
    const ssize_t written = write(fd, out.data() + offset, out.size() - offset);
    if (written <= 0) fail("write summary");
    offset += static_cast<size_t>(written);
  }
}
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
  if (bytes) madvise(const_cast<char *>(mapped), bytes, MADV_SEQUENTIAL);

  struct Slot {
    uint64_t hash = 0;
    uint32_t id_plus_one = 0;
  };
  std::array<Slot, 4096> dictionary {};
  const std::hash<std::string_view> hasher;
  std::vector<std::string_view> names;
  names.reserve(kEndpoints);
  std::vector<Group> groups(kGroups);
  std::array<std::array<char, 13>, kHours> hours {};
  std::vector<Record> records;
  records.reserve(bytes / 70 + 128);

  const char *p = mapped;
  const char *end = mapped ? mapped + bytes : nullptr;
  while (p != end) {
    const char *const line = p;
    const uint32_t hour = hour_index(line);
    p += 28; // Fixed 27-byte UTC timestamp followed by a space.
    while (*p != ' ') ++p; // Ignore method.
    const char *const endpoint = ++p;
    while (*p != ' ') ++p;
    const std::string_view name(endpoint, static_cast<size_t>(p - endpoint));
    const uint64_t hash = hasher(name);
    uint32_t pos = static_cast<uint32_t>(hash) & 4095;
    while (dictionary[pos].id_plus_one &&
           !(dictionary[pos].hash == hash && names[dictionary[pos].id_plus_one - 1] == name)) {
      pos = (pos + 1) & 4095;
    }
    uint32_t id;
    if (!dictionary[pos].id_plus_one) {
      id = static_cast<uint32_t>(names.size());
      if (id >= kEndpoints) fail("too many endpoints");
      names.push_back(name);
      dictionary[pos] = {hash, id + 1};
    } else {
      id = dictionary[pos].id_plus_one - 1;
    }
    const bool error = p[1] == '5'; // Status is exactly three digits, 100..599.
    p += 5; // Space, three status digits, space.
    uint32_t latency = 0;
    while (*p != '\n') latency = latency * 10 + static_cast<uint32_t>(*p++ - '0');
    ++p;

    const uint32_t key = hour * kEndpoints + id;
    Group &group = groups[key];
    if (group.count++ == 0) std::memcpy(hours[hour].data(), line, 13);
    group.errors += error;
    records.push_back({key, latency});
  }

  uint32_t prefix = 0;
  for (Group &group : groups) {
    group.cursor = prefix;
    prefix += group.count;
  }
  std::vector<uint32_t> latencies(records.size());
  for (const Record &record : records) latencies[groups[record.group].cursor++] = record.latency;
  records.clear();
  records.shrink_to_fit();

  std::vector<uint32_t> order(names.size());
  for (uint32_t i = 0; i < order.size(); ++i) order[i] = i;
  std::sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) { return names[a] < names[b]; });

  std::string output;
  output.reserve(bytes / 20 + 4096);
  for (uint32_t hour = 0; hour < kHours; ++hour) {
    for (const uint32_t id : order) {
      const Group &group = groups[hour * kEndpoints + id];
      const uint32_t n = group.count;
      if (!n) continue;
      uint32_t *const first = latencies.data() + group.cursor - n;
      const uint32_t r50 = (50 * n + 99) / 100 - 1;
      const uint32_t r95 = (95 * n + 99) / 100 - 1;
      const uint32_t r99 = (99 * n + 99) / 100 - 1;
      if (n <= 256) {
        std::sort(first, first + n);
      } else {
        std::nth_element(first, first + r99, first + n);
        std::nth_element(first, first + r95, first + r99);
        std::nth_element(first, first + r50, first + r95);
      }
      output.append(hours[hour].data(), 13);
      output.append(":00:00Z ");
      output.append(names[id]);
      output.push_back(' ');
      append_number(output, n);
      output.push_back(' ');
      append_number(output, first[r50]);
      output.push_back(' ');
      append_number(output, first[r95]);
      output.push_back(' ');
      append_number(output, first[r99]);
      output.push_back(' ');
      append_number(output, group.errors);
      output.push_back(' ');
      append_number(output, static_cast<uint64_t>(group.errors) * 1000000 / n);
      output.push_back('\n');
    }
  }

  const char *const out_dir = std::getenv("SD_OUT_DIR");
  const std::string output_path = std::string(out_dir ? out_dir : "/out") + "/summary.txt";
  const int fd = open(output_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0) fail("open summary");
  write_all(fd, output);
  if (close(fd) != 0) fail("close summary");
  if (bytes) munmap(const_cast<char *>(mapped), bytes);
}
