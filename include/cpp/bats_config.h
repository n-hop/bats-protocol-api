/**
 * @file bats_config.h
 * @author Lei Peng (peng.lei@n-hop.com)
 * @brief
 * @version 1.0.0
 * @date 2025-01-01
 *
 * Copyright (c) 2025 The n-hop technologies Limited. All Rights Reserved.
 *
 */
#ifndef INCLUDE_CPP_BATS_CONFIG_H_
#define INCLUDE_CPP_BATS_CONFIG_H_
#include <cstdint>
#include <iostream>
#include <string>

#include "include/cpp/bats_iocontext.h"

enum class BATSTransMode : uint8_t {
  BTP = 0,          // Unreliable BATS Transport Protocol (with cc)
  BRTP = 1,         // BATS Reliable Transport Protocol (with cc)
  BRCTP = 2,        // [WIP]: BATS Rateless-coding Transport Protocol (with cc)
  TRANSPARENT = 3,  // udp, no fec, no cc.
};

enum class BATSCongestionControl : uint8_t {
  None = 0,   // congestion control disabled.
  BBR = 1,    // BBR congestion control.
  BBRv2 = 3,  // Not implemented.
  BBRv3 = 4,  // Not implemented.
  GCC = 5,    // Not implemented.
};

enum BATSFrameType : uint8_t {
  TRANSPARENT = 0,      // no bats header
  BATS_HEADER_MIN = 3,  // only has compatible header
  BATS_HEADER_V1 = 2,   // has compatible header and coding header
  BATS_HEADER_V0 = 1,   // `bats_framework_header`
};

///
/// @brief The configuration for BATS protocol instance.
///
class BatsConfiguration {
 public:
  BatsConfiguration() = default;
  ~BatsConfiguration() = default;

  BATSLogLevel log_level = BATSLogLevel::LOG_INFO;
  BATSTransMode transport_mode = BATSTransMode::BRTP;        // Protocol transport mode
  BATSFrameType frame_type = BATSFrameType::BATS_HEADER_V1;  // Protocol frame header
  BATSCongestionControl congestion_control = BATSCongestionControl::BBR;
  std::string cert_file;  // for TLS connection
  std::string key_file;   // for TLS connection
  std::string local_addr = "127.0.0.1";
  std::string remote_addr = "127.0.0.1";
  int local_port = 13475;
  int remote_port = 13475;
  int connection_timeout = 2000;  // milliseconds
  bool is_compression_enabled = false;
  bool is_encryption_enabled = true;
  friend std::ostream& operator<<(std::ostream& os, const BatsConfiguration& config);
};

#endif  // INCLUDE_CPP_BATS_CONFIG_H_
