/**
 * @file seq_recorder_test.cc
 * @author Lei Peng (peng.lei@n-hop.com)
 * @brief
 * @version 1.0.0
 * @date 2025-06-05
 *
 * Copyright (c) 2025 The n-hop technologies Limited. All Rights Reserved.
 *
 */

#include "src/util/seq_recorder.h"

#include <gtest/gtest.h>

#include <iostream>
#include <set>
#include <vector>

#include "spdlog/fmt/bin_to_hex.h"

TEST(SeqRecorder1M, test0) {
  uint64_t min_seq = 0;
  uint64_t max_seq = 10000;
  SeqRecorder1M recorder;
  std::set<uint64_t> lost_seq = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
  for (uint64_t i = min_seq; i <= max_seq; ++i) {
    recorder.Record(i);
  }
  EXPECT_EQ(recorder.MinSeqRecorded(), min_seq);
  EXPECT_EQ(recorder.MaxSeqRecorded(), max_seq);
  // [0, max_seq)
  EXPECT_EQ(recorder.RecordedNumInRange(0, max_seq), max_seq);
  EXPECT_EQ(recorder.RecordedNumInRange(0, 32), 32);
  EXPECT_EQ(recorder.RecordedNumInRange(0, 33), 33);

  EXPECT_EQ(recorder.RecordedNumInRange(0, 0), 0);
  EXPECT_EQ(recorder.RecordedNumInRange(0, 1), 1);
  EXPECT_EQ(recorder.RecordedNumInRange(0, 2), 2);

  EXPECT_EQ(recorder.RecordedNumInRange(100, 100), 0);
  EXPECT_EQ(recorder.RecordedNumInRange(100, 101), 1);
  EXPECT_EQ(recorder.RecordedNumInRange(100, 102), 2);

  EXPECT_EQ(recorder.RecordedNumInRange(1, 10), 9);
  EXPECT_EQ(recorder.RecordedNumInRange(20, 30), 10);
  EXPECT_EQ(recorder.RecordedNumInRange(100, 110), 10);
  recorder.Reset();

  // validate the reset.
  EXPECT_EQ(recorder.RecordedNumInRange(1, 10), 0);
  EXPECT_EQ(recorder.RecordedNumInRange(20, 30), 0);
  EXPECT_EQ(recorder.RecordedNumInRange(100, 110), 0);

  for (uint64_t i = min_seq; i <= max_seq; ++i) {
    if (lost_seq.find(i) == lost_seq.end()) {
      recorder.Record(i);
    }
  }
  EXPECT_EQ(recorder.MinSeqRecorded(), min_seq);
  EXPECT_EQ(recorder.MaxSeqRecorded(), max_seq);
  EXPECT_EQ(recorder.RecordedNumInRange(0, max_seq), max_seq - lost_seq.size());
  EXPECT_EQ(recorder.RecordedNumInRange(1, 2), 0);
  EXPECT_EQ(recorder.RecordedNumInRange(2, 3), 0);
  EXPECT_EQ(recorder.RecordedNumInRange(3, 4), 0);
  EXPECT_EQ(recorder.RecordedNumInRange(4, 11), 0);
  EXPECT_EQ(recorder.RecordedNumInRange(1, 11), 0);

  EXPECT_EQ(recorder.RecordedNumInRange(20, 30), 10);
  EXPECT_EQ(recorder.RecordedNumInRange(100, 110), 10);

  EXPECT_EQ(recorder.RecordedNumInRange(100, 100), 0);
  EXPECT_EQ(recorder.RecordedNumInRange(100, 101), 1);
  EXPECT_EQ(recorder.RecordedNumInRange(100, 102), 2);

  // out of range.
  EXPECT_EQ(recorder.RecordedNumInRange(10000, 10002), 0);
  EXPECT_EQ(recorder.RecordedNumInRange(10000, 10012), 0);

  // overlapping.
  EXPECT_EQ(recorder.RecordedNumInRange(9999, 10002), 1);
  EXPECT_EQ(recorder.RecordedNumInRange(9998, 10002), 2);
}

TEST(SeqRecorder1M, test1) {
  uint64_t min_seq = 10001;
  uint64_t max_seq = 20001;
  SeqRecorder1M recorder;
  std::set<uint64_t> lost_seq = {10001, 10002, 10003, 10004, 10005, 10006, 10007, 10008, 10009, 10010};
  for (uint64_t i = min_seq; i <= max_seq; ++i) {
    recorder.Record(i);
  }
  EXPECT_EQ(recorder.MinSeqRecorded(), min_seq);
  EXPECT_EQ(recorder.MaxSeqRecorded(), max_seq);
  // [0, max_seq)
  EXPECT_EQ(recorder.RecordedNumInRange(min_seq, max_seq), max_seq - min_seq);
  recorder.Reset();
  // simulate loss.

  for (uint64_t i = min_seq; i <= max_seq; ++i) {
    if (lost_seq.find(i) == lost_seq.end()) {
      recorder.Record(i);
    }
  }
  EXPECT_EQ(recorder.MinSeqRecorded(), 10011);  // 10001 is lost.
  EXPECT_EQ(recorder.MaxSeqRecorded(), max_seq);
  EXPECT_EQ(recorder.RecordedNumInRange(min_seq, max_seq), max_seq - min_seq - lost_seq.size());

  // out of range.
  EXPECT_EQ(recorder.RecordedNumInRange(1, 2), 0);
  EXPECT_EQ(recorder.RecordedNumInRange(1, 11), 0);
  EXPECT_EQ(recorder.RecordedNumInRange(20, 30), 0);
  EXPECT_EQ(recorder.RecordedNumInRange(100, 110), 0);

  // overlapping.
  EXPECT_EQ(recorder.RecordedNumInRange(110, 10002), 0);

  // In Range.
  EXPECT_EQ(recorder.RecordedNumInRange(10001, 10002), 0);
  EXPECT_EQ(recorder.RecordedNumInRange(10003, 10004), 0);
  EXPECT_EQ(recorder.RecordedNumInRange(10001, 10011), 0);

  EXPECT_EQ(recorder.RecordedNumInRange(10011, 10012), 1);
  EXPECT_EQ(recorder.RecordedNumInRange(10011, 10013), 2);
  EXPECT_EQ(recorder.RecordedNumInRange(10011, 10014), 3);
  EXPECT_EQ(recorder.RecordedNumInRange(10011, 11011), 1000);
}

