/**
 * @file iperf_bats_api.h
 * @author Lei Peng (peng.lei@n-hop.com)
 * @brief
 * @version 1.0.0
 * @date 2025-05-26
 *
 * Copyright (c) 2025 The n-hop technologies Limited. All Rights Reserved.
 *
 */
#ifndef SRC_EXAMPLE_IPERF_BATS_IPERF_BATS_API_H_
#define SRC_EXAMPLE_IPERF_BATS_IPERF_BATS_API_H_

#include <atomic>
#include <cstdint>
#include <functional>  // std::reference_wrapper
#include <list>
#include <memory>
#include <mutex>  // NOLINT
#include <string>
#include <unordered_map>
#include <vector>

#include "include/cpp/bats_connection.h"
#include "include/cpp/bats_iocontext.h"
#include "include/cpp/bats_protocol.h"
#include "src/example/iperf-bats/iperf_common.h"
#include "src/example/iperf-bats/iperf_printer.h"
#include "src/example/iperf-bats/iperf_stream.h"
#include "src/example/iperf-bats/iperf_test.h"

class IperfBatsApi : public IPrinter, public std::enable_shared_from_this<IperfBatsApi> {
 public:
  IperfBatsApi();
  virtual ~IperfBatsApi();
  TestRole InitFromArgs(int argc, char* argv[]);
  void RunServer();
  void RunTest();
  void StopAll();

 protected:
  void PrintRecvSummaryHeader() override;
  void PrintSentSummaryHeader() override;
  void PrintReceiverInfo() override;
  void PrintSenderInfo() override;
  void PrintReceiverFailure() override;
  void PrintSenderFailure() override;

 private:
  void EnterReceiverIntervalLoop();
  void EnterSenderIntervalLoop();
  void Usage();
  void HandleControlMessage(const IBatsConnPtr& ctrl_conn, const octet* data, int length);

  bool ValidateClientConfig(const TestConfig& test_config);
  bool ValidateServerConfig(const TestConfig& test_config);

  void ControlChannelListenCallback(const IBatsConnPtr& ctrl_conn, const BatsListenEvent& event, void* user);
  bool ControlChannelConnectionCallback(const IBatsConnPtr& ctrl_conn, const BatsConnEvent& event, const octet* data,
                                        int length, void* user);
  int default_resolution = 10;  // 10Hz
  IOContextPtr io_ = nullptr;
  std::string ver_info_;
  std::string log_file_;
  std::string bats_log_path_;
  std::basic_ofstream<char> log_stream_;
  BatsProtocolPtr ctrl_chn_listener_ = nullptr;
  std::mutex test_tbl_mutex_;
  std::unordered_map<uint64_t, IIperfTestPtr> test_tbl_;
  TestRole test_role_ = TestRole::ROLE_NONE;
  int server_listen_port_ = 0;  // port for server to listen on.
  std::string interface_name_;  // network interface name, e.g., "eth0", "wlan0"; ROLE_RECEIVER must set this.
  static int stop_signal_value;
  TestConfig user_input_config;
  bool is_ctrl_chn_connected_ = false;
  std::atomic<bool> is_stopped_ = false;
};

using IperfBatsApiPtr = std::shared_ptr<IperfBatsApi>;

#endif  // SRC_EXAMPLE_IPERF_BATS_IPERF_BATS_API_H_
