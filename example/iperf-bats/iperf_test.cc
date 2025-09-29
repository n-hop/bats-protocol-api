/**
 * @file iperf_test.cc
 * @author Peng LEI (peng.lei@n-hop.com)
 * @brief
 * @version 1.0.0
 * @date 2025-06-16
 *
 * Copyright (c) 2025 The n-hop technologies Limited. All Rights Reserved.
 *
 */

#include "src/example/iperf-bats/iperf_test.h"

#if __has_include(<filesystem>)
#include <filesystem>  // NOLINT(build/c++17)
namespace std_fs = std::filesystem;
#elif __has_include(<experimental/filesystem>)
#include <experimental/filesystem>
namespace std_fs = std::experimental::filesystem;
#else
error "Missing the <filesystem> header."
#endif
#include <spdlog/cfg/env.h>   // support for loading levels from the environment variable
#include <spdlog/fmt/ostr.h>  // support for user defined types
#include <spdlog/spdlog.h>

#include <list>
#include <memory>

#include "src/include/time.h"

IperfTest::IperfTest(IOContext& io, std::basic_ofstream<char>& log_stream, uint64_t id, const TestConfig& config)
    : IIperfTest(io, log_stream, id, config) {}

IperfTest::IperfTest(IOContext& io, std::basic_ofstream<char>& log_stream, const TestConfig& config)
    : IIperfTest(io, log_stream, config) {
  spdlog::info("[IperfTest] Test is created with id: {}", test_id_);
}

IperfTest::~IperfTest() {}

bool IperfTest::Start() {
  // 0. check existence of bats_default_key_file
  if (!std_fs::exists(bats_default_key_file)) {
    spdlog::error("[IperfTest] BATS default key file {} does not exist.", bats_default_key_file);
    return false;
  }
  if (!std_fs::exists(bats_default_cert_file)) {
    spdlog::error("[IperfTest] BATS default cert file {} does not exist.", bats_default_cert_file);
    return false;
  }

  // 1. start control channel.
  BatsConfiguration bats_config;
  bats_config.SetTimeout(1000 * 1000);
  // default to TCP
  bats_config.SetFrameType(FrameType::BATS_HEADER_MIN);
  bats_config.SetCertFile(bats_default_cert_file);
  bats_config.SetKeyFile(bats_default_key_file);
  bats_config.SetMode(static_cast<TransMode>(10));
  bats_config.SetRemoteAddr(test_config_.peer_host);
  // use the different port for control channel.
  bats_config.SetRemotePort(test_config_.port - 1);
  ctrl_chn_connector_ = std::make_shared<BatsProtocol>(io_, bats_config);
  ctrl_chn_connector_->StartConnect(
      std::bind(&IperfTest::ControlChannelConnectionCallback, this, std::placeholders::_1, std::placeholders::_2,
                std::placeholders::_3, std::placeholders::_4, std::placeholders::_5),
      nullptr);
  return true;
}

bool IperfTest::Stop() {
  if (is_stopped_.exchange(true)) {
    return false;
  }
  spdlog::info("[IperfTest] Test is stopped!");
  state_ = TestState::TEST_DONE;
  for (auto& stream : test_streams_) {
    stream->Stop();
  }
  std::list<IperfStreamPtr>().swap(test_streams_);
  return true;
}

void IperfTest::OnSentFinished() {
  if (test_config_.role == TestRole::ROLE_NONE) {
    spdlog::warn("[IperfTest] Unexpected test role.");
    return;
  }
  // 6. test is finished then exchange reports. (usually sender sends reports to receiver)
  spdlog::info("[IperfTest] Iperf test completed (OnSentFinished). Role {}", static_cast<int>(test_config_.role));
  for (auto& stream : test_streams_) {
    spdlog::info("[IperfTest] Iperf test stream {} report is finished.", stream->Id());
    stream->FinishReport();
  }
  assert(printer_ != nullptr);
  printer_->PrintSentSummaryHeader();
  // STREAM_STOP ---- ack ---> STREAM_DONE
  // Wait the last sent data to be acked when stream is reliable.
  // send summary via control channel.
  SendTestSummary();
}