TEST(SeqRecorder1M, test2) {
  uint64_t min_seq = 822696;
  uint64_t max_seq = 1241578;
  SeqRecorder1M recorder;
  for (uint64_t i = min_seq; i <= max_seq; ++i) {
    recorder.Record(i);
  }
  EXPECT_EQ(recorder.MinSeqRecorded(), min_seq);
  EXPECT_EQ(recorder.MaxSeqRecorded(), max_seq);
  // [min_seq, max_seq)
  EXPECT_EQ(recorder.RecordedNumInRange(min_seq, max_seq), max_seq - min_seq);
  EXPECT_EQ(recorder.RecordedNumInRange(822696, 822697), 1);
  EXPECT_EQ(recorder.RecordedNumInRange(832696, 832697), 1);
  EXPECT_EQ(recorder.RecordedNumInRange(999999, 1000000), 1);
  EXPECT_EQ(recorder.RecordedNumInRange(1000000, 1000001), 1);
  EXPECT_EQ(recorder.RecordedNumInRange(1000000, 1000002), 2);
  EXPECT_EQ(recorder.RecordedNumInRange(1241570, 1241572), 2);
}

TEST(SeqRecorder1M, test3) {
  uint64_t min_seq = 100822696;
  uint64_t max_seq = 101241578;
  SeqRecorder1M recorder;
  for (uint64_t i = min_seq; i <= max_seq; ++i) {
    recorder.Record(i);
  }
  EXPECT_EQ(recorder.MinSeqRecorded(), min_seq);
  EXPECT_EQ(recorder.MaxSeqRecorded(), max_seq);
  // [min_seq, max_seq)
  EXPECT_EQ(recorder.RecordedNumInRange(min_seq, max_seq), max_seq - min_seq);
}

TEST(SeqRecorder1M, test4) {
  uint64_t min_seq = 791184;
  uint64_t max_seq = 1171566;
  SeqRecorder1M recorder;
  for (uint64_t i = min_seq; i <= max_seq; ++i) {
    recorder.Record(i);
  }
  EXPECT_EQ(recorder.MinSeqRecorded(), min_seq);
  EXPECT_EQ(recorder.MaxSeqRecorded(), max_seq);
  // [min_seq, max_seq)
  EXPECT_EQ(recorder.RecordedNumInRange(min_seq, max_seq), max_seq - min_seq);
  EXPECT_EQ(recorder.RecordedNumInRange(min_seq, max_seq), 380382);
}

TEST(SeqRecorder256, test0) {
  SeqRecorder256 recorder;
  std::cout << "TotalBytesUsed: " << recorder.TotalBytesUsed() << std::endl;
  std::vector<uint8_t> data;
  data.resize(32);
  assert(recorder.TotalBytesUsed() > 32);
  std::memcpy(data.data(), recorder.GetBytes(), 32);
  for (const auto x : data) {
    EXPECT_EQ(x, 0);
  }

  for (int i = 0; i <= 255; i++) {
    recorder.Record(i);
  }
  std::memcpy(data.data(), recorder.GetBytes(), 32);
  for (const auto x : data) {
    EXPECT_EQ(x, 0xff);
  }

  recorder.Reset();
  for (int i = 0; i <= 255; i++) {
    if (i % 2 == 0) {
      recorder.Record(i);
    }
  }
  std::memcpy(data.data(), recorder.GetBytes(), 32);
  for (const auto x : data) {
    EXPECT_EQ(x, 0x55);
  }

  recorder.Reset();
  for (int i = 0; i <= 10; i++) {
    recorder.Record(i);
  }
  for (int i = 50; i <= 78; i++) {
    recorder.Record(i);
  }

  for (int i = 0; i <= 255; i++) {
    if (i >= 0 && i <= 10) {
      EXPECT_TRUE(recorder.Has(i));
    } else if (i >= 50 && i <= 78) {
      EXPECT_TRUE(recorder.Has(i));
    } else {
      EXPECT_FALSE(recorder.Has(i));
    }
  }

  std::memcpy(data.data(), recorder.GetBytes(), 32);
  spdlog::info("1Bitmap: {:n}", spdlog::to_hex(std::begin(data), std::begin(data) + 32));
  // ff        07     00 00 00 00 fc ff
  // 11111111 00000111 00000000 00000000 00000000 00000000 11111100 11111111
  SeqRecorder256 recorder_copy(reinterpret_cast<const char*>(data.data()), 32);
  std::memcpy(data.data(), recorder_copy.GetBytes(), 32);
  spdlog::info("2Bitmap: {:n}", spdlog::to_hex(std::begin(data), std::begin(data) + 32));
  for (int i = 0; i <= 255; i++) {
    if (i >= 0 && i <= 10) {
      EXPECT_TRUE(recorder_copy.Has(i));
    } else if (i >= 50 && i <= 78) {
      EXPECT_TRUE(recorder_copy.Has(i));
    } else {
      EXPECT_FALSE(recorder_copy.Has(i));
    }
  }
}
