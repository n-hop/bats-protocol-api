/**
 * @file iperf_test_api.h
 * @author Peng LEI (peng.lei@n-hop.com)
 * @brief
 * @version 1.0.0
 * @date 2025-09-08
 *
 * Copyright (c) 2025 The n-hop technologies Limited. All Rights Reserved.
 *
 */
#ifndef SRC_EXAMPLE_IPERF_BATS_IPERF_TEST_API_H_
#define SRC_EXAMPLE_IPERF_BATS_IPERF_TEST_API_H_

#include <atomic>
#include <cstdint>
#include <fstream>
#include <list>
#include <memory>
#include <mutex>  // NOLINT
#include <string>

#include "include/cpp/bats_connection.h"
#include "include/cpp/bats_iocontext.h"
#include "include/cpp/bats_protocol.h"
#include "src/example/iperf-bats/iperf_common.h"
#include "src/example/iperf-bats/iperf_printer.h"
#include "src/example/iperf-bats/iperf_stream.h"

enum class TestState {
  TEST_NONE = 0,
  TEST_EXCHANGE_PARAMETERS = 1,  // exchanging parameters before starting the sending
  TEST_START,                    // started
  TEST_EXCHANGE_REPORTS,         // exchanging reports after sending is finished
  TEST_DONE,                     // done.
};

struct iperf_control_data {
  uint64_t magic;
  uint32_t test_id;
  // 01 setup/request, 02 exchange, 03 test results. 04 ack.
  uint8_t action_code;
  uint8_t num_of_streams;
  uint32_t param0;  // num. of files; response code(0 accepted, 1 denied).
  uint32_t param1;  // high bits of file length
  uint32_t param2;  // low bits of file length
  uint32_t param3;
  // void *data // of streams;
};

class IIperfTest {
 public:
  IIperfTest(IOContext& io, std::basic_ofstream<char>& log_stream, uint64_t id);
  IIperfTest(IOContext& io, std::basic_ofstream<char>& log_stream, uint64_t id, const TestConfig& config);
  IIperfTest(IOContext& io, std::basic_ofstream<char>& log_stream, const TestConfig& config);
  virtual ~IIperfTest() = default;
  const TestConfig& GetConfig() const { return test_config_; }
  uint64_t Id() const {
    assert(test_id_ != 0);
    return test_id_;
  }
  TestState GetState() const { return state_.load(); }
  void CloseCtrlChannel() { ctrl_chn_ = nullptr; }
  void SetResolution(int resolution) { test_config_.resolution = resolution; }
  void SetBoundInf(const std::string& interface_name) { interface_name_ = interface_name; }
  void SetPrinter(IPrinter* printer) { printer_ = printer; }
  void SetRole(const TestRole& role) { test_config_.role = role; }

  virtual void PrintStreamSendSummary() = 0;
  virtual void HandleControlMessage(const IBatsConnPtr& ctrl_conn, const octet* data, int length) = 0;
  virtual bool TestFinishState() = 0;

  virtual void OnSentFinished() = 0;
  virtual bool IsStreamReady() = 0;
  virtual bool IsStreamDone() = 0;
  virtual bool Start() = 0;
  virtual bool Stop() = 0;

 protected:
  IOContext& io_;
  std::basic_ofstream<char>& log_stream_;
  TestConfig test_config_;
  uint32_t test_id_ = 0;
  IBatsConnPtr ctrl_chn_ = nullptr;  // from api.
  std::atomic<TestState> state_ = {TestState::TEST_NONE};
  std::string interface_name_;  // network interface name, e.g., "eth0", "wlan0"; ROLE_RECEIVER must set this.
  IPrinter* printer_;
  std::atomic<bool> is_stopped_ = false;
};

using IIperfTestPtr = std::shared_ptr<IIperfTest>;

#endif  // SRC_EXAMPLE_IPERF_BATS_IPERF_TEST_API_H_
