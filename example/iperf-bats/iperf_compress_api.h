/**
 * @file iperf_compress_api.h
 * @author Peng LEI (peng.lei@n-hop.com)
 * @brief
 * @version 1.0.0
 * @date 2025-09-12
 *
 * Copyright (c) 2025 The n-hop technologies Limited. All Rights Reserved.
 *
 */
#ifndef SRC_EXAMPLE_IPERF_BATS_IPERF_COMPRESS_API_H_
#define SRC_EXAMPLE_IPERF_BATS_IPERF_COMPRESS_API_H_

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "src/include/types.h"

/// @brief For the time being, only support /density-0.14.2
/// density benchmark: https://quixdb.github.io/squash-benchmark/
/// Consider the ratio and speed.
class IperfCompressor {
 public:
  IperfCompressor() = default;
  virtual ~IperfCompressor() = default;

  IperfCompressor(const IperfCompressor&) = delete;
  IperfCompressor& operator=(const IperfCompressor&) = delete;
  IperfCompressor(IperfCompressor&& other) = delete;
  IperfCompressor& operator=(IperfCompressor&& other) = delete;

  static uint64_t Compress(const octet* in, int length, octet* out, int out_length);
  static uint64_t Decompress(const octet* in, int length, octet* out, int out_length);
};

#endif  // SRC_EXAMPLE_IPERF_BATS_IPERF_COMPRESS_API_H_
