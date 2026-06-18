/*
Copyright (c) 2026, XCDF contributors
All rights reserved.
*/

#include "PerfTest.h"

#include <xcdf/XCDF.h>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

namespace xcdf_perf {
namespace {

uint64_t CountOne(uint64_t events, unsigned seekCount) {
  (void)events;
  (void)seekCount;
  return 1;
}

uint64_t CountEvents(uint64_t events, unsigned seekCount) {
  (void)seekCount;
  return events;
}

void AddBasicFields(XCDFFile& f,
                    XCDFUnsignedIntegerField& eventId,
                    XCDFUnsignedIntegerField& multiplicity,
                    XCDFUnsignedIntegerField& channel,
                    XCDFFloatingPointField& charge,
                    XCDFFloatingPointField& zenith) {
  eventId = f.AllocateUnsignedIntegerField("event_id", 1);
  multiplicity = f.AllocateUnsignedIntegerField("multiplicity", 1);
  channel = f.AllocateUnsignedIntegerField("channel", 1, "multiplicity");
  charge = f.AllocateFloatingPointField("charge", 0.01, "multiplicity");
  zenith = f.AllocateFloatingPointField("zenith", 1e-4);
}

uint64_t FillBasicFile(const std::string& path,
                       uint64_t events,
                       bool blockTable,
                       uint64_t blockSize,
                       unsigned comments) {
  XCDFFile f(path.c_str(), "w");
  if (!blockTable) {
    f.DisableBlockTable();
  }
  if (blockSize > 0) {
    f.SetBlockSize(blockSize);
  }

  XCDFUnsignedIntegerField eventId;
  XCDFUnsignedIntegerField multiplicity;
  XCDFUnsignedIntegerField channel;
  XCDFFloatingPointField charge;
  XCDFFloatingPointField zenith;
  AddBasicFields(f, eventId, multiplicity, channel, charge, zenith);

  uint64_t checksum = 0;
  for (uint64_t i = 0; i < events; ++i) {
    unsigned n = static_cast<unsigned>((i * 5) % 16);
    eventId << i;
    multiplicity << n;
    for (unsigned j = 0; j < n; ++j) {
      channel << ((i + j * 17) % 1800);
      charge << (0.01 * static_cast<double>((i * 23 + j * 31) % 100000));
    }
    zenith << (static_cast<double>((i * 19) % 1800000) * 1e-4);
    checksum = Mix(checksum, i);
    f.Write();
  }

  for (unsigned i = 0; i < comments; ++i) {
    std::ostringstream os;
    os << "performance metadata comment " << i
       << " -- intentionally long enough to exercise trailer string parsing";
    f.AddComment(os.str());
  }

  f.Close();
  return checksum;
}

uint64_t ReadBasic(const std::string& path, uint64_t events, unsigned unused) {
  (void)unused;
  XCDFFile f(path.c_str(), "r");
  XCDFUnsignedIntegerField eventId = f.GetUnsignedIntegerField("event_id");
  XCDFUnsignedIntegerField multiplicity = f.GetUnsignedIntegerField("multiplicity");
  XCDFUnsignedIntegerField channel = f.GetUnsignedIntegerField("channel");
  XCDFFloatingPointField charge = f.GetFloatingPointField("charge");
  XCDFFloatingPointField zenith = f.GetFloatingPointField("zenith");

  uint64_t checksum = 0;
  for (uint64_t i = 0; i < events; ++i) {
    if (!f.Read()) {
      XCDFFatal("Unexpected EOF in basic feature read");
    }
    checksum = Mix(checksum, *eventId);
    checksum = Mix(checksum, *multiplicity);
    checksum = Mix(checksum, DoubleBits(*zenith));
    for (unsigned j = 0; j < channel.GetSize(); ++j) {
      checksum = Mix(checksum, channel[j]);
      checksum = Mix(checksum, DoubleBits(charge[j]));
    }
  }
  f.Close();
  return checksum;
}

uint64_t SeekBasic(const std::string& path, uint64_t events, unsigned count) {
  XCDFFile f(path.c_str(), "r");
  f.GetEventCount();
  XCDFUnsignedIntegerField eventId = f.GetUnsignedIntegerField("event_id");
  XCDFFloatingPointField zenith = f.GetFloatingPointField("zenith");
  std::vector<uint64_t> positions = SeekPositions(events, static_cast<unsigned>(SeekOperationCount(events, count)));

  uint64_t checksum = 0;
  for (std::vector<uint64_t>::const_iterator it = positions.begin(); it != positions.end(); ++it) {
    if (!f.Seek(*it)) {
      XCDFFatal("Seek failed in basic feature scenario");
    }
    checksum = Mix(checksum, *eventId);
    checksum = Mix(checksum, DoubleBits(*zenith));
  }
  f.Close();
  return checksum;
}

uint64_t EventCountOnly(const std::string& path, uint64_t events, unsigned unused) {
  (void)events;
  (void)unused;
  XCDFFile f(path.c_str(), "r");
  uint64_t count = f.GetEventCount();
  f.Close();
  return Mix(0, count);
}

uint64_t GlobalRanges(const std::string& path, uint64_t events, unsigned unused) {
  (void)events;
  (void)unused;
  XCDFFile f(path.c_str(), "r");
  std::pair<uint64_t, uint64_t> eventRange = f.GetUnsignedIntegerFieldRange("event_id");
  std::pair<uint64_t, uint64_t> channelRange = f.GetUnsignedIntegerFieldRange("channel");
  std::pair<double, double> chargeRange = f.GetFloatingPointFieldRange("charge");
  std::pair<double, double> zenithRange = f.GetFloatingPointFieldRange("zenith");
  uint64_t checksum = 0;
  checksum = Mix(checksum, eventRange.first);
  checksum = Mix(checksum, eventRange.second);
  checksum = Mix(checksum, channelRange.first);
  checksum = Mix(checksum, channelRange.second);
  checksum = Mix(checksum, DoubleBits(chargeRange.first));
  checksum = Mix(checksum, DoubleBits(chargeRange.second));
  checksum = Mix(checksum, DoubleBits(zenithRange.first));
  checksum = Mix(checksum, DoubleBits(zenithRange.second));
  f.Close();
  return checksum;
}

uint64_t CommentsMetadata(const std::string& path, uint64_t events, unsigned unused) {
  (void)events;
  (void)unused;
  XCDFFile f(path.c_str(), "r");
  f.LoadComments();
  uint64_t checksum = Mix(0, f.GetNComments(true));
  for (std::vector<std::string>::const_iterator it = f.CommentsBegin(); it != f.CommentsEnd(); ++it) {
    checksum = Mix(checksum, it->size());
  }
  f.Close();
  return checksum;
}

uint64_t WriteBlockTableSmallBlocks(const std::string& path, uint64_t events) {
  return FillBasicFile(path, events, true, 10, 0);
}

uint64_t WriteBlockTableLargeBlocks(const std::string& path, uint64_t events) {
  return FillBasicFile(path, events, true, 10000, 0);
}

uint64_t WriteNoBlockTable(const std::string& path, uint64_t events) {
  return FillBasicFile(path, events, false, 1000, 0);
}

uint64_t WriteManyComments(const std::string& path, uint64_t events) {
  return FillBasicFile(path, events, true, 1000, 200);
}

uint64_t WriteAliases(const std::string& path, uint64_t events) {
  XCDFFile f(path.c_str(), "w");
  XCDFUnsignedIntegerField eventId = f.AllocateUnsignedIntegerField("event_id", 1);
  XCDFUnsignedIntegerField multiplicity = f.AllocateUnsignedIntegerField("multiplicity", 1);
  XCDFFloatingPointField zenith = f.AllocateFloatingPointField("zenith", 1e-4);
  XCDFFloatingPointField energy = f.AllocateFloatingPointField("energy", 0.001);
  f.CreateAlias("event_plus_mult", "event_id + multiplicity");
  f.CreateAlias("weighted_energy", "sqrt(energy) + double(event_plus_mult % 17)");

  uint64_t checksum = 0;
  for (uint64_t i = 0; i < events; ++i) {
    unsigned n = static_cast<unsigned>((i * 5) % 16);
    eventId << i;
    multiplicity << n;
    zenith << (static_cast<double>((i * 19) % 1800000) * 1e-4);
    energy << (1.0 + static_cast<double>((i * 7) % 100000) * 0.001);
    checksum = Mix(checksum, i);
    f.Write();
  }

  f.CreateAlias("late_alias", "weighted_energy + sin(zenith)");
  f.Close();
  return checksum;
}

uint64_t ReadAliasFields(const std::string& path, uint64_t events, unsigned unused) {
  (void)unused;
  XCDFFile f(path.c_str(), "r");
  XCDFUnsignedIntegerField eventId = f.GetUnsignedIntegerField("event_id");
  XCDFUnsignedIntegerField multiplicity = f.GetUnsignedIntegerField("multiplicity");
  XCDFFloatingPointField zenith = f.GetFloatingPointField("zenith");
  XCDFFloatingPointField energy = f.GetFloatingPointField("energy");

  uint64_t checksum = 0;
  for (uint64_t i = 0; i < events; ++i) {
    if (!f.Read()) {
      XCDFFatal("Unexpected EOF in alias field read");
    }
    checksum = Mix(checksum, *eventId);
    checksum = Mix(checksum, *multiplicity);
    checksum = Mix(checksum, DoubleBits(*zenith));
    checksum = Mix(checksum, DoubleBits(*energy));
  }
  f.Close();
  return checksum;
}

uint64_t ReadAliasEvaluate(const std::string& path, uint64_t events, unsigned unused) {
  (void)unused;
  XCDFFile f(path.c_str(), "r");
  ConstXCDFUnsignedIntegerFieldAlias eventPlusMult = f.GetUnsignedIntegerAlias("event_plus_mult");
  ConstXCDFFloatingPointFieldAlias weightedEnergy = f.GetFloatingPointAlias("weighted_energy");
  ConstXCDFFloatingPointFieldAlias lateAlias = f.GetFloatingPointAlias("late_alias");

  uint64_t checksum = 0;
  for (uint64_t i = 0; i < events; ++i) {
    if (!f.Read()) {
      XCDFFatal("Unexpected EOF in alias evaluation read");
    }
    checksum = Mix(checksum, *eventPlusMult);
    checksum = Mix(checksum, DoubleBits(*weightedEnergy));
    checksum = Mix(checksum, DoubleBits(*lateAlias));
  }
  f.Close();
  return checksum;
}

uint64_t WriteNestedVectors(const std::string& path, uint64_t events) {
  XCDFFile f(path.c_str(), "w");
  XCDFUnsignedIntegerField nStations = f.AllocateUnsignedIntegerField("n_stations", 1);
  XCDFUnsignedIntegerField nPmt = f.AllocateUnsignedIntegerField("n_pmt", 1, "n_stations");
  XCDFFloatingPointField charge = f.AllocateFloatingPointField("charge", 0.01, "n_pmt");
  f.CreateAlias("total_charge", "sum(charge)");

  uint64_t checksum = 0;
  for (uint64_t i = 0; i < events; ++i) {
    unsigned stations = static_cast<unsigned>((i % 6) + 1);
    nStations << stations;
    unsigned totalPmt = 0;
    for (unsigned station = 0; station < stations; ++station) {
      unsigned pmt = static_cast<unsigned>(((i + station) % 4) + 1);
      nPmt << pmt;
      totalPmt += pmt;
    }
    for (unsigned pmt = 0; pmt < totalPmt; ++pmt) {
      charge << (0.01 * static_cast<double>((i * 13 + pmt * 17) % 100000));
    }
    checksum = Mix(checksum, totalPmt);
    f.Write();
  }
  f.Close();
  return checksum;
}

uint64_t ReadNestedVectors(const std::string& path, uint64_t events, unsigned unused) {
  (void)unused;
  XCDFFile f(path.c_str(), "r");
  XCDFUnsignedIntegerField nStations = f.GetUnsignedIntegerField("n_stations");
  XCDFUnsignedIntegerField nPmt = f.GetUnsignedIntegerField("n_pmt");
  XCDFFloatingPointField charge = f.GetFloatingPointField("charge");
  ConstXCDFFloatingPointFieldAlias totalCharge = f.GetFloatingPointAlias("total_charge");

  uint64_t checksum = 0;
  for (uint64_t i = 0; i < events; ++i) {
    if (!f.Read()) {
      XCDFFatal("Unexpected EOF in nested vector read");
    }
    checksum = Mix(checksum, *nStations);
    for (unsigned j = 0; j < nPmt.GetSize(); ++j) {
      checksum = Mix(checksum, nPmt[j]);
    }
    for (unsigned j = 0; j < charge.GetSize(); ++j) {
      checksum = Mix(checksum, DoubleBits(charge[j]));
    }
    checksum = Mix(checksum, DoubleBits(*totalCharge));
  }
  f.Close();
  return checksum;
}

uint64_t WriteOneConcatPart(const std::string& path, uint64_t events) {
  XCDFFile f(path.c_str(), "w");
  XCDFUnsignedIntegerField eventId = f.AllocateUnsignedIntegerField("event_id", 1);
  XCDFFloatingPointField value = f.AllocateFloatingPointField("value", 0.01);

  uint64_t checksum = 0;
  for (uint64_t i = 0; i < events; ++i) {
    eventId << i;
    value << (0.01 * static_cast<double>((i * 11) % 100000));
    checksum = Mix(checksum, i);
    f.Write();
  }
  f.AddComment("concat part");
  f.Close();
  return checksum;
}

void AppendFile(const std::string& inputPath, std::ofstream& out) {
  std::ifstream in(inputPath.c_str(), std::ios::binary);
  out << in.rdbuf();
}

uint64_t WriteConcatenated(const std::string& path, uint64_t events) {
  uint64_t first = events / 2;
  uint64_t second = events - first;
  std::string part1 = path + ".part1";
  std::string part2 = path + ".part2";
  uint64_t checksum = WriteOneConcatPart(part1, first);
  checksum = Mix(checksum, WriteOneConcatPart(part2, second));

  std::ofstream out(path.c_str(), std::ios::binary);
  AppendFile(part1, out);
  AppendFile(part2, out);
  out.close();
  std::remove(part1.c_str());
  std::remove(part2.c_str());
  return checksum;
}

uint64_t ReadConcatenated(const std::string& path, uint64_t events, unsigned unused) {
  (void)unused;
  XCDFFile f(path.c_str(), "r");
  XCDFUnsignedIntegerField eventId = f.GetUnsignedIntegerField("event_id");
  XCDFFloatingPointField value = f.GetFloatingPointField("value");

  uint64_t checksum = 0;
  for (uint64_t i = 0; i < events; ++i) {
    if (!f.Read()) {
      XCDFFatal("Unexpected EOF in concatenated read");
    }
    checksum = Mix(checksum, *eventId);
    checksum = Mix(checksum, DoubleBits(*value));
  }
  f.Close();
  return checksum;
}

uint64_t SeekConcatenated(const std::string& path, uint64_t events, unsigned count) {
  XCDFFile f(path.c_str(), "r");
  f.GetEventCount();
  XCDFUnsignedIntegerField eventId = f.GetUnsignedIntegerField("event_id");
  std::vector<uint64_t> positions = SeekPositions(events, static_cast<unsigned>(SeekOperationCount(events, count)));

  uint64_t checksum = 0;
  for (std::vector<uint64_t>::const_iterator it = positions.begin(); it != positions.end(); ++it) {
    if (!f.Seek(*it)) {
      XCDFFatal("Seek failed in concatenated scenario");
    }
    checksum = Mix(checksum, *eventId);
  }
  f.Close();
  return checksum;
}

uint64_t ReadInputStream(const std::string& path, uint64_t events, unsigned unused) {
  (void)unused;
  std::ifstream in(path.c_str(), std::ios::binary);
  XCDFFile f(in);
  XCDFUnsignedIntegerField eventId = f.GetUnsignedIntegerField("event_id");
  XCDFFloatingPointField zenith = f.GetFloatingPointField("zenith");

  uint64_t checksum = 0;
  for (uint64_t i = 0; i < events; ++i) {
    if (!f.Read()) {
      XCDFFatal("Unexpected EOF in istream read");
    }
    checksum = Mix(checksum, *eventId);
    checksum = Mix(checksum, DoubleBits(*zenith));
  }
  f.Close();
  return checksum;
}

uint64_t FixtureNoWrite(const std::string& path, uint64_t events) {
  (void)path;
  (void)events;
  return 0;
}

uint64_t HawcTrigCount(uint64_t events, unsigned seekCount) {
  (void)seekCount;
  return events;
}

uint64_t HawcTrigReadFull(const std::string& path, uint64_t events, unsigned unused) {
  (void)unused;
  XCDFFile f(path.c_str(), "r");
  uint64_t available = f.GetEventCount();
  uint64_t limit = std::min<uint64_t>(events, available);

  XCDFUnsignedIntegerField eventID = f.GetUnsignedIntegerField("trig.eventID");
  XCDFUnsignedIntegerField timeSliceID = f.GetUnsignedIntegerField("trig.timeSliceID");
  XCDFUnsignedIntegerField gpsSec = f.GetUnsignedIntegerField("trig.gpsSec");
  XCDFUnsignedIntegerField gpsNanosec = f.GetUnsignedIntegerField("trig.gpsNanosec");
  XCDFUnsignedIntegerField nHit4Edge = f.GetUnsignedIntegerField("trig.nHit4Edge");
  XCDFUnsignedIntegerField nHit2Edge = f.GetUnsignedIntegerField("trig.nHit2Edge");
  XCDFSignedIntegerField hitTime4Edge = f.GetSignedIntegerField("trig.hitTime_4Edge");
  XCDFUnsignedIntegerField loTOT4Edge = f.GetUnsignedIntegerField("trig.loTOT_4Edge");
  XCDFUnsignedIntegerField hiTOT4Edge = f.GetUnsignedIntegerField("trig.hiTOT_4Edge");
  XCDFUnsignedIntegerField channel4Edge = f.GetUnsignedIntegerField("trig.channelID_4Edge");
  XCDFSignedIntegerField hitTime2Edge = f.GetSignedIntegerField("trig.hitTime_2Edge");
  XCDFUnsignedIntegerField loTOT2Edge = f.GetUnsignedIntegerField("trig.loTOT_2Edge");
  XCDFUnsignedIntegerField channel2Edge = f.GetUnsignedIntegerField("trig.channelID_2Edge");

  uint64_t checksum = 0;
  for (uint64_t i = 0; i < limit; ++i) {
    if (!f.Read()) {
      XCDFFatal("Unexpected EOF in HAWC trigger fixture read");
    }
    checksum = Mix(checksum, *eventID);
    checksum = Mix(checksum, *timeSliceID);
    checksum = Mix(checksum, *gpsSec);
    checksum = Mix(checksum, *gpsNanosec);
    checksum = Mix(checksum, *nHit4Edge);
    checksum = Mix(checksum, *nHit2Edge);
    for (unsigned j = 0; j < hitTime4Edge.GetSize(); ++j) {
      checksum = Mix(checksum, static_cast<uint64_t>(hitTime4Edge[j]));
      checksum = Mix(checksum, loTOT4Edge[j]);
      checksum = Mix(checksum, hiTOT4Edge[j]);
      checksum = Mix(checksum, channel4Edge[j]);
    }
    for (unsigned j = 0; j < hitTime2Edge.GetSize(); ++j) {
      checksum = Mix(checksum, static_cast<uint64_t>(hitTime2Edge[j]));
      checksum = Mix(checksum, loTOT2Edge[j]);
      checksum = Mix(checksum, channel2Edge[j]);
    }
  }
  f.Close();
  return checksum;
}

uint64_t HawcTrigReadScalars(const std::string& path, uint64_t events, unsigned unused) {
  (void)unused;
  XCDFFile f(path.c_str(), "r");
  uint64_t available = f.GetEventCount();
  uint64_t limit = std::min<uint64_t>(events, available);

  XCDFUnsignedIntegerField eventID = f.GetUnsignedIntegerField("trig.eventID");
  XCDFUnsignedIntegerField timeSliceID = f.GetUnsignedIntegerField("trig.timeSliceID");
  XCDFUnsignedIntegerField gpsSec = f.GetUnsignedIntegerField("trig.gpsSec");
  XCDFUnsignedIntegerField gpsNanosec = f.GetUnsignedIntegerField("trig.gpsNanosec");
  XCDFUnsignedIntegerField nHit4Edge = f.GetUnsignedIntegerField("trig.nHit4Edge");
  XCDFUnsignedIntegerField nHit2Edge = f.GetUnsignedIntegerField("trig.nHit2Edge");

  uint64_t checksum = 0;
  for (uint64_t i = 0; i < limit; ++i) {
    if (!f.Read()) {
      XCDFFatal("Unexpected EOF in HAWC trigger scalar fixture read");
    }
    checksum = Mix(checksum, *eventID);
    checksum = Mix(checksum, *timeSliceID);
    checksum = Mix(checksum, *gpsSec);
    checksum = Mix(checksum, *gpsNanosec);
    checksum = Mix(checksum, *nHit4Edge);
    checksum = Mix(checksum, *nHit2Edge);
  }
  f.Close();
  return checksum;
}

uint64_t HawcTrigSeek(const std::string& path, uint64_t events, unsigned count) {
  XCDFFile f(path.c_str(), "r");
  uint64_t available = f.GetEventCount();
  uint64_t limit = std::min<uint64_t>(events, available);
  uint64_t nSeek = SeekOperationCount(limit, count);
  XCDFUnsignedIntegerField eventID = f.GetUnsignedIntegerField("trig.eventID");
  XCDFUnsignedIntegerField nHit4Edge = f.GetUnsignedIntegerField("trig.nHit4Edge");
  std::vector<uint64_t> positions = SeekPositions(limit, static_cast<unsigned>(nSeek));

  uint64_t checksum = 0;
  for (std::vector<uint64_t>::const_iterator it = positions.begin(); it != positions.end(); ++it) {
    if (!f.Seek(*it)) {
      XCDFFatal("Seek failed in HAWC trigger fixture");
    }
    checksum = Mix(checksum, *eventID);
    checksum = Mix(checksum, *nHit4Edge);
  }
  f.Close();
  return checksum;
}

uint64_t HawcTrigEventCount(const std::string& path, uint64_t events, unsigned unused) {
  (void)events;
  (void)unused;
  XCDFFile f(path.c_str(), "r");
  uint64_t count = f.GetEventCount();
  f.Close();
  return Mix(0, count);
}

uint64_t HawcTrigGlobalRanges(const std::string& path, uint64_t events, unsigned unused) {
  (void)events;
  (void)unused;
  XCDFFile f(path.c_str(), "r");
  uint64_t checksum = 0;
  std::pair<uint64_t, uint64_t> eventRange = f.GetUnsignedIntegerFieldRange("trig.eventID");
  std::pair<uint64_t, uint64_t> n4Range = f.GetUnsignedIntegerFieldRange("trig.nHit4Edge");
  std::pair<uint64_t, uint64_t> loRange = f.GetUnsignedIntegerFieldRange("trig.loTOT_4Edge");
  std::pair<int64_t, int64_t> timeRange = f.GetSignedIntegerFieldRange("trig.hitTime_4Edge");
  checksum = Mix(checksum, eventRange.first);
  checksum = Mix(checksum, eventRange.second);
  checksum = Mix(checksum, n4Range.first);
  checksum = Mix(checksum, n4Range.second);
  checksum = Mix(checksum, loRange.first);
  checksum = Mix(checksum, loRange.second);
  checksum = Mix(checksum, static_cast<uint64_t>(timeRange.first));
  checksum = Mix(checksum, static_cast<uint64_t>(timeRange.second));
  f.Close();
  return checksum;
}

} // namespace

std::vector<Scenario> FeatureScenarios() {
  std::vector<Scenario> scenarios;
  scenarios.push_back(Scenario{"small_blocks",
      "Many small blocks: stresses frame/block transition overhead and block-table size",
      WriteBlockTableSmallBlocks,
      std::vector<Operation>{
          Operation("read", ReadBasic),
          Operation("seek", SeekBasic, SeekOperationCount),
          Operation("event_count", EventCountOnly, CountOne),
          Operation("global_ranges", GlobalRanges, CountOne)}});
  scenarios.push_back(Scenario{"large_blocks",
      "Large blocks: stresses decompression and intra-block skipping/seeking",
      WriteBlockTableLargeBlocks,
      std::vector<Operation>{
          Operation("read", ReadBasic),
          Operation("seek", SeekBasic, SeekOperationCount),
          Operation("event_count", EventCountOnly, CountOne),
          Operation("global_ranges", GlobalRanges, CountOne)}});
  scenarios.push_back(Scenario{"no_block_table",
      "Compatibility path with disabled block table/globals; event count, ranges, and seeks scan data",
      WriteNoBlockTable,
      std::vector<Operation>{
          Operation("read", ReadBasic),
          Operation("event_count_scan", EventCountOnly, CountEvents),
          Operation("global_ranges_scan", GlobalRanges, CountEvents)}});
  scenarios.push_back(Scenario{"many_comments",
      "Trailer/comment-heavy metadata load path",
      WriteManyComments,
      std::vector<Operation>{
          Operation("read", ReadBasic),
          Operation("comments", CommentsMetadata, CountOne)}});
  scenarios.push_back(Scenario{"aliases",
      "Header/trailer aliases and expression evaluation during reads",
      WriteAliases,
      std::vector<Operation>{
          Operation("read_fields", ReadAliasFields),
          Operation("read_aliases", ReadAliasEvaluate)}});
  scenarios.push_back(Scenario{"nested_vectors",
      "Recursive vector parentage plus vector-reduction alias evaluation",
      WriteNestedVectors,
      std::vector<Operation>{
          Operation("read", ReadNestedVectors)}});
  scenarios.push_back(Scenario{"concatenated",
      "Two XCDF files concatenated byte-for-byte; block-table offsets and seek remapping",
      WriteConcatenated,
      std::vector<Operation>{
          Operation("read", ReadConcatenated),
          Operation("seek", SeekConcatenated, SeekOperationCount),
          Operation("event_count", EventCountOnly, CountOne)}});
  scenarios.push_back(Scenario{"istream_read",
      "User-supplied std::istream read path rather than XCDF-owned file stream",
      WriteBlockTableLargeBlocks,
      std::vector<Operation>{
          Operation("read_file", ReadBasic),
          Operation("read_istream", ReadInputStream)}});
  scenarios.push_back(Scenario{"hawc_trig_fixture",
      "Existing HAWC trigger XCDF fixture/slice supplied with --fixture",
      FixtureNoWrite,
      std::vector<Operation>{
          Operation("read_full", HawcTrigReadFull, HawcTrigCount),
          Operation("read_scalars", HawcTrigReadScalars, HawcTrigCount),
          Operation("seek", HawcTrigSeek, SeekOperationCount),
          Operation("event_count", HawcTrigEventCount, CountOne),
          Operation("global_ranges", HawcTrigGlobalRanges, CountOne)},
      true});
  return scenarios;
}

} // namespace xcdf_perf