void IperfTest::DataListenCallback(const IBatsConnPtr& data_conn, const BatsListenEvent& event, void* user) {
  switch (event) {
    case BatsListenEvent::BATS_LISTEN_NEW_CONNECTION:
      spdlog::info("[IperfTest] Iperf data stream is accepted on port {} protocol {}.", test_config_.port,
                   test_config_.protocol);
      {
        assert(data_conn != nullptr);
        auto copied_config = test_config_;
        copied_config.role = TestRole::ROLE_RECEIVER;  // receiver role.
        auto data_stream = std::make_shared<IperfStream>(io_, log_stream_, copied_config, data_conn);
        data_stream->Start();  // just set callback.
        {
          std::scoped_lock lock(streams_mutex_);
          test_streams_.push_back(data_stream);
        }
      }
      break;
    case BatsListenEvent::BATS_LISTEN_SUCCESS:
      spdlog::info("[IperfTest] Iperf test is listening on port {}.", test_config_.port);
      break;
    case BatsListenEvent::BATS_LISTEN_FAILED:
      spdlog::info("[IperfTest] Failed to listen on port {}.", test_config_.port);
      break;
    default:
      break;
  }
}

/// @brief handler for the direction from `SENDER` to `RECEIVER`
bool IperfTest::ControlChannelConnectionCallback(const IBatsConnPtr& ctrl_conn, const BatsConnEvent& event,
                                                 const octet* data, int length, void* user) {
  switch (event) {
    case BatsConnEvent::BATS_CONNECTION_FAILED:
      printer_->PrintSenderFailure();
      break;
    case BatsConnEvent::BATS_CONNECTION_CLOSED:
    case BatsConnEvent::BATS_CONNECTION_SHUTDOWN_BY_PEER:
      this->CloseCtrlChannel();
      this->Stop();
      spdlog::info("[IperfTest] (ControlChannel) Iperf control stream is closed.");
      break;
    case BatsConnEvent::BATS_CONNECTION_ESTABLISHED:
      // sender role only. (ctrl_chn_ is ready)
      ctrl_chn_ = ctrl_conn;
      printer_->PrintSenderInfo();
      // 3. exchange parameters. (usually sender sends parameters to receiver)
      SendTestParameters();

      // 4. start test stream when ack is received.
      spdlog::info("[IperfTest] (ControlChannel) Iperf control stream is established.");
      break;
    case BatsConnEvent::BATS_CONNECTION_DATA_RECEIVED:
      // receiver and sender role.
      spdlog::info("[IperfTest] (ControlChannel) Iperf control stream received data, length: {}", length);
      HandleControlMessage(ctrl_conn, data, length);
      break;
    default:
      break;
  }
  return true;
}

void IperfTest::HandleControlMessage(const IBatsConnPtr& ctrl_conn, const octet* data, int length) {
  if (ctrl_chn_ == nullptr) {
    ctrl_chn_ = ctrl_conn;
  }
  const struct iperf_control_data* control_header = reinterpret_cast<const struct iperf_control_data*>(data);
  assert(control_header->test_id == test_id_);
  switch (control_header->action_code) {
    case 0x04:
      spdlog::info("[IperfTest] Testing ack is received.");
      HandleAckMessage(control_header);
      break;
    case 0x03:
      spdlog::info("[IperfTest] Testing summary is received.");
      HandleSummaryMessage(control_header);
      break;
    case 0x02:
      // test parameters exchange.
      spdlog::info("[IperfTest] Testing parameters are received.");
      HandleTestParameters(control_header);
      break;
    default:
      break;
  }
}

void IperfTest::HandleAckMessage([[maybe_unused]] const struct iperf_control_data* control_header) {
  if (state_ == TestState::TEST_DONE) {
    return;
  }
  if (state_ == TestState::TEST_EXCHANGE_PARAMETERS) {
    state_ = TestState::TEST_START;
    spdlog::info("[IperfTest] Test ack is received. start test streams.");
    assert(printer_ != nullptr);
    printer_->PrintSentSummaryHeader();
    for (int i = 0; i < test_config_.num_streams; ++i) {
      auto stream = std::make_shared<IperfStream>(io_, log_stream_, test_config_);
      stream->Start();
      test_streams_.push_back(stream);
    }
    return;
  }

  if (state_ == TestState::TEST_EXCHANGE_REPORTS) {
    spdlog::info("[IperfTest] Test ack is received. test is done.");
    state_ = TestState::TEST_DONE;
    return;
  }
  spdlog::warn("IperfTest received ack in unexpected state: {}", static_cast<int>(state_.load()));
}

