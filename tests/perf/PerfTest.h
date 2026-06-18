/*
Copyright (c) 2026, XCDF contributors
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef XCDF_PERF_TEST_INCLUDED_H
#define XCDF_PERF_TEST_INCLUDED_H

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace xcdf_perf {

struct Options {
  uint64_t events;
  unsigned repetitions;
  unsigned seekCount;
  std::string outputDir;
  std::string scenario;
  bool csv;
  bool keepFiles;
  std::string fixturePath;

  Options() : events(200000),
              repetitions(3),
              seekCount(10000),
              outputDir("."),
              scenario("all"),
              csv(false),
              keepFiles(false),
              fixturePath("") { }
};

struct Result {
  std::string scenario;
  std::string operation;
  uint64_t events;
  uint64_t fileBytes;
  double seconds;
  uint64_t checksum;
};

struct Operation {
  typedef std::function<uint64_t(const std::string&, uint64_t, unsigned)> RunFn;
  typedef std::function<uint64_t(uint64_t, unsigned)> CountFn;

  std::string name;
  RunFn run;
  CountFn count;

  Operation(const std::string& operationName,
            RunFn operationRun,
            CountFn operationCount = CountFn()) :
      name(operationName), run(operationRun), count(operationCount) { }

  uint64_t GetMeasuredEvents(uint64_t events, unsigned seekCount) const {
    if (count) {
      return count(events, seekCount);
    }
    return events;
  }
};

struct Scenario {
  std::string name;
  std::string description;
  std::function<uint64_t(const std::string&, uint64_t)> write;
  std::vector<Operation> operations;
  bool usesFixture;

  Scenario(const std::string& scenarioName,
           const std::string& scenarioDescription,
           std::function<uint64_t(const std::string&, uint64_t)> scenarioWrite,
           const std::vector<Operation>& scenarioOperations,
           bool scenarioUsesFixture = false) :
      name(scenarioName),
      description(scenarioDescription),
      write(scenarioWrite),
      operations(scenarioOperations),
      usesFixture(scenarioUsesFixture) { }
};

inline uint64_t Mix(uint64_t hash, uint64_t value) {
  hash ^= value + 0x9e3779b97f4a7c15ULL + (hash << 6) + (hash >> 2);
  return hash;
}

inline uint64_t DoubleBits(double value) {
  uint64_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  return bits;
}

inline uint64_t SeekOperationCount(uint64_t events, unsigned seekCount) {
  return std::min<uint64_t>(events, seekCount);
}

inline uint64_t CappedSeekOperationCount(uint64_t events, unsigned seekCount) {
  return std::min<uint64_t>(std::min<uint64_t>(events, seekCount), 100);
}

inline uint64_t MetadataOperationCount(uint64_t events, unsigned seekCount) {
  (void)events;
  (void)seekCount;
  return 1;
}

uint64_t FileSize(const std::string& path);
void EnsureDirectory(const std::string& path);
std::vector<uint64_t> SeekPositions(uint64_t events, unsigned count);

std::vector<Scenario> CoreScenarios();
std::vector<Scenario> FeatureScenarios();
std::vector<Scenario> AllScenarios();

} // namespace xcdf_perf

#endif // XCDF_PERF_TEST_INCLUDED_H
