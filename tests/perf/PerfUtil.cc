/*
Copyright (c) 2026, XCDF contributors
All rights reserved.
*/

#include "PerfTest.h"

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <iostream>

namespace xcdf_perf {

uint64_t FileSize(const std::string& path) {
  struct stat st;
  if (stat(path.c_str(), &st) != 0) {
    return 0;
  }
  return static_cast<uint64_t>(st.st_size);
}

void EnsureDirectory(const std::string& path) {
  if (path.empty() || path == ".") {
    return;
  }
  if (mkdir(path.c_str(), 0777) != 0 && errno != EEXIST) {
    std::cerr << "Unable to create output directory: " << path << std::endl;
    std::exit(2);
  }
}

uint64_t NextRandom(uint64_t& state) {
  state = state * 2862933555777941757ULL + 3037000493ULL;
  return state;
}

std::vector<uint64_t> SeekPositions(uint64_t events, unsigned count) {
  std::vector<uint64_t> positions;
  if (events == 0 || count == 0) {
    return positions;
  }
  positions.reserve(count);
  uint64_t state = 0x5eed123456789abcULL;
  for (unsigned i = 0; i < count; ++i) {
    positions.push_back(NextRandom(state) % events);
  }
  return positions;
}

std::vector<Scenario> AllScenarios() {
  std::vector<Scenario> scenarios = CoreScenarios();
  std::vector<Scenario> features = FeatureScenarios();
  scenarios.insert(scenarios.end(), features.begin(), features.end());
  return scenarios;
}

} // namespace xcdf_perf
