/**
 * @file iperf_stream.h
 * @author Lei Peng (peng.lei@n-hop.com)
 * @brief
 * @version 1.0.0
 * @date 2025-05-30
 *
 * Copyright (c) 2025 The n-hop technologies Limited. All Rights Reserved.
 *
 */

#ifndef SRC_EXAMPLE_IPERF_BATS_IPERF_STREAM_H_
#define SRC_EXAMPLE_IPERF_BATS_IPERF_STREAM_H_
#include <atomic>
#include <cstdint>
#include <deque>
#include <fstream>
#include <memory>
#include <mutex>  // NOLINT
#include <string>
#include <thread>  // NOLINT
#include <vector>

#include "include/cpp/bats_connection.h"
#include "include/cpp/bats_iocontext.h"
#include "include/cpp/bats_protocol.h"
#include "src/example/iperf-bats/iperf_common.h"
#include "src/include/queue.h"
#include "src/util/flow_shaping.h"
#include "src/util/seq_recorder.h"

using BatsProtocolPtr = std::shared_ptr<BatsProtocol>;
using IOContextPtr = std::shared_ptr<IOContext>;

enum class StreamState {
  STREAM_NONE = 0,
  STREAM_START,  // sending is started
  STREAM_STOP,   // sending is stopped
  STREAM_DONE,   // test is done
};

enum class ConnectionState {
  CONN_NONE = 0,
  CONN_CONNECTING,    // connecting to remote
  CONN_CONNECTED,     // connected
  CONN_DISCONNECTED,  // disconnected
};

class IperfStream : public std::enable_shared_from_this<IperfStream> {
 public:
  constexpr static uint64_t iperf_stream_seq_reorder_threshold = 128;
  constexpr uint64_t get_time_reorder_threshold(uint32_t rtt) { return rtt / 4; }

  IperfStream(IOContext& io, std::basic_ofstream<char>& log_stream, TestConfig& config);
  IperfStream(IOContext& io, std::basic_ofstream<char>& log_stream, TestConfig& config, const IBatsConnPtr& conn);
  virtual ~IperfStream();

  IperfStream(const IperfStream&) = delete;
  IperfStream& operator=(const IperfStream&) = delete;
  IperfStream(IperfStream&& other) = default;
  IperfStream& operator=(IperfStream&& other) = default;

  bool operator<(const IperfStream& other) const { return Id() < other.Id(); }

  void PrintLastInterval();
  void PrintSendRecvSummary();
  void PrintSendSummary();
  StreamState GetState() const { return state_.load(); }
  octet* SerializeReport(octet* to_buffer);
  const octet* DeserializeReport(const octet* from_buffer);
  bool IsIntervalDataReceivedTimeout();
  bool SnapshotReport();
  void FinishReport();

  bool IsConnected() { return conn_state_ == ConnectionState::CONN_CONNECTED; }

  void Stop();
  void StartConnect();
  void StartSend();
  void StartRecv();
  int Id() const { return report_.stream_id; }
  uint64_t GetStreamIdentifier() { return stream_identifier_; }
  bool ConnectionCallback(const IBatsConnPtr& new_conn, const BatsConnEvent& event, const octet* data, int length,
                          void* user);
  bool IsFinReceived() const { return is_fin_received_.load(); }
  bool IsFinAcked() const { return is_fin_acked_.load(); }

 protected:
  void PrintReceiveSummary();
  octet* SerializeInterval(octet* to_buffer, const TestInterval& inter);
  TestInterval DeserializeInterval(const octet* from_buffer);
  bool UpdateReceived(const octet* data, int length);
  bool IsConnecting() const { return conn_state_ == ConnectionState::CONN_CONNECTING; }
  /// @brief call it when bats connection is ready.
  void SwitchDataBlockLength(int ideal_length);
  void SnapshotRxTxReport(TestReport& report, uint64_t current, uint64_t interval);
  void GenSendSummary();
  void GenReceiveSummary();
  void ResetSeqRecord();
  bool HasNewGrpId(uint8_t flag, uint64_t seq);
  std::stringstream PrintReceiveLastInterval();
  std::stringstream PrintSendLastInterval();
  void SendFIN();
  void OnReceivedFIN();
  void OnReceivedIntervalData(const octet* data, int length);
  bool ValidateReceivedData(const octet* data, int length);
  static constexpr int max_connect_retry = 3;

  IOContext& io_;
  std::basic_ofstream<char>& log_stream_;
  std::mutex snapshot_mutex_;
  TestConfig config_;
  std::thread sending_thread_;
  // read stream id from `iperf_test_header`
  uint64_t stream_identifier_ = 0;
  /// @brief Whether the summary from the peer is received.
  std::atomic<bool> is_sum_printed_ = false;
  std::atomic<bool> is_sum_received_ = false;
  std::atomic<bool> is_fin_received_ = false;
  std::atomic<bool> is_fin_acked_ = false;
  std::atomic<bool> is_writable_ = false;
  std::atomic<bool> has_err_in_send_ = false;
  std::atomic<bool> is_stopped_ = false;
  std::atomic<ConnectionState> conn_state_ = ConnectionState::CONN_NONE;
  std::atomic<StreamState> state_ = {StreamState::STREAM_NONE};
  BatsProtocolPtr connector_ = nullptr;
  int connect_retry_cnt_ = 0;

  TestReportOfStream report_;
  IBatsConnPtr data_conn_ = nullptr;
  octetVec iperf_data_;  // data a;
  std::atomic<int> ideal_length_ = 0;
  int last_grp_id_ = -1;
  uint64_t seq_of_last_changed_grp_id_ = 0;
  SeqRecorder2M recv_seq_recorder_;
  std::deque<int> diff_s_r_list_;  // diff of send/arrival time for the same packet.
  bool is_first_jitter_calculation_ = true;
  FlowShaper* flow_shaper_ = nullptr;
  std::unique_ptr<WaitStrategy<PredicateWaitStrategy>> writable_wait_ = nullptr;
};

using IperfStreamPtr = std::shared_ptr<IperfStream>;

#endif  // SRC_EXAMPLE_IPERF_BATS_IPERF_STREAM_H_
