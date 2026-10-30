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
#include <thread>
#include <unistd.h>
#include <vector>

namespace {
constexpr uint32_t kHours = 72;
constexpr uint32_t kEndpoints = 1842;
constexpr uint32_t kGroups = kHours * kEndpoints;
constexpr uint32_t kParseWorkers = 5;
constexpr uint32_t kOutputWorkers = 4;

struct Record { uint32_t group, latency; };
struct Group { uint32_t count = 0, errors = 0, cursor = 0; };
struct Slot { uint64_t hash = 0; uint32_t id_plus_one = 0; };

[[noreturn]] void fail(const char *message) {
  std::perror(message);
  std::exit(1);
}
uint32_t hour_index(const char *p) {
  const uint32_t day = (p[8] - '0') * 10 + (p[9] - '0');
  const uint32_t hour = (p[11] - '0') * 10 + (p[12] - '0');
  return p[6] == '0' ? (day - 30) * 24 + hour - 18 : 30 + (day - 1) * 24 + hour;
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
struct Worker {
  std::array<Slot, 4096> dictionary {};
  std::vector<std::string_view> names;
  std::vector<Group> groups = std::vector<Group>(kGroups);
  std::array<std::array<char, 13>, kHours> hours {};
  std::array<uint8_t, kHours> seen {};
  std::vector<Record> records;

  void parse(const char *p, const char *end) {
    const std::hash<std::string_view> hasher;
    names.reserve(kEndpoints);
    records.reserve(static_cast<size_t>(end - p) / 70 + 128);
    while (p < end) {
      const char *const line = p;
      const uint32_t hour = hour_index(p);
      p += 28;
      while (*p != ' ') ++p;
      const char *const endpoint = ++p;
      p = static_cast<const char *>(std::memchr(p, ' ', static_cast<size_t>(end - p)));
      const std::string_view name(endpoint, static_cast<size_t>(p - endpoint));
      const uint64_t hash = hasher(name);
      uint32_t pos = static_cast<uint32_t>(hash) & 4095;
      while (dictionary[pos].id_plus_one &&
             !(dictionary[pos].hash == hash && names[dictionary[pos].id_plus_one - 1] == name))
        pos = (pos + 1) & 4095;
      uint32_t id;
      if (!dictionary[pos].id_plus_one) {
        id = static_cast<uint32_t>(names.size());
        if (id >= kEndpoints) fail("too many endpoints");
        names.push_back(name);
        dictionary[pos] = {hash, id + 1};
      } else {
        id = dictionary[pos].id_plus_one - 1;
      }
      const bool error = p[1] == '5';
      p += 5;
      uint32_t latency = 0;
      while (*p != '\n') latency = latency * 10 + static_cast<uint32_t>(*p++ - '0');
      ++p;
      const uint32_t key = hour * kEndpoints + id;
      Group &group = groups[key];
      ++group.count;
      group.errors += error;
      if (!seen[hour]) {
        seen[hour] = 1;
        std::memcpy(hours[hour].data(), line, 13);
      }
      records.push_back({key, latency});
    }
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
  if (bytes) madvise(const_cast<char *>(mapped), bytes, MADV_SEQUENTIAL);

  std::vector<Worker> workers(kParseWorkers);
  std::array<const char *, kParseWorkers + 1> bounds {};
  if (bytes) {
    bounds[0] = mapped;
    bounds[kParseWorkers] = mapped + bytes;
    for (uint32_t i = 1; i < kParseWorkers; ++i) {
      const char *p = mapped + bytes * i / kParseWorkers;
      while (p < mapped + bytes && p[-1] != '\n') ++p;
      bounds[i] = p;
    }
    std::array<std::thread, kParseWorkers - 1> threads;
    for (uint32_t i = 1; i < kParseWorkers; ++i)
      threads[i - 1] = std::thread([&, i] { workers[i].parse(bounds[i], bounds[i + 1]); });
    workers[0].parse(bounds[0], bounds[1]);
    for (auto &thread : threads) thread.join();
  }

  std::array<Slot, 4096> dictionary {};
  const std::hash<std::string_view> hasher;
  std::vector<std::string_view> names;
  names.reserve(kEndpoints);
  std::vector<Group> groups(kGroups);
  std::array<std::array<char, 13>, kHours> hours {};
  std::vector<std::vector<uint32_t>> remaps(kParseWorkers);
  for (uint32_t w = 0; w < kParseWorkers; ++w) {
    Worker &worker = workers[w];
    remaps[w].reserve(worker.names.size());
    for (const std::string_view name : worker.names) {
      const uint64_t hash = hasher(name);
      uint32_t pos = static_cast<uint32_t>(hash) & 4095;
      while (dictionary[pos].id_plus_one &&
             !(dictionary[pos].hash == hash && names[dictionary[pos].id_plus_one - 1] == name))
        pos = (pos + 1) & 4095;
      uint32_t id;
      if (!dictionary[pos].id_plus_one) {
        id = static_cast<uint32_t>(names.size());
        if (id >= kEndpoints) fail("too many endpoints");
        names.push_back(name);
        dictionary[pos] = {hash, id + 1};
      } else {
        id = dictionary[pos].id_plus_one - 1;
      }
      remaps[w].push_back(id);
    }
    for (uint32_t hour = 0; hour < kHours; ++hour) {
      if (worker.seen[hour]) std::memcpy(hours[hour].data(), worker.hours[hour].data(), 13);
      for (uint32_t id = 0; id < worker.names.size(); ++id) {
        const Group &src = worker.groups[hour * kEndpoints + id];
        Group &dst = groups[hour * kEndpoints + remaps[w][id]];
        dst.count += src.count;
        dst.errors += src.errors;
      }
    }
  }

  uint32_t prefix = 0;
  for (Group &group : groups) {
    group.cursor = prefix;
    prefix += group.count;
  }
  std::vector<uint32_t> latencies(prefix);
  std::array<std::vector<uint32_t>, kParseWorkers> cursors;
  for (uint32_t w = 0; w < kParseWorkers; ++w) {
    cursors[w].assign(kGroups, 0);
    for (uint32_t hour = 0; hour < kHours; ++hour) {
      for (uint32_t id = 0; id < workers[w].names.size(); ++id) {
        const uint32_t key = hour * kEndpoints + remaps[w][id];
        cursors[w][key] = workers[w].groups[hour * kEndpoints + id].count;
      }
    }
  }
  for (uint32_t key = 0; key < kGroups; ++key) {
    uint32_t offset = groups[key].cursor;
    for (uint32_t w = 0; w < kParseWorkers; ++w) {
      const uint32_t count = cursors[w][key];
      cursors[w][key] = offset;
      offset += count;
    }
    groups[key].cursor = offset;
  }
  auto scatter = [&](uint32_t w) {
    std::vector<uint32_t> &cursor = cursors[w];
    for (const Record &record : workers[w].records) {
      const uint32_t key = (record.group / kEndpoints) * kEndpoints + remaps[w][record.group % kEndpoints];
      latencies[cursor[key]++] = record.latency;
    }
  };
  std::array<std::thread, kParseWorkers - 1> scatter_threads;
  for (uint32_t w = 1; w < kParseWorkers; ++w)
    scatter_threads[w - 1] = std::thread(scatter, w);
  scatter(0);
  for (auto &thread : scatter_threads) thread.join();
  workers.clear();
  workers.shrink_to_fit();

  std::vector<uint32_t> order(names.size());
  for (uint32_t i = 0; i < order.size(); ++i) order[i] = i;
  std::sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) { return names[a] < names[b]; });

  std::array<std::string, kOutputWorkers> parts;
  auto produce = [&](uint32_t worker) {
    std::string &output = parts[worker];
    output.reserve(bytes / (20 * kOutputWorkers) + 4096);
    for (uint32_t hour = worker * (kHours / kOutputWorkers);
         hour < (worker + 1) * (kHours / kOutputWorkers); ++hour) {
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
          std::nth_element(first, first + r50, first + n);
          std::nth_element(first + r50 + 1, first + r95, first + n);
          std::nth_element(first + r95 + 1, first + r99, first + n);
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
  };
  std::array<std::thread, kOutputWorkers - 1> output_threads;
  for (uint32_t i = 1; i < kOutputWorkers; ++i)
    output_threads[i - 1] = std::thread(produce, i);
  produce(0);
  for (auto &thread : output_threads) thread.join();

  const char *const out_dir = std::getenv("SD_OUT_DIR");
  const std::string output_path = std::string(out_dir ? out_dir : "/out") + "/summary.txt";
  const int fd = open(output_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0) fail("open summary");
  for (const std::string &part : parts) write_all(fd, part);
  if (close(fd) != 0) fail("close summary");
  if (bytes) munmap(const_cast<char *>(mapped), bytes);
}
