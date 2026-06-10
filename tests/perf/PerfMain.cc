/*
Copyright (c) 2026, XCDF contributors
All rights reserved.
*/

#include "PerfTest.h"

#include <xcdf/version.h>

#include <sys/utsname.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

using xcdf_perf::MetadataOperationCount;
using xcdf_perf::Operation;
using xcdf_perf::Options;
using xcdf_perf::Result;
using xcdf_perf::Scenario;

uint64_t ParseUnsigned(const std::string& value, const char* option) {
  char* end = NULL;
  unsigned long long parsed = std::strtoull(value.c_str(), &end, 10);
  if (!end || *end != '\0') {
    std::cerr << "Invalid value for " << option << ": " << value << std::endl;
    std::exit(2);
  }
  return static_cast<uint64_t>(parsed);
}

void Usage(const char* argv0) {
  std::cout << "Usage: " << argv0 << " [options]\n\n"
            << "Options:\n"
            << "  --events N        Events per write/read scenario (default: 200000)\n"
            << "  --repetitions N   Repetitions per scenario (default: 3)\n"
            << "  --seek-count N    Random seeks per scenario (default: 10000)\n"
            << "  --output-dir DIR  Directory for temporary XCDF files (default: .)\n"
            << "  --scenario NAME   Scenario to run, or 'all' (default: all)\n"
            << "  --fixture FILE    Existing XCDF file for fixture scenarios\n"
            << "  --csv             Emit CSV instead of a human-readable table\n"
            << "  --keep-files      Keep generated XCDF files\n"
            << "  --list            List scenarios and exit\n"
            << "  --help            Show this help\n";
}

std::string FileName(const Options& options,
                     const std::string& scenario,
                     unsigned repetition) {
  std::ostringstream os;
  os << options.outputDir;
  if (!options.outputDir.empty() &&
      options.outputDir[options.outputDir.size() - 1] != '/') {
    os << '/';
  }
  os << "xcdf-perf-" << scenario << "-" << repetition << ".xcd";
  return os.str();
}

std::string HostName() {
  char name[256];
  if (gethostname(name, sizeof(name)) == 0) {
    name[sizeof(name) - 1] = '\0';
    return name;
  }
  return "unknown";
}

std::string UnameString() {
  struct utsname u;
  if (uname(&u) == 0) {
    std::ostringstream os;
    os << u.sysname << " " << u.release << " " << u.machine;
    return os.str();
  }
  return "unknown";
}

std::string CpuModel() {
  std::ifstream in("/proc/cpuinfo");
  std::string line;
  while (std::getline(in, line)) {
    const std::string key = "model name";
    if (line.compare(0, key.size(), key) == 0) {
      std::string::size_type pos = line.find(':');
      if (pos != std::string::npos) {
        std::string value = line.substr(pos + 1);
        while (!value.empty() && value[0] == ' ') {
          value.erase(value.begin());
        }
        return value;
      }
    }
  }
  return "unknown";
}

void PrintMetadata(const Options& options) {
  std::cout << "# xcdf_perf_schema,2\n"
            << "# xcdf_version," << xcdf::get_version() << "\n"
            << "# xcdf_git_hash," << xcdf::get_git_hash() << "\n"
            << "# compiler," << __VERSION__ << "\n"
            << "# cplusplus," << __cplusplus << "\n"
            << "# build_date," << __DATE__ << " " << __TIME__ << "\n"
            << "# hostname," << HostName() << "\n"
            << "# uname," << UnameString() << "\n"
            << "# cpu_model," << CpuModel() << "\n"
            << "# events," << options.events << "\n"
            << "# repetitions," << options.repetitions << "\n"
            << "# seek_count," << options.seekCount << "\n"
            << "# scenario," << options.scenario << "\n"
            << "# fixture," << options.fixturePath << "\n";
}

void PrintResult(const Result& result, bool csv) {
  double eventRate = result.seconds > 0.0 ? result.events / result.seconds : 0.0;
  double mibRate = result.seconds > 0.0 ?
      (static_cast<double>(result.fileBytes) / (1024.0 * 1024.0)) / result.seconds : 0.0;
  double bytesPerEvent = result.events > 0 ?
      static_cast<double>(result.fileBytes) / result.events : 0.0;

  if (csv) {
    std::cout << result.scenario << ','
              << result.operation << ','
              << result.events << ','
              << result.fileBytes << ','
              << std::setprecision(9) << result.seconds << ','
              << eventRate << ','
              << mibRate << ','
              << bytesPerEvent << ','
              << result.checksum << '\n';
  } else {
    std::cout << std::left << std::setw(24) << result.scenario
              << std::setw(18) << result.operation
              << std::right << std::setw(12) << result.events
              << std::setw(14) << result.fileBytes
              << std::setw(12) << std::fixed << std::setprecision(4) << result.seconds
              << std::setw(16) << std::fixed << std::setprecision(1) << eventRate
              << std::setw(12) << std::fixed << std::setprecision(1) << mibRate
              << std::setw(12) << std::fixed << std::setprecision(2) << bytesPerEvent
              << "  " << result.checksum << '\n';
  }
}

