/*
Copyright (c) 2026, XCDF contributors
All rights reserved.
*/

#include "PerfTest.h"

#include <xcdf/XCDF.h>

#include <sstream>
#include <string>
#include <vector>

namespace xcdf_perf {
namespace {

uint64_t ReadLimit(const std::string& path, uint64_t events, unsigned unused) {
  (void)unused;
  XCDFFile f(path.c_str(), "r");
  uint64_t checksum = 0;
  for (uint64_t i = 0; i < events; ++i) {
    if (!f.Read()) {
      XCDFFatal("Unexpected EOF in generic read");
    }
    checksum = Mix(checksum, f.GetCurrentEventNumber());
  }
  f.Close();
  return checksum;
}

uint64_t WriteSpeedMix(const std::string& path, uint64_t events) {
  XCDFFile f(path.c_str(), "w");
  XCDFUnsignedIntegerField field1 = f.AllocateUnsignedIntegerField("field1", 1);
  XCDFUnsignedIntegerField field2 = f.AllocateUnsignedIntegerField("field2", 4);
  XCDFSignedIntegerField field3 = f.AllocateSignedIntegerField("field3", 2);
  XCDFFloatingPointField field4 = f.AllocateFloatingPointField("field4", 0.1);
  XCDFFloatingPointField field5 = f.AllocateFloatingPointField("field5", 0.1, "field1");
  XCDFUnsignedIntegerField field6 = f.AllocateUnsignedIntegerField("field6", 1);

  uint64_t checksum = 0;
  for (uint64_t k = 0; k < events; ++k) {
    field1 << 4;
    field2 << k;
    field3 << -1;
    field4 << 101.3;
    field5 << 0.2 << 3.9 << 222.3 << 10840.4;
    field6 << k;
    checksum = Mix(checksum, k);
    f.Write();
  }
  f.Close();
  return checksum;
}

uint64_t ReadSpeedMix(const std::string& path, uint64_t events, unsigned unused) {
  (void)unused;
  XCDFFile f(path.c_str(), "r");
  XCDFUnsignedIntegerField field1 = f.GetUnsignedIntegerField("field1");
  XCDFUnsignedIntegerField field2 = f.GetUnsignedIntegerField("field2");
  XCDFSignedIntegerField field3 = f.GetSignedIntegerField("field3");
  XCDFFloatingPointField field4 = f.GetFloatingPointField("field4");
  XCDFFloatingPointField field5 = f.GetFloatingPointField("field5");
  XCDFUnsignedIntegerField field6 = f.GetUnsignedIntegerField("field6");

  uint64_t checksum = 0;
  for (uint64_t i = 0; i < events; ++i) {
    if (!f.Read()) {
      XCDFFatal("Unexpected EOF in speed_mix");
    }
    checksum = Mix(checksum, *field1);
    checksum = Mix(checksum, *field2);
    checksum = Mix(checksum, static_cast<uint64_t>(*field3));
    checksum = Mix(checksum, DoubleBits(*field4));
    for (unsigned j = 0; j < field5.GetSize(); ++j) {
      checksum = Mix(checksum, DoubleBits(field5[j]));
    }
    checksum = Mix(checksum, *field6);
  }
  f.Close();
  return checksum;
}

uint64_t SeekSpeedMix(const std::string& path, uint64_t events, unsigned count) {
  XCDFFile f(path.c_str(), "r");
  f.GetEventCount();
  XCDFUnsignedIntegerField field2 = f.GetUnsignedIntegerField("field2");
  XCDFUnsignedIntegerField field6 = f.GetUnsignedIntegerField("field6");
  std::vector<uint64_t> positions = SeekPositions(events, static_cast<unsigned>(SeekOperationCount(events, count)));
  uint64_t checksum = 0;
  for (std::vector<uint64_t>::const_iterator it = positions.begin(); it != positions.end(); ++it) {
    if (!f.Seek(*it)) {
      XCDFFatal("Seek failed in speed_mix");
    }
    checksum = Mix(checksum, *field2);
    checksum = Mix(checksum, *field6);
  }
  f.Close();
  return checksum;
}

uint64_t WriteScalarInts(const std::string& path, uint64_t events) {
  XCDFFile f(path.c_str(), "w");
  XCDFUnsignedIntegerField eventId = f.AllocateUnsignedIntegerField("event_id", 1);
  XCDFUnsignedIntegerField run = f.AllocateUnsignedIntegerField("run", 1);
  XCDFUnsignedIntegerField tank = f.AllocateUnsignedIntegerField("tank", 1);
  XCDFSignedIntegerField dt = f.AllocateSignedIntegerField("dt", 1);
  XCDFUnsignedIntegerField charge = f.AllocateUnsignedIntegerField("charge", 1);
  XCDFUnsignedIntegerField flags = f.AllocateUnsignedIntegerField("flags", 1);

  uint64_t checksum = 0;
  for (uint64_t i = 0; i < events; ++i) {
    eventId << i;
    run << (1000 + (i / 100000));
    tank << ((i * 37) % 1200);
    dt << (static_cast<int64_t>((i * 17) % 20001) - 10000);
    charge << ((i * 13) % 4096);
    flags << (1ULL << (i % 12));
    checksum = Mix(checksum, i);
    f.Write();
  }
  f.Close();
  return checksum;
}

uint64_t ReadScalarInts(const std::string& path, uint64_t events, unsigned unused) {
  (void)unused;
  XCDFFile f(path.c_str(), "r");
  XCDFUnsignedIntegerField eventId = f.GetUnsignedIntegerField("event_id");
  XCDFUnsignedIntegerField run = f.GetUnsignedIntegerField("run");
  XCDFUnsignedIntegerField tank = f.GetUnsignedIntegerField("tank");
  XCDFSignedIntegerField dt = f.GetSignedIntegerField("dt");
  XCDFUnsignedIntegerField charge = f.GetUnsignedIntegerField("charge");
  XCDFUnsignedIntegerField flags = f.GetUnsignedIntegerField("flags");

  uint64_t checksum = 0;
  for (uint64_t i = 0; i < events; ++i) {
    if (!f.Read()) {
      XCDFFatal("Unexpected EOF in scalar_ints");
    }
    checksum = Mix(checksum, *eventId);
    checksum = Mix(checksum, *run);
    checksum = Mix(checksum, *tank);
    checksum = Mix(checksum, static_cast<uint64_t>(*dt));
    checksum = Mix(checksum, *charge);
    checksum = Mix(checksum, *flags);
  }
  f.Close();
  return checksum;
}

uint64_t SeekScalarInts(const std::string& path, uint64_t events, unsigned count) {
  XCDFFile f(path.c_str(), "r");
  f.GetEventCount();
  XCDFUnsignedIntegerField eventId = f.GetUnsignedIntegerField("event_id");
  XCDFUnsignedIntegerField charge = f.GetUnsignedIntegerField("charge");
  std::vector<uint64_t> positions = SeekPositions(events, static_cast<unsigned>(SeekOperationCount(events, count)));
  uint64_t checksum = 0;
  for (std::vector<uint64_t>::const_iterator it = positions.begin(); it != positions.end(); ++it) {
    if (!f.Seek(*it)) {
      XCDFFatal("Seek failed in scalar_ints");
    }
    checksum = Mix(checksum, *eventId);
    checksum = Mix(checksum, *charge);
  }
  f.Close();
  return checksum;
}

uint64_t WriteQuantizedDoubles(const std::string& path, uint64_t events) {
  XCDFFile f(path.c_str(), "w");
  XCDFFloatingPointField mjd = f.AllocateFloatingPointField("mjd", 1e-6);
  XCDFFloatingPointField zenith = f.AllocateFloatingPointField("zenith", 1e-4);
  XCDFFloatingPointField azimuth = f.AllocateFloatingPointField("azimuth", 1e-4);
  XCDFFloatingPointField coreX = f.AllocateFloatingPointField("core_x", 0.01);
  XCDFFloatingPointField coreY = f.AllocateFloatingPointField("core_y", 0.01);
  XCDFFloatingPointField energy = f.AllocateFloatingPointField("energy", 0.001);

  uint64_t checksum = 0;
  for (uint64_t i = 0; i < events; ++i) {
    mjd << (60000.0 + static_cast<double>(i % 86400000) * 1e-6);
    zenith << (static_cast<double>((i * 17) % 1800000) * 1e-4);
    azimuth << (static_cast<double>((i * 29) % 3600000) * 1e-4);
    coreX << (static_cast<double>(static_cast<int64_t>((i * 41) % 200000) - 100000) * 0.01);
    coreY << (static_cast<double>(static_cast<int64_t>((i * 53) % 200000) - 100000) * 0.01);
    energy << (1.0 + static_cast<double>((i * 7) % 100000) * 0.001);
    checksum = Mix(checksum, i);
    f.Write();
  }
  f.Close();
  return checksum;
}

uint64_t ReadQuantizedDoubles(const std::string& path, uint64_t events, unsigned unused) {
  (void)unused;
  XCDFFile f(path.c_str(), "r");
  XCDFFloatingPointField mjd = f.GetFloatingPointField("mjd");
  XCDFFloatingPointField zenith = f.GetFloatingPointField("zenith");
  XCDFFloatingPointField azimuth = f.GetFloatingPointField("azimuth");
  XCDFFloatingPointField coreX = f.GetFloatingPointField("core_x");
  XCDFFloatingPointField coreY = f.GetFloatingPointField("core_y");
  XCDFFloatingPointField energy = f.GetFloatingPointField("energy");

  uint64_t checksum = 0;
  for (uint64_t i = 0; i < events; ++i) {
    if (!f.Read()) {
      XCDFFatal("Unexpected EOF in quantized_doubles");
    }
    checksum = Mix(checksum, DoubleBits(*mjd));
    checksum = Mix(checksum, DoubleBits(*zenith));
    checksum = Mix(checksum, DoubleBits(*azimuth));
    checksum = Mix(checksum, DoubleBits(*coreX));
    checksum = Mix(checksum, DoubleBits(*coreY));
    checksum = Mix(checksum, DoubleBits(*energy));
  }
  f.Close();
  return checksum;
}

uint64_t SeekQuantizedDoubles(const std::string& path, uint64_t events, unsigned count) {
  XCDFFile f(path.c_str(), "r");
  f.GetEventCount();
  XCDFFloatingPointField energy = f.GetFloatingPointField("energy");
  XCDFFloatingPointField zenith = f.GetFloatingPointField("zenith");
  std::vector<uint64_t> positions = SeekPositions(events, static_cast<unsigned>(SeekOperationCount(events, count)));
  uint64_t checksum = 0;
  for (std::vector<uint64_t>::const_iterator it = positions.begin(); it != positions.end(); ++it) {
    if (!f.Seek(*it)) {
      XCDFFatal("Seek failed in quantized_doubles");
    }
    checksum = Mix(checksum, DoubleBits(*energy));
    checksum = Mix(checksum, DoubleBits(*zenith));
  }
  f.Close();
  return checksum;
}

uint64_t WriteVectorVariable(const std::string& path, uint64_t events) {
  XCDFFile f(path.c_str(), "w");
  XCDFUnsignedIntegerField nHit = f.AllocateUnsignedIntegerField("n_hit", 1);
  XCDFUnsignedIntegerField channel = f.AllocateUnsignedIntegerField("channel", 1, "n_hit");
  XCDFFloatingPointField time = f.AllocateFloatingPointField("time", 0.1, "n_hit");
  XCDFFloatingPointField charge = f.AllocateFloatingPointField("charge", 0.01, "n_hit");

  uint64_t checksum = 0;
  for (uint64_t i = 0; i < events; ++i) {
    unsigned n = static_cast<unsigned>((i * 7) % 25);
    nHit << n;
    for (unsigned j = 0; j < n; ++j) {
      channel << ((i + j * 13) % 1800);
      time << (static_cast<double>(static_cast<int>(j) * 25 - 300) + static_cast<double>(i % 10) * 0.1);
      charge << (0.01 * static_cast<double>((i * 11 + j * 17) % 50000));
    }
    checksum = Mix(checksum, n);
    f.Write();
  }
  f.Close();
  return checksum;
}

uint64_t ReadVectorVariable(const std::string& path, uint64_t events, unsigned unused) {
  (void)unused;
  XCDFFile f(path.c_str(), "r");
  XCDFUnsignedIntegerField nHit = f.GetUnsignedIntegerField("n_hit");
  XCDFUnsignedIntegerField channel = f.GetUnsignedIntegerField("channel");
  XCDFFloatingPointField time = f.GetFloatingPointField("time");
  XCDFFloatingPointField charge = f.GetFloatingPointField("charge");

  uint64_t checksum = 0;
  for (uint64_t i = 0; i < events; ++i) {
    if (!f.Read()) {
      XCDFFatal("Unexpected EOF in vector_variable");
    }
    checksum = Mix(checksum, *nHit);
    for (unsigned j = 0; j < channel.GetSize(); ++j) {
      checksum = Mix(checksum, channel[j]);
      checksum = Mix(checksum, DoubleBits(time[j]));
      checksum = Mix(checksum, DoubleBits(charge[j]));
    }
  }
  f.Close();
  return checksum;
}

uint64_t SeekVectorVariable(const std::string& path, uint64_t events, unsigned count) {
  XCDFFile f(path.c_str(), "r");
  f.GetEventCount();
  XCDFUnsignedIntegerField nHit = f.GetUnsignedIntegerField("n_hit");
  XCDFUnsignedIntegerField channel = f.GetUnsignedIntegerField("channel");
  std::vector<uint64_t> positions = SeekPositions(events, static_cast<unsigned>(SeekOperationCount(events, count)));
  uint64_t checksum = 0;
  for (std::vector<uint64_t>::const_iterator it = positions.begin(); it != positions.end(); ++it) {
    if (!f.Seek(*it)) {
      XCDFFatal("Seek failed in vector_variable");
    }
    checksum = Mix(checksum, *nHit);
    for (unsigned j = 0; j < channel.GetSize(); ++j) {
      checksum = Mix(checksum, channel[j]);
    }
  }
  f.Close();
  return checksum;
}

uint64_t WriteManyFields(const std::string& path, uint64_t events) {
  XCDFFile f(path.c_str(), "w");
  std::vector<XCDFUnsignedIntegerField> integers;
  std::vector<XCDFFloatingPointField> doubles;
  for (unsigned i = 0; i < 32; ++i) {
    std::ostringstream name;
    name << "u" << i;
    integers.push_back(f.AllocateUnsignedIntegerField(name.str(), 1));
  }
  for (unsigned i = 0; i < 16; ++i) {
    std::ostringstream name;
    name << "d" << i;
    doubles.push_back(f.AllocateFloatingPointField(name.str(), 0.01));
  }

  uint64_t checksum = 0;
  for (uint64_t event = 0; event < events; ++event) {
    for (unsigned i = 0; i < integers.size(); ++i) {
      integers[i] << ((event * (i + 3)) % 1000000);
    }
    for (unsigned i = 0; i < doubles.size(); ++i) {
      doubles[i] << (static_cast<double>((event * (i + 5)) % 1000000) * 0.01);
    }
    checksum = Mix(checksum, event);
    f.Write();
  }
  f.Close();
  return checksum;
}

uint64_t ReadManyFields(const std::string& path, uint64_t events, unsigned unused) {
  (void)unused;
  XCDFFile f(path.c_str(), "r");
  std::vector<XCDFUnsignedIntegerField> integers;
  std::vector<XCDFFloatingPointField> doubles;
  for (unsigned i = 0; i < 32; ++i) {
    std::ostringstream name;
    name << "u" << i;
    integers.push_back(f.GetUnsignedIntegerField(name.str()));
  }
  for (unsigned i = 0; i < 16; ++i) {
    std::ostringstream name;
    name << "d" << i;
    doubles.push_back(f.GetFloatingPointField(name.str()));
  }

  uint64_t checksum = 0;
  for (uint64_t event = 0; event < events; ++event) {
    if (!f.Read()) {
      XCDFFatal("Unexpected EOF in many_fields");
    }
    for (unsigned i = 0; i < integers.size(); ++i) {
      checksum = Mix(checksum, *integers[i]);
    }
    for (unsigned i = 0; i < doubles.size(); ++i) {
      checksum = Mix(checksum, DoubleBits(*doubles[i]));
    }
  }
  f.Close();
  return checksum;
}

uint64_t SeekManyFields(const std::string& path, uint64_t events, unsigned count) {
  XCDFFile f(path.c_str(), "r");
  f.GetEventCount();
  XCDFUnsignedIntegerField u0 = f.GetUnsignedIntegerField("u0");
  XCDFFloatingPointField d0 = f.GetFloatingPointField("d0");
  std::vector<uint64_t> positions = SeekPositions(events, static_cast<unsigned>(SeekOperationCount(events, count)));
  uint64_t checksum = 0;
  for (std::vector<uint64_t>::const_iterator it = positions.begin(); it != positions.end(); ++it) {
    if (!f.Seek(*it)) {
      XCDFFatal("Seek failed in many_fields");
    }
    checksum = Mix(checksum, *u0);
    checksum = Mix(checksum, DoubleBits(*d0));
  }
  f.Close();
  return checksum;
}

uint64_t WriteFullPrecision(const std::string& path, uint64_t events) {
  XCDFFile f(path.c_str(), "w");
  XCDFFloatingPointField x = f.AllocateFloatingPointField("x", 0.0);
  XCDFFloatingPointField y = f.AllocateFloatingPointField("y", 0.0);
  XCDFFloatingPointField z = f.AllocateFloatingPointField("z", 0.0);
  XCDFFloatingPointField t = f.AllocateFloatingPointField("t", 0.0);

  uint64_t checksum = 0;
  for (uint64_t i = 0; i < events; ++i) {
    x << (1.0 / static_cast<double>(i + 1));
    y << (static_cast<double>(i) * 1.0000000000001);
    z << (static_cast<double>((i * 7919) % 104729) / 104729.0);
    t << (60000.0 + static_cast<double>(i) / 86400000.0);
    checksum = Mix(checksum, i);
    f.Write();
  }
  f.Close();
  return checksum;
}

uint64_t ReadFullPrecision(const std::string& path, uint64_t events, unsigned unused) {
  (void)unused;
  XCDFFile f(path.c_str(), "r");
  XCDFFloatingPointField x = f.GetFloatingPointField("x");
  XCDFFloatingPointField y = f.GetFloatingPointField("y");
  XCDFFloatingPointField z = f.GetFloatingPointField("z");
  XCDFFloatingPointField t = f.GetFloatingPointField("t");

  uint64_t checksum = 0;
  for (uint64_t i = 0; i < events; ++i) {
    if (!f.Read()) {
      XCDFFatal("Unexpected EOF in full_precision");
    }
    checksum = Mix(checksum, DoubleBits(*x));
    checksum = Mix(checksum, DoubleBits(*y));
    checksum = Mix(checksum, DoubleBits(*z));
    checksum = Mix(checksum, DoubleBits(*t));
  }
  f.Close();
  return checksum;
}

uint64_t SeekFullPrecision(const std::string& path, uint64_t events, unsigned count) {
  XCDFFile f(path.c_str(), "r");
  f.GetEventCount();
  XCDFFloatingPointField x = f.GetFloatingPointField("x");
  XCDFFloatingPointField t = f.GetFloatingPointField("t");
  std::vector<uint64_t> positions = SeekPositions(events, static_cast<unsigned>(SeekOperationCount(events, count)));
  uint64_t checksum = 0;
  for (std::vector<uint64_t>::const_iterator it = positions.begin(); it != positions.end(); ++it) {
    if (!f.Seek(*it)) {
      XCDFFatal("Seek failed in full_precision");
    }
    checksum = Mix(checksum, DoubleBits(*x));
    checksum = Mix(checksum, DoubleBits(*t));
  }
  f.Close();
  return checksum;
}

} // namespace

std::vector<Scenario> CoreScenarios() {
  std::vector<Scenario> scenarios;
  scenarios.push_back(Scenario{"speed_mix",
      "Original SpeedTest-style scalar + fixed vector workload",
      WriteSpeedMix,
      std::vector<Operation>{
          Operation("read", ReadSpeedMix),
          Operation("seek", SeekSpeedMix, SeekOperationCount)}});
  scenarios.push_back(Scenario{"scalar_ints",
      "Integer-heavy event metadata and low-level detector quantities",
      WriteScalarInts,
      std::vector<Operation>{
          Operation("read", ReadScalarInts),
          Operation("seek", SeekScalarInts, SeekOperationCount)}});
  scenarios.push_back(Scenario{"quantized_doubles",
      "Several lossy floating-point reconstruction quantities",
      WriteQuantizedDoubles,
      std::vector<Operation>{
          Operation("read", ReadQuantizedDoubles),
          Operation("seek", SeekQuantizedDoubles, SeekOperationCount)}});
  scenarios.push_back(Scenario{"vector_variable",
      "Variable-length hit arrays with integer and floating-point children",
      WriteVectorVariable,
      std::vector<Operation>{
          Operation("read", ReadVectorVariable),
          Operation("seek", SeekVectorVariable, SeekOperationCount)}});
  scenarios.push_back(Scenario{"many_fields",
      "Large field count stress test for per-field overhead",
      WriteManyFields,
      std::vector<Operation>{
          Operation("read", ReadManyFields),
          Operation("seek", SeekManyFields, SeekOperationCount)}});
  scenarios.push_back(Scenario{"full_precision",
      "Unquantized 64-bit floating-point data",
      WriteFullPrecision,
      std::vector<Operation>{
          Operation("read", ReadFullPrecision),
          Operation("seek", SeekFullPrecision, SeekOperationCount)}});
  (void)ReadLimit;
  return scenarios;
}

} // namespace xcdf_perf