void IperfTest::HandleSummaryMessage(const struct iperf_control_data* control_header) {
  if (state_ == TestState::TEST_DONE) {
    return;
  }
  auto num_of_streams = control_header->num_of_streams;
  assert(control_header->test_id == test_id_);
  auto data = reinterpret_cast<const octet*>(control_header) + sizeof(struct iperf_control_data);
  assert(printer_ != nullptr);
  printer_->PrintRecvSummaryHeader();
  std::scoped_lock lock(streams_mutex_);
  for (int i = 0; i < num_of_streams; i++) {
    auto stream_identifier = *reinterpret_cast<const uint64_t*>(data);
    // offset the stream_identifier
    data += sizeof(uint64_t);
    // match stream via identifier; low efficiency(num_of_streams^2) but doesn't matter.
    for (auto& stream : test_streams_) {
      if (stream->GetSteamIdentifier() == stream_identifier) {
        data = stream->DeserializeReport(data);
        // stop the receive stream.
        spdlog::info("[IperfTest] Test stream {} report finished.", stream->Id());
        stream->FinishReport();
        stream->PrintReceiveSummary();
        // received from clients. (DeserializeReport)
        stream->PrintSendSummary();
      }
    }
  }
  SendAck();
}

void IperfTest::HandleTestParameters(const struct iperf_control_data* control_header) {
  if (state_ == TestState::TEST_DONE) {
    return;
  }
  TestConfig received_test_config;
  auto data = reinterpret_cast<const octet*>(control_header) + sizeof(struct iperf_control_data);
  received_test_config.Deserialize(data);

  std::stringstream ss;
  ss << received_test_config;
  spdlog::info("[IperfTest] Test parameters received {}", ss.str());

  BatsConfiguration bats_config;
  bats_config.SetTimeout(received_test_config.interval * received_test_config.time * 1000);
  if (received_test_config.protocol == 0 || received_test_config.protocol == 1) {
    bats_config.SetFrameType(FrameType::BATS_HEADER_V1);
  }
  // udp.quic
  if (received_test_config.protocol == 11 || received_test_config.protocol == 12) {
    bats_config.SetFrameType(FrameType::TRANSPARENT);
  }
  // tcp
  if (received_test_config.protocol == 10) {
    bats_config.SetFrameType(FrameType::BATS_HEADER_MIN);
  }
  bats_config.SetCertFile(bats_default_cert_file);
  bats_config.SetKeyFile(bats_default_key_file);
  bats_config.SetMode(static_cast<TransMode>(received_test_config.protocol));
  data_chn_listener_ = std::make_shared<BatsProtocol>(io_, bats_config);
  received_test_config.resolution = test_config_.resolution;
  received_test_config.is_debug_mode = test_config_.is_debug_mode;
  received_test_config.is_precise_time_enabled = test_config_.is_precise_time_enabled;
  test_config_ = received_test_config;
  test_config_.role = TestRole::ROLE_RECEIVER;
  spdlog::info("[IperfTest] data chn StartListen on {} {}, protocol {}", interface_name_, received_test_config.port,
               test_config_.protocol);
  using namespace std::placeholders;  // NOLINT
  data_chn_listener_->StartListen(ReadIPAddress(interface_name_), received_test_config.port,
                                  std::bind(&IperfTest::DataListenCallback, this, _1, _2, _3), nullptr);
  // ack
  SendAck();
  // print header.
  assert(printer_ != nullptr);
  printer_->PrintRecvSummaryHeader();
}

void IperfTest::SendTestSummary() {
  if (ctrl_chn_ == nullptr) {
    spdlog::info("[IperfTest] no control channel. unable to send test summary.");
    return;
  }
  octetVec iper_control_data;
  iper_control_data.resize(1100);
  octet* ptr = iper_control_data.data();
  // header
  iperf_control_data* control_header = reinterpret_cast<iperf_control_data*>(ptr);
  control_header->magic = bats_iperf_magic_code;
  control_header->test_id = Id();
  control_header->action_code = 0x03;
  control_header->num_of_streams = test_streams_.size();

  ptr += sizeof(struct iperf_control_data);

  // body
  for (auto& stream : test_streams_) {
    ptr = stream->SerializeReport(ptr);
  }

  ctrl_chn_->SendData(iper_control_data.data(), iper_control_data.size());
  state_ = TestState::TEST_EXCHANGE_REPORTS;
  spdlog::info("[IperfTest] Testing summary is sent.");
}

