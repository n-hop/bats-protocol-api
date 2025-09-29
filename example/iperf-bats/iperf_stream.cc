/**
 * @file iperf_stream.cc
 * @author Lei Peng (peng.lei@n-hop.com)
 * @brief
 * @version 1.0.0
 * @date 2025-05-30
 *
 * Copyright (c) 2025 The n-hop technologies Limited. All Rights Reserved.
 *
 */
#include "src/example/iperf-bats/iperf_stream.h"

#include <spdlog/cfg/env.h>  // support for loading levels from the environment variable
#include <spdlog/fmt/bin_to_hex.h>
#include <spdlog/fmt/ostr.h>  // support for user defined types
#include <spdlog/spdlog.h>

#include <climits>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "src/include/time.h"
#include "src/util/ring_arithmetic.h"

static inline std::stringstream PrintLastInterval(const TestInterval& last_interval, int stream_id, float interval,
                                                  int num, bool is_precise_time_enabled) {
  std::stringstream ss;
  if (is_precise_time_enabled) {
    auto now = std::chrono::system_clock::now();
    auto now_steady = std::chrono::steady_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    auto local_time = *std::localtime(&time_t_now);
    auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    fmt::print(ss, "[{:%H:%M:%S} {:03}] ", local_time, milliseconds.count());
    auto uptime = now_steady.time_since_epoch();
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(uptime).count();
    auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(uptime).count();
    double precise_uptime = seconds + (nanoseconds % 1'000'000'000) / 1e9;
    fmt::print(ss, " {:.9f} ", precise_uptime);
  }

  ss << "[ " << stream_id << "]  " << std::fixed << std::setprecision(2) << (num - 1) * interval << "-"
     << (num)*interval << " sec   " << last_interval.get_bytes_string() << "  " << last_interval.get_bitrate_string()
     << "  " << std::setprecision(3) << last_interval.jitter << " ms   0/" << last_interval.datagrams << " (0%)"
     << '\n';
  return ss;
  /// @loss_datagrams The loss rate in interval indicates the disorder at receiving side not actual packet loss.
}

static inline std::stringstream PrintSummary(const TestInterval& summary, const std::string& str, int stream_id) {
  std::stringstream ss;
  ss << "[ " << stream_id << "]  "
     << ".00-"
     << ".00  sec   " << summary.get_bytes_string() << "  " << summary.get_bitrate_string() << "  " << std::fixed
     << std::setprecision(3) << summary.jitter << " ms   " << summary.lost_datagrams << "/" << summary.datagrams << " ("
     << std::fixed << std::setprecision(1) << summary.loss * 100 << "%) " << str << '\n';
  return ss;
}

void IperfStream::SnapshotRxTxReport(TestReport& report, uint64_t current, uint64_t interval) {
  std::unique_lock<std::mutex> lock(snapshot_mutex_);
  auto& report_to_be_update = report;

  auto report_summary = report_to_be_update.summary;  // copy data.
  auto& last_test_snapshot = report_to_be_update.GetLastSnapshot();
  // save it to the last interval.
  TestInterval& new_interval = report_to_be_update.GetLastInterval();
  new_interval.bitrate = static_cast<double>(report_summary.bytes - last_test_snapshot.bytes) / (interval / 1000.0);
  new_interval.bitrate = std::round(new_interval.bitrate * 100) / 100.0;

  new_interval.bytes = report_summary.bytes - last_test_snapshot.bytes;
  new_interval.datagrams = report_summary.datagrams - last_test_snapshot.datagrams;
  new_interval.start_seq = last_test_snapshot.datagrams;

  last_test_snapshot.bytes = report_summary.bytes;
  last_test_snapshot.datagrams = report_summary.datagrams;
  last_test_snapshot.jitter = report_summary.jitter;

  report_to_be_update.last_snapshot_time = current;
  report_to_be_update.snapshot_cnt++;
}

// Test data layout:
// | iperf_test_header | last interval data |
struct iperf_test_header {
  uint64_t magic;
  uint64_t stream_identifier;
  uint64_t seq;
  uint64_t time;
#if __BYTE_ORDER == __LITTLE_ENDIAN
  uint8_t grp_id : 1;
  uint8_t last : 1;          // the last block.
  uint8_t ack : 1;           // the ack.
  uint8_t has_interval : 1;  // has interval data.
  uint8_t reserved : 4;
#elif __BYTE_ORDER == __BIG_ENDIAN
  uint8_t reserved : 4;
  uint8_t has_interval : 1;  // has interval data.
  uint8_t ack : 1;           // the ack.
  uint8_t last : 1;          // the last block.
  uint8_t grp_id : 1;
#endif
  uint16_t interval_id;
};

static const char iperf_char = 0x77;  // 01110111
static int g_stream_id = 1;
IperfStream::IperfStream(IOContext& io, std::basic_ofstream<char>& log_stream, TestConfig& config)
    : io_(io), log_stream_(log_stream), config_(config) {
  is_stopped_ = false;
  writable_wait_ = std::make_unique<PredicateWaitStrategy>();
  report_.stream_id = g_stream_id++;
  stream_identifier_ = get_time_in_ms<ClockType::MONOTONIC>() + report_.stream_id;

  iperf_data_.resize(150000);
  iperf_data_.assign(150000, iperf_char);
  {
    iperf_test_header* test_data = reinterpret_cast<iperf_test_header*>(iperf_data_.data());
    test_data->magic = bats_iperf_magic_code;
    test_data->stream_identifier = stream_identifier_;
  }

  BatsConfiguration bats_config;
  bats_config.SetTimeout(config_.interval * config_.time * 1000);
  bats_config.SetMode(static_cast<TransMode>(config_.protocol));

  if (config_.protocol == 0 || config_.protocol == 1) {
    bats_config.SetFrameType(FrameType::BATS_HEADER_V1);
  }

  // udp.quic
  if (config_.protocol == 11 || config_.protocol == 12) {
    bats_config.SetFrameType(FrameType::TRANSPARENT);
  }

  // tcp
  if (config_.protocol == 10) {
    bats_config.SetFrameType(FrameType::BATS_HEADER_MIN);
  }

  bats_config.SetCertFile(bats_default_cert_file);
  bats_config.SetKeyFile(bats_default_key_file);
  // to start new connection with `connector_`
  connector_ = std::make_shared<BatsProtocol>(io_, bats_config);
  report_.sender_report.interval = config_.interval;
  report_.receiver_report.interval = config_.interval;
  // init shaper if max_allowed_sending_rate is set
  if (config_.max_allowed_sending_rate > 0) {
    flow_shaper_ = new (std::nothrow) FlowShaper(0, config_.max_allowed_sending_rate);
    if (flow_shaper_ == nullptr) {
      spdlog::warn("[IperfStream] Test stream {} unable to init a flow shaper", this->Id());
    }
  }
}

IperfStream::IperfStream(IOContext& io, std::basic_ofstream<char>& log_stream, TestConfig& config,
                         const IBatsConnPtr& conn)
    : io_(io), log_stream_(log_stream), config_(config) {
  is_stopped_ = false;
  writable_wait_ = std::make_unique<PredicateWaitStrategy>();
  report_.stream_id = g_stream_id++;
  report_.sender_report.interval = config_.interval;
  report_.receiver_report.interval = config_.interval;
  // accepted connection from listener.
  data_conn_ = conn;
  conn_state_ = ConnectionState::CONN_CONNECTED;

  iperf_data_.resize(150000);
  iperf_data_.assign(150000, iperf_char);
  {
    iperf_test_header* test_data = reinterpret_cast<iperf_test_header*>(iperf_data_.data());
    test_data->magic = bats_iperf_magic_code;
    test_data->stream_identifier = stream_identifier_;
  }

  this->SwitchDataBlockLength(data_conn_->GetIdealBufferLength());

  // init shaper if max_allowed_sending_rate is set
  if (config_.max_allowed_sending_rate > 0) {
    flow_shaper_ = new (std::nothrow) FlowShaper(0, config_.max_allowed_sending_rate);
    if (flow_shaper_ == nullptr) {
      spdlog::warn("[IperfStream] Test stream {} unable to init a flow shaper", this->Id());
    }
  }
}

IperfStream::~IperfStream() {
  conn_state_ = ConnectionState::CONN_NONE;
  state_ = StreamState::STREAM_DONE;
  writable_wait_->BreakAllWait();

  if (sending_thread_.joinable()) {
    sending_thread_.join();
  }
  if (flow_shaper_ != nullptr) {
    delete flow_shaper_;
  }
  spdlog::info("[IperfStream] Test stream {} is destroyed", this->Id());
}

void IperfStream::SwitchDataBlockLength(int ideal_length) {
  // switch the using buffer.
  assert(ideal_length > 0);
  assert(ideal_length <= 150000);
  ideal_length_.store(ideal_length, std::memory_order_seq_cst);
  spdlog::info("[IperfStream] Test stream {} SwitchDataBlockLength with ideal_length {} ", this->Id(), ideal_length);
}

octet* IperfStream::SerializeReport(octet* to_buffer) {
  // always, client sends the report(sending/receiving) to server
  *reinterpret_cast<uint64_t*>(to_buffer) = stream_identifier_;
  to_buffer += sizeof(uint64_t);

  auto& sender_report = report_.sender_report;
  // transfer bytes
  *reinterpret_cast<uint64_t*>(to_buffer) = sender_report.summary.bytes;
  to_buffer += sizeof(uint64_t);

  *reinterpret_cast<uint64_t*>(to_buffer) = sender_report.summary.datagrams;
  to_buffer += sizeof(uint64_t);

  // bitrate bytes/sec; original value * 100
  *reinterpret_cast<uint64_t*>(to_buffer) = sender_report.summary.bitrate * 100;
  spdlog::info("[IperfStream] SerializeReport: stream {} bytes {} datagrams {} bitrate {}", report_.stream_id,
               sender_report.summary.bytes, sender_report.summary.datagrams, sender_report.summary.bitrate);

  to_buffer += sizeof(uint64_t);
  return to_buffer;
}

const octet* IperfStream::DeserializeReport(const octet* from_buffer) {
  // skip the `stream_identifier_`
  // server receives the results
  auto& sender_report = report_.sender_report;
  sender_report.summary.bytes = *reinterpret_cast<const uint64_t*>(from_buffer);
  from_buffer += sizeof(uint64_t);

  sender_report.summary.datagrams = *reinterpret_cast<const uint64_t*>(from_buffer);
  from_buffer += sizeof(uint64_t);

  sender_report.summary.bitrate = *reinterpret_cast<const uint64_t*>(from_buffer) / 100.0;
  from_buffer += sizeof(uint64_t);
  spdlog::info("[IperfStream] DeserializeReport: stream {} bytes {} datagrams {} bitrate {}", report_.stream_id,
               sender_report.summary.bytes, sender_report.summary.datagrams, sender_report.summary.bitrate);
  return from_buffer;
}

octet* IperfStream::SerializeInterval(octet* to_buffer, const TestInterval& inter) {
  *reinterpret_cast<uint64_t*>(to_buffer) = inter.bytes;
  to_buffer += sizeof(uint64_t);

  *reinterpret_cast<uint64_t*>(to_buffer) = inter.datagrams;
  to_buffer += sizeof(uint64_t);

  // start `seq` in this interval.
  *reinterpret_cast<uint64_t*>(to_buffer) = inter.start_seq;
  to_buffer += sizeof(uint64_t);

  // bitrate bytes/sec; original value * 100
  *reinterpret_cast<uint64_t*>(to_buffer) = inter.bitrate * 100;
  to_buffer += sizeof(uint64_t);

  return to_buffer;
}

TestInterval IperfStream::DeserializeInterval(const octet* from_buffer) {
  TestInterval inter;
  inter.bytes = *reinterpret_cast<const uint64_t*>(from_buffer);
  from_buffer += sizeof(uint64_t);

  inter.datagrams = *reinterpret_cast<const uint64_t*>(from_buffer);
  from_buffer += sizeof(uint64_t);

  // start `seq` in this interval.
  inter.start_seq = *reinterpret_cast<const uint64_t*>(from_buffer);
  from_buffer += sizeof(uint64_t);

  inter.bitrate = *reinterpret_cast<const uint64_t*>(from_buffer) / 100.0;
  return inter;
}
/// @brief Check whether the latest interval data from `sender` is timeout.
/// @return True if the interval print is timeout.
bool IperfStream::IsIntervalDataReceivedTimeout() {
  // OnReceivedIntervalData trigger interval print and do the SnapshotRxTxReport
  auto current = get_time_in_ms<ClockType::MONOTONIC>();
  auto last_snapshot_time = report_.receiver_report.last_snapshot_time;
  auto interval_base_ms = static_cast<uint64_t>(config_.interval * 1000);
  auto expected_time_point = last_snapshot_time + interval_base_ms;
  int max_allowed_deviation = 50;
  if (current < expected_time_point - max_allowed_deviation) {
    // not yet reached the next snapshot time.
    return false;
  }
  // take snapshot for receiver_report immediately.
  spdlog::trace("[IperfStream] {} take snapshot for receiver_report immediately current {} expected_time_point {}",
                current, current - last_snapshot_time, expected_time_point - last_snapshot_time);
  SnapshotRxTxReport(report_.receiver_report, current, (current - last_snapshot_time));
  return true;
}

bool IperfStream::SnapshotReport() {
  auto current = get_time_in_ms<ClockType::MONOTONIC>();
  auto escaped_time_ms =
      std::llabs(static_cast<int64_t>(current) - static_cast<int64_t>(report_.sender_report.last_snapshot_time));
  if (escaped_time_ms >= static_cast<int64_t>(config_.interval * 1000 * 0.95)) {
    // interval should be closer to "config_.interval * 1000" ms
    spdlog::trace("[IperfStream] Test stream {} SnapshotReport: interval {} ms is_writable {}", report_.stream_id,
                  escaped_time_ms, is_writable_.load());
    SnapshotRxTxReport(report_.sender_report, current, escaped_time_ms);
    return true;
  }
  // don't take snapshot for receiver_report here.
  return false;
}

void IperfStream::PrintLastInterval() {
  std::stringstream ss_interval;
  if (config_.role == TestRole::ROLE_SENDER) {
    ss_interval = this->PrintSendLastInterval();
  }

  if (config_.role == TestRole::ROLE_RECEIVER) {
    ss_interval = this->PrintReceiveLastInterval();
  }

  std::cout << ss_interval.str();
  spdlog::info("bats_iperf {}", ss_interval.str());
  if (log_stream_.is_open()) {
    log_stream_ << ss_interval.str();
    log_stream_ << std::flush;
  }
}

std::stringstream IperfStream::PrintReceiveLastInterval() {
  std::unique_lock<std::mutex> lock(snapshot_mutex_);
  if (report_.receiver_report.snapshot_cnt < 1) {
    spdlog::info("[IperfStream] Test stream {} no test results yet", report_.stream_id);
    return std::stringstream();
  }
  auto& interval = report_.receiver_report.interval;
  auto& last_test_interval = report_.receiver_report.GetLastInterval();
  return ::PrintLastInterval(last_test_interval, report_.stream_id, interval, report_.receiver_report.snapshot_cnt,
                             config_.is_precise_time_enabled);
}

std::stringstream IperfStream::PrintSendLastInterval() {
  std::unique_lock<std::mutex> lock(snapshot_mutex_);
  if (report_.sender_report.snapshot_cnt < 1) {
    spdlog::info("[IperfStream] Test stream {} no test results yet", report_.stream_id);
    return std::stringstream();
  }
  auto& interval = report_.sender_report.interval;
  auto& last_test_interval = report_.sender_report.GetLastInterval();
  return ::PrintLastInterval(last_test_interval, report_.stream_id, interval, report_.sender_report.snapshot_cnt,
                             config_.is_precise_time_enabled);
}

void IperfStream::PrintReceiveSummary() {
  auto ss = PrintSummary(report_.receiver_report.summary, "receiver", report_.stream_id);
  std::cout << ss.str();
  if (log_stream_.is_open()) {
    log_stream_ << ss.str();
    log_stream_ << std::flush;
  }
}

void IperfStream::PrintSendSummary() {
  auto ss = PrintSummary(report_.sender_report.summary, "sender", report_.stream_id);
  std::cout << ss.str();
  if (log_stream_.is_open()) {
    log_stream_ << ss.str();
    log_stream_ << std::flush;
  }
}

void IperfStream::GenSendSummary() {
  TestInterval& summary = report_.sender_report.summary;
  summary.bitrate = summary.bytes / static_cast<double>(report_.sender_report.snapshot_cnt * config_.interval);
  spdlog::info("[IperfStream] Test stream {} GenSendSummary send rate {}. cnt {}", this->Id(), summary.bitrate,
               report_.sender_report.snapshot_cnt);
}

void IperfStream::GenReceiveSummary() {
  TestInterval& summary = report_.receiver_report.summary;
  summary.bitrate = summary.bytes / static_cast<double>(report_.receiver_report.snapshot_cnt * config_.interval);
  // ROLE_RECEIVER receives the `sender_report` from peer with SerializeReport()
  summary.lost_datagrams = report_.sender_report.summary.datagrams - report_.receiver_report.summary.datagrams;
  if (summary.lost_datagrams < 0) {
    // stop sending control is imprecise, sender may send additional one or two packets than the number in
    // report;
    summary.lost_datagrams = 0;
  }

  // `datagrams` in `sender_report` is the total datagrams sent by sender.
  report_.receiver_report.summary.datagrams = report_.sender_report.summary.datagrams;
  summary.loss = static_cast<double>(summary.lost_datagrams) / report_.sender_report.summary.datagrams;
  spdlog::info("[IperfStream] Test stream {} GenReceiveSummary send rate {}. total_intervals {} interval {}",
               this->Id(), summary.bitrate, report_.receiver_report.snapshot_cnt, config_.interval);
}

bool IperfStream::ValidateReceivedData(const octet* data, int length) {
  const iperf_test_header* test_header = reinterpret_cast<const iperf_test_header*>(data);
  if (test_header->magic != bats_iperf_magic_code) {
    // indicate corruption in data
    return false;
  }
  // bytes, datagrams, start_seq, bitrate
  constexpr int IntervalDataSize = sizeof(uint64_t) * 4;
  if (length <= static_cast<int>(sizeof(iperf_test_header) + IntervalDataSize)) {
    // only header
    return true;
  }

  // check `iperf_char` (skip the interval data after header)
  if (data[sizeof(iperf_test_header) + IntervalDataSize] != iperf_char) {
    spdlog::warn(
        "[IperfStream] Test stream {} received data corruption at the beginning of data, length {}; char diff {:x} vs "
        "{:x}",
        this->Id(), length, data[sizeof(iperf_test_header)], iperf_char);
    return false;
  }

  if (data[length - 1] != iperf_char) {
    spdlog::error(
        "[IperfStream] Test stream {} received data corruption at the end of data, length {}; char diff {:x} vs {:x}",
        this->Id(), length, data[length - 1], iperf_char);
    return false;
  }
  return true;
}

bool IperfStream::UpdateReceived(const octet* data, int length) {
  if (unlikely(data == nullptr || length <= 0)) {
    spdlog::info("[IperfStream] Test stream {} received null data.", this->Id());
    return false;
  }

  if (report_.receiver_report.summary.bytes == 0) {
    report_.receiver_report.last_snapshot_time = get_time_in_ms<ClockType::MONOTONIC>();
  }
  report_.receiver_report.summary.bytes += length;
  report_.receiver_report.summary.datagrams += 1;

  const iperf_test_header* test_header = reinterpret_cast<const iperf_test_header*>(data);
  auto seq = test_header->seq;
  if (!ValidateReceivedData(data, length)) {
    spdlog::error("[IperfStream] Test stream {} Not iperf test data, seq {}, magic code {}, bats magic code {}",
                  this->Id(), seq, test_header->magic, bats_iperf_magic_code);
    // indicate corruption in data
    spdlog::error("[IperfStream] received header: {:n}", spdlog::to_hex(data, data + sizeof(iperf_test_header) + 8));
    exit(EXIT_FAILURE);
  }

  recv_seq_recorder_.Record(seq);

  if (stream_identifier_ == 0) {
    stream_identifier_ = test_header->stream_identifier;
    // check stream ISN for reliable protocol
    if (seq != 0 && (config_.protocol == 1 || config_.protocol == 2 || config_.protocol == 10)) {
      spdlog::error("[IperfStream] Test stream {} receives stream_identifier {}, but seq {} != 0", this->Id(),
                    stream_identifier_, seq);
      // indicate disorder in data receiving
      exit(EXIT_FAILURE);
    }
  }

  if (test_header->last == 0x01 && test_header->ack == 0x01) {
    // acked the last data.
    is_fin_acked_ = true;
    state_ = StreamState::STREAM_DONE;
    spdlog::info("[IperfStream] Test stream {} is finished (fin-ack is received).", this->Id());
    this->PrintSendSummary();
  }

  if (test_header->last == 0x01 && test_header->ack == 0x00) {
    OnReceivedFIN();
  }

  if (HasNewGrpId(test_header->grp_id, seq) && test_header->has_interval == 0x01) {
    // received the new interval and each interval has been marked in "ABABA..."
    OnReceivedIntervalData(data + sizeof(iperf_test_header), length - sizeof(iperf_test_header));
  }
  /// @note: Jitter estimation follows the method in https://datatracker.ietf.org/doc/html/rfc1889#section-6.3.1 and
  /// [Type-P-One-way-ipdv-jitter]: https://datatracker.ietf.org/doc/html/rfc3393#section-4.5
  /// Formula: D(i,j)=(Rj-Ri)-(Sj-Si)=(Rj-Sj)-(Ri-Si)
  auto cur_arrival_time = get_time_in_ms();
  int curr_diff_s_r = static_cast<int>(cur_arrival_time - test_header->time);
  if (diff_s_r_list_.empty()) {
    diff_s_r_list_.push_back(curr_diff_s_r);
  } else {
    int last_diff_s_r = diff_s_r_list_.back();
    int new_diff_i_j = std::abs(curr_diff_s_r - last_diff_s_r);
    if (is_first_jitter_calculation_) {
      is_first_jitter_calculation_ = false;
      report_.receiver_report.summary.jitter = new_diff_i_j;
    } else {
      report_.receiver_report.summary.jitter = (15 * report_.receiver_report.summary.jitter + new_diff_i_j) / 16.0;
    }
    diff_s_r_list_.pop_back();
    diff_s_r_list_.push_back(curr_diff_s_r);
  }
  return true;
}

bool IperfStream::ConnectionCallback(const IBatsConnPtr& new_conn, const BatsConnEvent& event, const octet* data,
                                     int length, void* user) {
  switch (event) {
    case BatsConnEvent::BATS_CONNECTION_ESTABLISHED:
      if (config_.role == TestRole::ROLE_SENDER) {
        spdlog::info("[IperfStream] Test stream {} connection established!", this->Id());
        conn_state_ = ConnectionState::CONN_CONNECTED;
        data_conn_ = new_conn;

        this->SwitchDataBlockLength(data_conn_->GetIdealBufferLength());
        // start snapshot immediately when data connection is established.
        std::unique_lock<std::mutex> lock(snapshot_mutex_);
        auto& sender_report = report_.sender_report;
        if (sender_report.summary.bytes == 0 && sender_report.last_snapshot_time == 0) {
          sender_report.last_snapshot_time = get_time_in_ms<ClockType::MONOTONIC>();
        }
      }
      break;
    case BatsConnEvent::BATS_CONNECTION_CLOSED:
    case BatsConnEvent::BATS_CONNECTION_SHUTDOWN_BY_PEER:
      spdlog::info("[IperfStream] Test stream {} is closed!", this->Id());
      conn_state_ = ConnectionState::CONN_DISCONNECTED;
      break;
    case BatsConnEvent::BATS_CONNECTION_BUFFER_FULL:
      is_writable_.store(true, std::memory_order_seq_cst);
      break;
    case BatsConnEvent::BATS_CONNECTION_WRITABLE:
      // no instructions reorder
      is_writable_.store(true, std::memory_order_seq_cst);
      writable_wait_->NotifyOne();
      break;
    case BatsConnEvent::BATS_CONNECTION_SEND_DATA_ERROR:
      has_err_in_send_.store(true, std::memory_order_seq_cst);
      spdlog::warn("[IperfStream] Test stream {} send data error!", this->Id());
      break;
    case BatsConnEvent::BATS_CONNECTION_IDEAL_BUFFER_LENGTH:
      // the ideal length may change due to network condition change.
      spdlog::info("[IperfStream] Test stream {} ideal buf length update to {}", this->Id(), length);
      this->SwitchDataBlockLength(length);
      break;
    case BatsConnEvent::BATS_CONNECTION_DATA_RECEIVED:
      // to receive any acks.
      // @note: return true if data is copied or processed.
      return this->UpdateReceived(data, length);
  }
  return true;
}

void IperfStream::Start() {
  if (config_.role == TestRole::ROLE_RECEIVER) {
    using namespace std::placeholders;  // NOLINT
    data_conn_->SetConnectionCallback(
        std::bind(&IperfStream::ConnectionCallback, shared_from_this(), _1, _2, _3, _4, _5), nullptr);
    return;
  }
  assert(connector_ != nullptr);

  state_ = StreamState::STREAM_START;
  sending_thread_ = std::thread([this]() {
    int retry_cnt = 0;
    while (IsConnected() == false && state_ == StreamState::STREAM_START) {
      using namespace std::placeholders;  // NOLINT
      conn_state_ = ConnectionState::CONN_CONNECTING;
      connector_->StartConnect(config_.peer_host, config_.port,
                               std::bind(&IperfStream::ConnectionCallback, shared_from_this(), _1, _2, _3, _4, _5),
                               nullptr);
      spdlog::info("[IperfStream] Test stream {} connecting to {}:{}", this->Id(), config_.peer_host, config_.port);
      int wait_cnt = 0;
      while (IsConnecting() && wait_cnt < 5000) {  // max wait 10s
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        wait_cnt++;
      }
      if (IsConnected() == false) {
        spdlog::info("[IperfStream] Test stream {} connection failed, retrying...", this->Id());
        retry_cnt++;
      }
      if (retry_cnt > 3) {
        spdlog::error("[IperfStream] Test stream {} connection failed after {} retries, giving up.", this->Id(),
                      retry_cnt);
        break;
      }
    }

    if (IsConnected() == false) {
      spdlog::info("[IperfStream] Failed to connect to the server, test failed.", this->Id());
      this->Stop();
      return;
    }

    spdlog::info("[IperfStream] Test stream {} sending thread is started.", this->Id());
    bool last_grp_id = false;
    while (IsConnected() && state_ == StreamState::STREAM_START) {
      if (has_err_in_send_) {
        has_err_in_send_ = false;
        state_ = StreamState::STREAM_DONE;
        spdlog::error("[IperfStream] Test stream {} has error in sending, force the test to stop.", this->Id());
        break;
      }

      while (is_writable_.load(std::memory_order_seq_cst) == false) {
        if (state_ != StreamState::STREAM_START) {
          break;
        }
        if (has_err_in_send_.load(std::memory_order_seq_cst) == true) {
          break;
        }
        if (writable_wait_->EmptyWait()) {
          continue;
        }
      }

      auto cur_time = get_time_in_ms();  // timestamp for peer
      if (flow_shaper_ &&
          flow_shaper_->is_bandwidth_limit_hit<FlowDirection::EGRESS>(get_time_in_ms<ClockType::MONOTONIC>())) {
        // don't send data
        std::this_thread::yield();
        continue;
      }

      // after init is done, choose the correct data buffer to use.
      auto current_data_length = ideal_length_.load(std::memory_order_seq_cst);
      const octet* data_ptr = iperf_data_.data();
      octet* ptr = const_cast<octet*>(data_ptr);

      iperf_test_header* test_header = reinterpret_cast<iperf_test_header*>(ptr);

      int cur_snapshot_cnt = 0;
      auto& sender_report = report_.sender_report;
      {
        // wait for snapshot to finish updating `snapshot_cnt`.
        std::unique_lock<std::mutex> lock(snapshot_mutex_);
        cur_snapshot_cnt = sender_report.snapshot_cnt;

        test_header->seq = sender_report.summary.datagrams;
        test_header->time = cur_time;
        // grp_id:  color A/B, uses idea from https://www.rfc-editor.org/rfc/rfc8321
        test_header->grp_id = (cur_snapshot_cnt % 2);
        test_header->last = 0;
        test_header->ack = 0;

        if (cur_snapshot_cnt <= 0) {
          // the first grp has no interval data.
          test_header->has_interval = 0;
        }

        if (cur_snapshot_cnt > 0 && last_grp_id != test_header->grp_id) {
          last_grp_id = test_header->grp_id;
          test_header->has_interval = 1;
          ptr += sizeof(iperf_test_header);
          // send last interval data to receiver.
          // Init the data body once and use it for multiple sends.
          TestInterval& last_test_interval = sender_report.GetLastInterval();
          ptr = SerializeInterval(ptr, last_test_interval);
          spdlog::trace(
              "[IperfStream] Test stream sending interval data ============== last_test_interval start seq {} {}",
              last_test_interval.start_seq, ptr - data_ptr);
        }
      }

      // Wait until writable or error in sending.
      bool send_ok = data_conn_->SendData(data_ptr, current_data_length);
      while (send_ok == false) {
        if (state_ != StreamState::STREAM_START) {
          break;
        }
        if (has_err_in_send_.load(std::memory_order_seq_cst) == true) {
          break;
        }
        if (is_writable_.load(std::memory_order_seq_cst) == true) {
          // retry after timeout when it's writable.
          send_ok = data_conn_->SendData(data_ptr, current_data_length);
        } else {
          // wait only if not writable.
          writable_wait_->EmptyWait();
        }
      }

      sender_report.summary.bytes += current_data_length;
      sender_report.summary.datagrams += 1;
      if (flow_shaper_ != nullptr) {
        flow_shaper_->update_bytes_counter<FlowDirection::EGRESS>(current_data_length);
      }
    }  // while (IsConnected() && state_ == StreamState::STREAM_START) {

    SendFIN();
    spdlog::info("[IperfStream] Test stream {} sending thread is finished.", this->Id());
  });
}

void IperfStream::SendFIN() {
  // do this when target protocol is reliable.
  if (IsReliableProtocol(config_.protocol)) {
    iperf_test_header* test_data = reinterpret_cast<iperf_test_header*>(iperf_data_.data());
    test_data->seq = report_.sender_report.summary.datagrams;
    test_data->grp_id = (report_.sender_report.snapshot_cnt % 2);
    test_data->time = get_time_in_ms();  // timestamp for peer
    test_data->last = 1;
    test_data->ack = 0;
    test_data->has_interval = 0;
    while (IsConnected() == true &&
           data_conn_->SendData(reinterpret_cast<const octet*>(iperf_data_.data()), ideal_length_) == false) {
      // keep sending.
      // not a successful sending due to many possible reasons.
      if (state_ == StreamState::STREAM_DONE) {
        spdlog::info("[IperfStream] Test stream {} is terminated, no need to send FIN.", this->Id());
        return;
      }
    }
    report_.sender_report.summary.bytes += ideal_length_;
    report_.sender_report.summary.datagrams += 1;
    spdlog::info("[IperfStream] Test stream {} FIN is sent.", this->Id());
  } else {
    spdlog::info("[IperfStream] Test stream {} is finished.", this->Id());
    this->PrintSendSummary();
  }
}

void IperfStream::OnReceivedFIN() {
  iperf_test_header* test_data = reinterpret_cast<iperf_test_header*>(iperf_data_.data());
  test_data->seq = report_.sender_report.summary.datagrams;
  test_data->last = 1;
  test_data->ack = 1;
  test_data->has_interval = 0;

  spdlog::info("[IperfStream] Test stream {} FIN is received", this->Id());
  while (IsConnected() == true &&
         data_conn_->SendData(reinterpret_cast<const octet*>(iperf_data_.data()), ideal_length_) == false) {
    // keep sending.
    // not a successful sending due to many possible reasons.
    if (state_ == StreamState::STREAM_DONE) {
      spdlog::info("[IperfStream] Test stream {} is terminated, no need to send FIN-ACK.", this->Id());
      return;
    }
  }
  spdlog::info("[IperfStream] Test stream {} FIN-ACK is sent.", this->Id());
  report_.sender_report.summary.bytes += ideal_length_;
  report_.sender_report.summary.datagrams += 1;
  is_fin_received_ = true;
  state_ = StreamState::STREAM_DONE;
}

void IperfStream::OnReceivedIntervalData(const octet* data, int length) {
  if (config_.role == TestRole::ROLE_RECEIVER) {
    /// @Note: The interval print of the receiver is driven by the incoming data from sender. It was trigger by the
    /// first packet of the next interval with the max delay of `jitter`ms compared to the timing interval print.
    /// ========>   AAAAAAA .. BBBB...
    ///                   |____|
    ///                   original time gap + jitter(from network path)
    auto current = get_time_in_ms<ClockType::MONOTONIC>();
    // For receiver, do snapshot and print results.
    auto interval =
        std::llabs(static_cast<int64_t>(current) - static_cast<int64_t>(report_.receiver_report.last_snapshot_time));
    auto received_interval = DeserializeInterval(data);
    spdlog::trace("[IperfStream] Test stream {} received interval data, start seq: {}, rate: {} datagrams: {}",
                  this->Id(), received_interval.start_seq, received_interval.bitrate, received_interval.datagrams);
    seq_of_last_changed_grp_id_ = received_interval.start_seq + received_interval.datagrams;
    spdlog::trace("[IperfStream] seq_of_last_changed_grp_id_ {}", seq_of_last_changed_grp_id_);
    auto actual_received =
        recv_seq_recorder_.RecordedNumInRange(received_interval.start_seq, seq_of_last_changed_grp_id_);
    assert(actual_received <= received_interval.datagrams);
    ResetSeqRecord();
    if (interval < static_cast<int64_t>(config_.interval * 1000 * 0.9)) {
      // just update `seq_of_last_changed_grp_id_` and ResetSeqRecord
      diff_s_r_list_.clear();
      is_first_jitter_calculation_ = true;
      return;
    }

    // `SnapshotRxTxReport` will update the `last_snapshot_time`
    SnapshotRxTxReport(report_.receiver_report, current, interval);
    diff_s_r_list_.clear();
    is_first_jitter_calculation_ = true;

    // std::unique_lock<std::mutex> lock(snapshot_mutex_);
    auto total_intervals = report_.receiver_report.snapshot_cnt;
    auto& last_test_interval = report_.receiver_report.GetLastInterval();
    last_test_interval.start_seq = received_interval.start_seq;
    last_test_interval.lost_datagrams = received_interval.datagrams - actual_received;
    last_test_interval.loss = (static_cast<double>(last_test_interval.lost_datagrams) / received_interval.datagrams);

    std::stringstream ss_interval;
    if (config_.is_precise_time_enabled) {
      auto now = std::chrono::system_clock::now();
      auto now_steady = std::chrono::steady_clock::now();
      auto time_t_now = std::chrono::system_clock::to_time_t(now);
      auto local_time = *std::localtime(&time_t_now);
      auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
      fmt::print(ss_interval, "[{:%H:%M:%S} {:03}] ", local_time, milliseconds.count());
      auto uptime = now_steady.time_since_epoch();
      auto seconds = std::chrono::duration_cast<std::chrono::seconds>(uptime).count();
      auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(uptime).count();
      double precise_uptime = seconds + (nanoseconds % 1'000'000'000) / 1e9;
      fmt::print(ss_interval, " {:.9f} ", precise_uptime);
    }

    ss_interval << "[ " << report_.stream_id << "]  " << std::fixed << std::setprecision(2)
                << (total_intervals - 1) * config_.interval << "-" << (total_intervals)*config_.interval << " sec   "
                << last_test_interval.get_bytes_string() << "  " << last_test_interval.get_bitrate_string()
                << std::fixed << std::setprecision(3) << "  " << last_test_interval.jitter << " ms   "
                << (last_test_interval.lost_datagrams) << "/" << last_test_interval.datagrams << " (" << std::fixed
                << std::setprecision(1) << last_test_interval.loss * 100 << "%)" << '\n';
    std::cout << ss_interval.str();
    if (log_stream_.is_open()) {
      log_stream_ << ss_interval.str();
      log_stream_ << std::flush;
    }
  } else {
    spdlog::warn("[IperfStream] Test stream {} received interval data but the role is not receiver {}", this->Id(),
                 static_cast<int>(config_.role));
  }
}

void IperfStream::ResetSeqRecord() {
  spdlog::trace("[IperfStream] ResetSeqRecord range [{}, {}]", recv_seq_recorder_.MinSeqRecorded(),
                recv_seq_recorder_.MaxSeqRecorded());
  std::vector<uint64_t> extra;
  extra.reserve(iperf_stream_seq_reorder_threshold);
  for (uint64_t i = seq_of_last_changed_grp_id_; i <= recv_seq_recorder_.MaxSeqRecorded(); i++) {
    if (recv_seq_recorder_.Has(i)) {
      extra.push_back(i);
    }
  }
  recv_seq_recorder_.Reset();
  for (const auto& i : extra) {
    recv_seq_recorder_.Record(i);
  }
}

bool IperfStream::HasNewGrpId(uint8_t flag, uint64_t seq) {
  if (last_grp_id_ == -1) {
    last_grp_id_ = flag;
    spdlog::trace("[IperfStream] HasNewGrpId: first grp_id {} seq {}", static_cast<int>(flag), seq);
    return true;
  }
  // against the packet reordering.
  if (last_grp_id_ != flag &&
      is_less_than_in_half_ring(seq_of_last_changed_grp_id_ + iperf_stream_seq_reorder_threshold, seq)) {
    last_grp_id_ = flag;
    spdlog::trace("[IperfStream] HasNewGrpId: grp_id {} seq {} seq_of_last_changed_grp_id_ {}", static_cast<int>(flag),
                  seq, seq_of_last_changed_grp_id_);
    return true;
  }
  return false;
}
