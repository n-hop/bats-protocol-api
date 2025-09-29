/**
 * @file iperf_compress_api.cc
 * @author Peng LEI (peng.lei@n-hop.com)
 * @brief
 * @version 1.0.0
 * @date 2025-09-12
 *
 * Copyright (c) 2025 The n-hop technologies Limited. All Rights Reserved.
 *
 */

#include "src/example/iperf-bats/iperf_compress_api.h"

#include <density_api.h>
#include <spdlog/cfg/env.h>   // support for loading levels from the environment variable
#include <spdlog/fmt/ostr.h>  // support for user defined types
#include <spdlog/spdlog.h>

#include <cstring>

uint64_t IperfCompressor::Compress(const octet* in, int length, octet* out, int out_length) {
  if (!in || length <= 0 || !out || out_length <= 0) return false;
  auto compress_safe_size = density_compress_safe_size(length);
  if (out_length < compress_safe_size) {
    spdlog::error("[IperfCompressor] The length of the out buffer {} is smaller than the compress safe size {}.",
                  out_length, compress_safe_size);
    return 0;
  }
  auto rc = density_compress(in, length, out, out_length, DENSITY_ALGORITHM_CHAMELEON);
  if (rc.state == DENSITY_STATE_OK) {
    return rc.bytesWritten;
  }
  spdlog::error("[IperfCompressor] Failed to compress {} bytes, state {}.", length, static_cast<int>(rc.state));
  return 0;
}

uint64_t IperfCompressor::Decompress(const octet* in, int length, octet* out, int out_length) {
  if (!in || length <= 0 || !out || out_length <= 0) return false;
  auto decompress_safe_size = density_decompress_safe_size(length);
  if (out_length < decompress_safe_size) {
    spdlog::error("[IperfCompressor] The length of the out buffer {} is smaller than the decompress safe size {}.",
                  out_length, decompress_safe_size);
    return 0;
  }
  auto rc = density_decompress(in, length, out, out_length);
  if (rc.state == DENSITY_STATE_OK) {
    return rc.bytesWritten;
  }
  spdlog::error("[IperfCompressor] Failed to decompress {} bytes, state {}. decompress_safe_size {} out_length {}",
                length, static_cast<int>(rc.state), decompress_safe_size, out_length);
  return 0;
}