// exchange parameters
bool IperfTest::SendTestParameters() {
  octetVec iper_control_data;
  iper_control_data.resize(300);
  [[maybe_unused]] octet* ptr = iper_control_data.data();
  // header
  iperf_control_data* control_header = reinterpret_cast<iperf_control_data*>(ptr);
  control_header->magic = bats_iperf_magic_code;
  control_header->test_id = Id();
  control_header->action_code = 0x02;
  control_header->num_of_streams = test_streams_.size();

  ptr += sizeof(struct iperf_control_data);

  // body (Serialize)
  ptr = test_config_.Serialize(ptr);

  ctrl_chn_->SendData(iper_control_data.data(), iper_control_data.size());
  spdlog::info("[IperfTest] Test parameters are sent.");
  state_ = TestState::TEST_EXCHANGE_PARAMETERS;
  return true;
}

bool IperfTest::SendAck() {
  octetVec iper_control_data;
  iper_control_data.resize(300);
  octet* ptr = iper_control_data.data();
  // header
  iperf_control_data* control_header = reinterpret_cast<iperf_control_data*>(ptr);
  control_header->test_id = Id();
  control_header->magic = bats_iperf_magic_code;
  control_header->action_code = 0x04;

  ctrl_chn_->SendData(iper_control_data.data(), iper_control_data.size());
  spdlog::info("[IperfTest] Test ack is sent.");
  // For RECEIVER
  if (state_ == TestState::TEST_NONE) {
    state_ = TestState::TEST_START;
    spdlog::info("[IperfTest] Test role {} is started", static_cast<int>(test_config_.role));
    return true;
  }

  if (state_ == TestState::TEST_START) {
    state_ = TestState::TEST_DONE;
    spdlog::info("[IperfTest] Test role {} is done", static_cast<int>(test_config_.role));
    return true;
  }
  spdlog::warn("[IperfTest] Test role {} unexpected state when sending ack: {}", static_cast<int>(test_config_.role),
               static_cast<int>(state_.load()));
  return true;
}

bool IperfTest::IsStreamReady() {
  std::scoped_lock lock(streams_mutex_);
  auto expected_num_streams = test_config_.num_streams;
  for (auto& stream : test_streams_) {
    if (!stream->IsConnected()) {
      return false;
    }
    expected_num_streams--;
  }
  return expected_num_streams == 0;
}

void IperfTest::PrintStreamSendSummary() {
  std::scoped_lock lock(streams_mutex_);
  for (auto& stream : test_streams_) {
    if (test_config_.role != TestRole::ROLE_SENDER) {
      continue;
    }
    stream->PrintSendSummary();
  }
}

bool IperfTest::IsStreamDone() {
  std::scoped_lock lock(streams_mutex_);
  if (test_streams_.empty()) {
    return true;
  }
  auto expected_num_streams = test_config_.num_streams;
  for (auto& stream : test_streams_) {
    if (stream->GetState() != StreamState::STREAM_DONE) {
      return false;
    }
    expected_num_streams--;
  }
  return expected_num_streams == 0;
}

void IperfTest::CheckStreamIntervalReceive() {
  if (test_config_.role != TestRole::ROLE_RECEIVER) {
    return;
  }
  if (passed_ticks_++ > (test_config_.time * test_config_.resolution)) {
    return;
  }
  if ((passed_ticks_ % static_cast<int>(std::round(test_config_.interval * test_config_.resolution))) != 0) {
    // not the time to do snapshot.
    return;
  }

  for (auto& stream : test_streams_) {
    if (stream->IsIntervalDataReceivedTimeout()) {
      stream->PrintLastInterval();
    }
  }
}

/// @brief Polling the ready state of the test in fixed intervals(1s)
/// @return
bool IperfTest::TestFinishState() {
  if (test_config_.role != TestRole::ROLE_SENDER) {
    // receiver role wait for sender to finish.
    CheckStreamIntervalReceive();
    return false;
  }

  if (state_ == TestState::TEST_DONE) {
    return true;
  }

  if (passed_ticks_++ > (test_config_.time * test_config_.resolution)) {
    return true;
  }

  if ((passed_ticks_ % static_cast<int>(std::round(test_config_.interval * test_config_.resolution))) != 0) {
    // not the time to do snapshot.
    return false;
  }

  int dead_streams = 0;
  // do snapshot for each stream.
  for (auto& stream : test_streams_) {
    if (stream->GetState() == StreamState::STREAM_DONE) {
      // stream is dead.
      dead_streams++;
      continue;
    }
    if (stream->SnapshotReport()) {
      stream->PrintLastInterval();
    }
  }

  if (dead_streams == test_streams_.size()) {
    // can't proceed the test, mark the test as done.
    spdlog::error("[IperfTest] Test streams were dead.");
    state_ = TestState::TEST_DONE;
    return true;
  }

  return false;
}
