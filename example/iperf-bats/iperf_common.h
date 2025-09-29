/**
 * @file iperf_common.h
 * @author Lei Peng (peng.lei@n-hop.com)
 * @brief
 * @version 1.0.0
 * @date 2025-05-26
 *
 * Copyright (c) 2025 The n-hop technologies Limited. All Rights Reserved.
 *
 */
#ifndef SRC_EXAMPLE_IPERF_BATS_IPERF_COMMON_H_
#define SRC_EXAMPLE_IPERF_BATS_IPERF_COMMON_H_

#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include "include/cpp/bats_config.h"

static const uint64_t bats_iperf_magic_code = 0xa1b2c3d4a5b6c7d8;
const char report_bw_retrans_cwnd_header[] = "[ ID] Interval           Transfer     Bitrate         Retr  Cwnd\n";
const char report_bw_retrans_cwnd_header_bidir[] =
    "[ ID][Role] Interval           Transfer     Bitrate         Retr  Cwnd\n";

const char send_report_bw_udp_header[] =
    "[ ID] Interval           Transfer     Bitrate         Jitter    Lost/Total Datagrams\n";
const char send_report_bw_udp_header_bidir[] =
    "[ ID][Role] Interval           Transfer     Bitrate         Jitter    Lost/Total Datagrams\n";

const char recv_report_bw_udp_header[] =
    "[ ID] Interval           Received     Bitrate         Jitter    Lost/Total Datagrams\n";
const char recv_report_bw_udp_header_bidir[] =
    "[ ID][Role] Interval           Received     Bitrate         Jitter    Lost/Total Datagrams\n";

enum class TestRole {
  ROLE_NONE = 0,
  ROLE_SENDER,
  ROLE_RECEIVER,
  ROLE_FILE_SENDER,
  ROLE_FILE_RECEIVER,
  ROLE_OBSERVER,  // print usage
};

inline bool isSenderRole(const TestRole& role) {
  return (role == TestRole::ROLE_SENDER || role == TestRole::ROLE_FILE_SENDER);
}

inline bool isReceiverRole(const TestRole& role) {
  return (role == TestRole::ROLE_RECEIVER || role == TestRole::ROLE_FILE_RECEIVER);
}

const char bats_default_cert_file[] = "/etc/bats-protocol/server.cert";
const char bats_default_key_file[] = "/etc/bats-protocol/server.key";

std::string ReadIPAddress(const std::string& interface_name);
std::string RoleString(const TestRole& role);

bool IsSupportedProtocol(int protocol);
bool IsReliableProtocol(int protocol);

class TestInterval {
 public:
  uint64_t start_seq;
  uint64_t bytes = 0;
  uint64_t datagrams = 0;
  int64_t lost_datagrams = 0;
  double bitrate = 0.0;  // in bytes/sec
  double jitter = 0.0;   // in milliseconds
  double loss = 0.0;     // in %

  std::string get_bitrate_string() const;
  std::string get_bytes_string() const;
};

class TestReport {
 public:
  TestReport() = default;

  uint64_t last_snapshot_time = 0;  // in ms.
  float interval = 1.0;             // in seconds.
  int snapshot_cnt = 0;
  TestInterval summary;

  TestInterval& GetLastInterval() { return intervals[last_interval_index]; }
  TestInterval& GetLastSnapshot() { return intervals[last_snapshot_index]; }

 private:
  const int last_interval_index = 1;
  const int last_snapshot_index = 0;
  std::array<TestInterval, 2> intervals;
};

class TestReportOfStream {
 public:
  int stream_id = 0;
  TestReport sender_report;    // tx
  TestReport receiver_report;  // rx
};

class TestConfig {
 public:
  /// @brief Serialize the configuration to a byte buffer.
  /// @param to_buffer
  octet* Serialize(octet* to_buffer) const {
    *reinterpret_cast<uint32_t*>(to_buffer) = static_cast<uint32_t>(interval * 100);
    to_buffer += sizeof(uint32_t);

    *reinterpret_cast<uint32_t*>(to_buffer) = static_cast<uint32_t>(time * 100);
    to_buffer += sizeof(uint32_t);

    // protocol
    to_buffer[0] = static_cast<uint8_t>(protocol);
    // is reverse enabled
    to_buffer[1] = static_cast<uint8_t>(is_reverse_enabled ? 1 : 0);
    to_buffer += 2;

    // port
    *reinterpret_cast<uint16_t*>(to_buffer) = port;
    to_buffer += sizeof(uint16_t);
    return to_buffer;
  }

  /// @brief Deserialize the configuration from a byte buffer.
  /// @param from_buffer
  void Deserialize(const octet* from_buffer) {
    interval = *reinterpret_cast<const uint32_t*>(from_buffer) / 100.0f;
    from_buffer += sizeof(uint32_t);

    time = *reinterpret_cast<const uint32_t*>(from_buffer) / 100.0f;
    from_buffer += sizeof(uint32_t);

    protocol = static_cast<int>(from_buffer[0]);
    is_reverse_enabled = (from_buffer[1] != 0);

    from_buffer += 2;
    port = *reinterpret_cast<const uint16_t*>(from_buffer);
  }

  float interval = 1;                   // in seconds
  float time = 10;                      // in seconds
  float resolution = 1;                 // 1 tick per second, default 1 Hz
  int num_streams = 1;                  // number of streams in parallel, default 1
  int protocol = 0;                     // default to BTP
  bool is_reverse_enabled = false;      // reverse mode: server--> client; otherwise client--> server
  bool is_compression_enabled = false;  //
  bool is_debug_mode = false;
  bool is_precise_time_enabled = false;  // show precise wall clock time in reports
  int port = 5201;                       // default port.
  int max_allowed_sending_rate = 0;
  std::string peer_host;
  std::vector<std::string> files;  // list of full_file_path
  int64_t total_bytes_in_files = 0;
  int num_files = 0;  // number of files for receivers' reference
  TestRole role = TestRole::ROLE_NONE;
};

inline std::ostream& operator<<(std::ostream& os, const TestConfig& config) {
  os << "TestConfig interval: " << config.interval << "\nTestConfig time: " << config.time
     << "\nTestConfig port: " << config.port << "\nTestConfig num_streams: " << config.num_streams
     << "\nTestConfig is_compression_enabled: " << (config.is_compression_enabled ? "true" : "false")
     << "\nTestConfig is_reverse_enabled: " << (config.is_reverse_enabled ? "true" : "false")
     << "\nTestConfig is_debug_mode: " << (config.is_debug_mode ? "true" : "false")
     << "\nTestConfig peer_host: " << config.peer_host << "\nTestConfig protocol: " << config.protocol
     << "\nTestConfig role: " << (config.role == TestRole::ROLE_SENDER ? "CLIENT" : "SERVER");
  return os;
}

#endif  // SRC_EXAMPLE_IPERF_BATS_IPERF_COMMON_H_
