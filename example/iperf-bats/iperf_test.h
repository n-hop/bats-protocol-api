/**
 * @file iperf_test.h
 * @author Peng LEI (peng.lei@n-hop.com)
 * @brief
 * @version 1.0.0
 * @date 2025-06-16
 *
 * Copyright (c) 2025 The n-hop technologies Limited. All Rights Reserved.
 *
 */
#ifndef SRC_EXAMPLE_IPERF_BATS_IPERF_TEST_H_
#define SRC_EXAMPLE_IPERF_BATS_IPERF_TEST_H_

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
#include "src/example/iperf-bats/iperf_test_api.h"

/** @brief Iperf test architecture overview
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *
 *                 IperfBatsApi
 *                     |
 *                     | (manages)
 *                     |
 *                 IperfTest
 *                /        \
 *           [streams]    TestConfig
 *              /   \
 *             /     \
 *    IperfStream .. IperfStream
 *                     /   \
 *                    /     \
 *                sending   report of sending & receiving
 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 ** @brief Iperf test communication channel overview
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *    [IperfTest] -------> control channel(tcp) ---------------> [IperfTest]
 *      |_ IperfStream 0  ---> data channel 0 (btp/brtp/..) ---> [IperfStream]
 *      |_ IperfStream 1  ---> data channel 1 (btp/brtp/..) ---> [IperfStream]
 *      ...
 *      |_ IperfStream n  ---> ...
 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 ** @brief Iperf test port number allocations
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *    test 1 (random port) <---control channel------\
 *                                                       server (`port` - 1)
 *    stream client 0 (random port) <---data channel---> server (`port`)
 *    stream client 1 (random port) <---data channel---/
 *
 *
 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 ** @brief Iperf test state machine
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *
 * SENDER:
 *
 *  TEST_NONE --Start()--> TEST_EXCHANGE_PARAMETERS (sending parameters of test.)
 *                                  |
 *                                 (ack)
 *                                  |
 *                             TEST_START (sending data)
 *                                  |
 *                             (sending ends.)
 *                                  |
 *                             TEST_EXCHANGE_REPORTS (sending summary of reports.)
 *                                  |
 *                                (ack)
 *                                  |
 *                              TEST_DONE
 *
 *
 * RECEIVER:
 *
 * TEST_NONE --received parameters --> TEST_START
 *                                        |
 *                                  (received summary of reports)
 *                                        |
 *                                    TEST_DONE
 *
 */

class IperfTest : public IIperfTest {
 public:
  IperfTest(IOContext& io, std::basic_ofstream<char>& log_stream, uint64_t id, const TestConfig& config);
  IperfTest(IOContext& io, std::basic_ofstream<char>& log_stream, const TestConfig& config);
  virtual ~IperfTest();
  bool Start() override;
  bool Stop() override;
  // for ROLE_RECEIVER & ROLE_SENDER
  void HandleControlMessage(const IBatsConnPtr& ctrl_conn, const octet* data, int length) override;

  /// @brief callback function when the sender finished sending data.
  void OnSentFinished() override;

  /// @brief check if it's ready to test. (data stream connected.)
  /// @return
  bool IsStreamReady() override;
  bool IsStreamDone() override;
  /// @brief check if the test is finished.
  /// @return
  bool TestFinishState() override;
  void PrintStreamSendSummary() override;

 protected:
  bool ControlChannelConnectionCallback(const IBatsConnPtr& ctrl_conn, const BatsConnEvent& event, const octet* data,
                                        int length, void* user);
  void DataListenCallback(const IBatsConnPtr& data_conn, const BatsListenEvent& event, void* user);

  void SendTestSummary();
  bool SendTestParameters();
  bool SendAck();
  void HandleAckMessage(const struct iperf_control_data* control_header);
  void HandleSummaryMessage(const struct iperf_control_data* control_header);
  void HandleTestParameters(const struct iperf_control_data* control_header);
  /// @brief Check whether expected interval data is received.
  void CheckStreamIntervalReceive();

 private:
  BatsProtocolPtr data_chn_listener_ = nullptr;
  BatsProtocolPtr ctrl_chn_connector_ = nullptr;
  std::mutex streams_mutex_;
  std::list<IperfStreamPtr> test_streams_;
  int passed_ticks_ = 0;
};

using IperfTestPtr = std::shared_ptr<IperfTest>;

#endif  // SRC_EXAMPLE_IPERF_BATS_IPERF_TEST_H_
