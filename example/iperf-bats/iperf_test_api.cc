/**
 * @file iperf_test_api.cc
 * @author Peng LEI (peng.lei@n-hop.com)
 * @brief
 * @version 1.0.0
 * @date 2025-09-08
 *
 * Copyright (c) 2025 The n-hop technologies Limited. All Rights Reserved.
 *
 */

#include "src/example/iperf-bats/iperf_test_api.h"

#include "src/include/time.h"

IIperfTest::IIperfTest(IOContext& io, std::basic_ofstream<char>& log_stream, uint64_t id)
    : io_(io), log_stream_(log_stream), test_id_(id) {}
IIperfTest::IIperfTest(IOContext& io, std::basic_ofstream<char>& log_stream, uint64_t id, const TestConfig& config)
    : io_(io), log_stream_(log_stream), test_config_(config), test_id_(id) {}
IIperfTest::IIperfTest(IOContext& io, std::basic_ofstream<char>& log_stream, const TestConfig& config)
    : io_(io), log_stream_(log_stream), test_config_(config) {
  test_id_ = get_time_in_ms<ClockType::MONOTONIC>();
}