Options ParseOptions(int argc, char** argv, bool& listOnly) {
  Options options;
  listOnly = false;
  for (int i = 1; i < argc; ++i) {
    std::string arg(argv[i]);
    if (arg == "--help") {
      Usage(argv[0]);
      std::exit(0);
    } else if (arg == "--list") {
      listOnly = true;
    } else if (arg == "--csv") {
      options.csv = true;
    } else if (arg == "--keep-files") {
      options.keepFiles = true;
    } else if (arg == "--events" && i + 1 < argc) {
      options.events = ParseUnsigned(argv[++i], "--events");
    } else if (arg == "--repetitions" && i + 1 < argc) {
      options.repetitions = static_cast<unsigned>(ParseUnsigned(argv[++i], "--repetitions"));
    } else if (arg == "--seek-count" && i + 1 < argc) {
      options.seekCount = static_cast<unsigned>(ParseUnsigned(argv[++i], "--seek-count"));
    } else if (arg == "--output-dir" && i + 1 < argc) {
      options.outputDir = argv[++i];
    } else if (arg == "--scenario" && i + 1 < argc) {
      options.scenario = argv[++i];
    } else if (arg == "--fixture" && i + 1 < argc) {
      options.fixturePath = argv[++i];
    } else {
      std::cerr << "Unknown or incomplete option: " << arg << std::endl;
      Usage(argv[0]);
      std::exit(2);
    }
  }
  return options;
}

} // namespace

int main(int argc, char** argv) {
  bool listOnly = false;
  Options options = ParseOptions(argc, argv, listOnly);
  std::vector<Scenario> scenarios = xcdf_perf::AllScenarios();

  if (listOnly) {
    for (std::vector<Scenario>::const_iterator it = scenarios.begin();
         it != scenarios.end(); ++it) {
      std::cout << std::left << std::setw(24) << it->name
                << it->description << '\n';
      for (std::vector<Operation>::const_iterator op = it->operations.begin();
           op != it->operations.end(); ++op) {
        std::cout << "  - " << op->name << '\n';
      }
    }
    return 0;
  }

  xcdf_perf::EnsureDirectory(options.outputDir);

  PrintMetadata(options);

  if (options.csv) {
    std::cout << "scenario,operation,events,file_bytes,seconds,events_per_second,mib_per_second,bytes_per_event,checksum\n";
  } else {
    std::cout << std::left << std::setw(24) << "scenario"
              << std::setw(18) << "operation"
              << std::right << std::setw(12) << "events"
              << std::setw(14) << "file_bytes"
              << std::setw(12) << "seconds"
              << std::setw(16) << "events/s"
              << std::setw(12) << "MiB/s"
              << std::setw(12) << "B/event"
              << "  checksum\n";
  }

  bool ranScenario = false;
  for (std::vector<Scenario>::const_iterator it = scenarios.begin();
       it != scenarios.end(); ++it) {
    if (options.scenario != "all" && options.scenario != it->name) {
      continue;
    }
    ranScenario = true;

    if (it->usesFixture && options.fixturePath.empty()) {
      if (!options.csv) {
        std::cerr << "Skipping fixture scenario " << it->name
                  << ": pass --fixture FILE to run it." << std::endl;
      }
      continue;
    }

    for (unsigned rep = 0; rep < options.repetitions; ++rep) {
      const std::string path = it->usesFixture ?
          options.fixturePath : FileName(options, it->name, rep);
      uint64_t fileBytes = 0;

      if (!it->usesFixture) {
        auto start = std::chrono::steady_clock::now();
        uint64_t checksum = it->write(path, options.events);
        auto stop = std::chrono::steady_clock::now();
        fileBytes = xcdf_perf::FileSize(path);
        Result writeResult = {it->name, "write", options.events, fileBytes,
            std::chrono::duration<double>(stop - start).count(), checksum};
        PrintResult(writeResult, options.csv);
      } else {
        fileBytes = xcdf_perf::FileSize(path);
      }

      for (std::vector<Operation>::const_iterator op = it->operations.begin();
           op != it->operations.end(); ++op) {
        uint64_t measuredEvents = op->GetMeasuredEvents(options.events,
                                                        options.seekCount);
        auto start = std::chrono::steady_clock::now();
        uint64_t checksum = op->run(path, options.events, options.seekCount);
        auto stop = std::chrono::steady_clock::now();
        Result result = {it->name, op->name, measuredEvents, fileBytes,
            std::chrono::duration<double>(stop - start).count(), checksum};
        PrintResult(result, options.csv);
      }

      if (!it->usesFixture && !options.keepFiles) {
        std::remove(path.c_str());
      }
    }
  }

  if (!ranScenario) {
    std::cerr << "Unknown scenario: " << options.scenario << std::endl;
    std::cerr << "Use --list to show available scenarios." << std::endl;
    return 2;
  }

  return 0;
}
