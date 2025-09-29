/**
 * @file iperf_file_stream.cc
 * @author Peng LEI (peng.lei@n-hop.com)
 * @brief
 * @version 1.0.0
 * @date 2025-09-08
 *
 * Copyright (c) 2025 The n-hop technologies Limited. All Rights Reserved.
 *
 */
#include "src/example/iperf-bats/iperf_file_transfer.h"

#include <spdlog/cfg/env.h>   // support for loading levels from the environment variable
#include <spdlog/fmt/ostr.h>  // support for user defined types
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#if __has_include(<filesystem>)
#include <filesystem>  // NOLINT(build/c++17)
namespace std_fs = std::filesystem;
#elif __has_include(<experimental/filesystem>)
#include <experimental/filesystem>
namespace std_fs = std::experimental::filesystem;
#else
error "Missing the <filesystem> header."
#endif

static const uint64_t bats_iperf_file_code = 0xa1b2ffff626174;
static constexpr uint32_t max_hash_length = 64;
static constexpr uint32_t max_name_length = 256;  // linux/windows limits on file name length.
static constexpr uint32_t max_extension_length = 16;

enum class CompressAlgo : uint8_t {
  CMA_NONE,  // no compression
  CMA_DENSITY,
  CMA_DEFLATE,  // DEFLATE (gzip)
  CMA_LZ4,      // extremely fast, lower compression ratio; used for real-time, logs, databases.
  CMA_LZMA,     // LZMA / xz
  CMA_LZF,      // LZF / Snappy — very fast, moderate ratio; used in big-data systems (Snappy in Hadoop/Parquet).
  CMA_MAX
};

/* Stream layout
   | file block |
       /   \
   | file block header | file block data  |
                            /   \
                    | (File meta data) File data0 | ... |File data1 |
*/
struct file_bk_header {
#if __BYTE_ORDER == __LITTLE_ENDIAN
  uint8_t syn : 1;       // is the first block
  uint8_t fin : 1;       // is the last block
  uint8_t has_sum : 1;   // has valid checksum
  uint8_t has_comp : 1;  // compress flags.
  // is_compression_enabled
  uint8_t file_seq : 4;  // file seq; being used to control progress bar at receiver side.
#elif __BYTE_ORDER == __BIG_ENDIAN
  uint8_t file_seq : 4;  // file seq
  uint8_t has_comp : 1;  // compress flags.
  uint8_t has_sum : 1;   // has valid checksum
  uint8_t fin : 1;       // is the last block
  uint8_t syn : 1;       // is the first block
#else
#error \
    "Unsupported byte order: Either __LITTLE_ENDIAN or " \
                   "__BIG_ENDIAN must be defined"
#endif
} __attribute__((__packed__));

struct file_meta {
  uint64_t magic;
  uint64_t stream_identifier;
#if __BYTE_ORDER == __LITTLE_ENDIAN
  uint64_t compression : 3;   // compression algo.
  uint64_t reserved : 5;      // reserved field.
  uint64_t file_length : 56;  // > PB.
#elif __BYTE_ORDER == __BIG_ENDIAN
  uint64_t file_length : 56;  // > PB.
  uint64_t reserved : 5;      // reserved field.
  uint64_t compression : 3;   // compression algo.
#else
#error \
    "Unsupported byte order: Either __LITTLE_ENDIAN or " \
                   "__BIG_ENDIAN must be defined"
#endif
  uint8_t sha256sum[max_hash_length];
  uint8_t file_name[max_name_length];
  uint8_t file_extension[max_extension_length];
} __attribute__((__packed__));

IperfFileTransfer::IperfFileTransfer(IOContext& io, std::basic_ofstream<char>& log_stream, uint64_t id,
                                     const TestConfig& config)
    : IIperfTest(io, log_stream, id, config), pending_files_("fq", 100) {
  // receiver side.
  decompressed_buf.resize(max_decompressed_buf_sz);
}

IperfFileTransfer::IperfFileTransfer(IOContext& io, std::basic_ofstream<char>& log_stream, const TestConfig& config)
    : IIperfTest(io, log_stream, config), pending_files_("fq", 100) {
  // for senders.
  spdlog::info("[IperfFileTransfer] Test is created with id: {}", test_id_);
  std::unordered_set<std::string> unique_files;
  // init pending files and filter out the duplicated files.
  test_config_.total_bytes_in_files = 0;
  int id = 0;
  for (const auto& file : test_config_.files) {
    // Only process the file if we haven't seen it before
    if (unique_files.insert(file).second) {
      std::error_code ec;
      const std::uintmax_t size = std_fs::file_size(file, ec);
      if (!ec) {
        test_config_.total_bytes_in_files += size;
        pending_files_.Enqueue({.bar_id = id, .file_sz = static_cast<int64_t>(size), .path = file});
        spdlog::info("[IperfFileTransfer] Added unique file: {} size {} bytes", file, size);
      } else {
        spdlog::error("[IperfFileTransfer] Unable to read file {}, error code {} ", file, ec.value());
      }
    } else {
      spdlog::warn("[IperfFileTransfer] Skipping duplicate file: {}", file);
    }
    id++;
  }
}

IperfFileTransfer::~IperfFileTransfer() {}

bool IperfFileTransfer::Start() {
  // 0. check existence of bats_default_key_file
  if (!std_fs::exists(bats_default_key_file)) {
    spdlog::error("[IperfFileTransfer] BATS default key file {} does not exist.", bats_default_key_file);
    return false;
  }
  if (!std_fs::exists(bats_default_cert_file)) {
    spdlog::error("[IperfFileTransfer] BATS default cert file {} does not exist.", bats_default_cert_file);
    return false;
  }

  // 1. start control channel.
  BatsConfiguration bats_config;
  // default to TCP
  bats_config.SetCertFile(bats_default_cert_file);
  bats_config.SetKeyFile(bats_default_key_file);
  bats_config.SetMode(static_cast<TransMode>(10));

  // use the different port for control channel.
  if (data_chn_connector_ == nullptr) {
    data_chn_connector_ = std::make_shared<BatsProtocol>(io_, bats_config);
  }
  // no user context for control channel.
  data_chn_connector_->StartConnect(
      test_config_.peer_host, test_config_.port - 1,
      std::bind(&IperfFileTransfer::ConnectionCallback, shared_from_this(), std::placeholders::_1,
                std::placeholders::_2, std::placeholders::_3, std::placeholders::_4, std::placeholders::_5),
      nullptr);
  return true;
}

bool IperfFileTransfer::Stop() {
  if (is_stopped_.exchange(true)) {
    return false;
  }
  state_ = TestState::TEST_DONE;
  std::scoped_lock lock(streams_mutex_);
  for (auto& stream : streams_) {
    stream->writable_wait->BreakAllWait();
    if (stream->thread.joinable()) {
      stream->thread.join();
    }
    stream->stream = nullptr;
    stream->is_connected = false;
    stream->is_writable = false;
    stream->is_done = true;
  }
  std::vector<FileStreamPtr>().swap(streams_);
  return false;
}

void IperfFileTransfer::HandleControlMessage(const IBatsConnPtr& ctrl_conn, const octet* data, int length) {
  const struct iperf_control_data* control_header = reinterpret_cast<const struct iperf_control_data*>(data);
  assert(control_header->test_id == test_id_);
  assert(control_header->action_code == 0x01 || control_header->action_code == 0x04);
  switch (control_header->action_code) {
    case 0x04:
      if (control_header->param0 == 0) {
        spdlog::info("[IperfFileTransfer] File transfer ack(ok) is received.");
        this->OnRecvResponse();
      } else {
        spdlog::error("[IperfFileTransfer] File transfer ack(denied) is received. test will be stopped.");
        this->CloseCtrlChannel();
        this->Stop();
      }
      break;
    case 0x01:
      // for receiver role,
      ctrl_chn_ = ctrl_conn;
      HandleFileTransferRequest(control_header);
      break;
    default:
      break;
  }
}

void IperfFileTransfer::OnSentFinished() {}
bool IperfFileTransfer::IsStreamReady() {
  std::scoped_lock lock(streams_mutex_);
  if (state_ == TestState::TEST_DONE && streams_.empty()) {
    // false ready
    return true;
  }
  auto expected_num_streams = test_config_.num_streams;
  for (auto& stream : streams_) {
    if (!stream->is_connected) {
      return false;
    }
    expected_num_streams--;
  }
  return expected_num_streams == 0;
}

bool IperfFileTransfer::IsStreamDone() {
  if (test_config_.role == TestRole::ROLE_FILE_RECEIVER && test_config_.num_files <= 0 &&
      state_ == TestState::TEST_DONE) {
    return true;
  }

  std::scoped_lock lock(streams_mutex_);
  if (state_ == TestState::TEST_DONE && streams_.empty()) {
    return true;
  }

  auto expected_num_streams = test_config_.num_streams;
  for (auto& stream : streams_) {
    if (stream->has_err_in_send == false && test_config_.role == TestRole::ROLE_FILE_SENDER &&
        (stream->is_done == false || stream->all_acked == false)) {
      return false;
    }
    // has error or finished.
    expected_num_streams--;
  }

  if (expected_num_streams == 0) {
    state_ = TestState::TEST_DONE;
    return true;
  }

  return false;
}

bool IperfFileTransfer::TestFinishState() {
  // this will make the sender exits directly, so wait for the last ACK from server and exit then.
  // sender should ensure all the sent data have been acked.
  return IsStreamDone();
}

void IperfFileTransfer::PrintStreamSendSummary() {}
bool IperfFileTransfer::ConnectionCallback(const IBatsConnPtr& data_conn, const BatsConnEvent& event, const octet* data,
                                           int length, void* user) {
  bool is_control_channel = (user == nullptr);
  switch (event) {
    case BatsConnEvent::BATS_CONNECTION_TIMEOUT:
      if (!is_control_channel) {
        int stream_id = *(reinterpret_cast<int*>(user));
        if (streams_.at(stream_id)->retry_cnt < 3) {
          data_chn_connector_->StartConnect(
              test_config_.peer_host, test_config_.port,
              std::bind(&IperfFileTransfer::ConnectionCallback, shared_from_this(), std::placeholders::_1,
                        std::placeholders::_2, std::placeholders::_3, std::placeholders::_4, std::placeholders::_5),
              &(streams_.at(stream_id)->id));
          streams_.at(stream_id)->retry_cnt++;
        }
      }
      break;
    case BatsConnEvent::BATS_CONNECTION_FAILED:
      printer_->PrintSenderFailure();
      break;
    case BatsConnEvent::BATS_CONNECTION_CLOSED:
    case BatsConnEvent::BATS_CONNECTION_SHUTDOWN_BY_PEER:
      if (is_control_channel) {
        this->CloseCtrlChannel();
        this->Stop();
        spdlog::info("[IperfFileTransfer] (ControlChannel) Iperf control stream is closed.");
      } else {
        assert(user != nullptr);
        int stream_id = *(reinterpret_cast<int*>(user));
        std::scoped_lock lock(streams_mutex_);
        if (stream_id >= streams_.size()) {
          spdlog::info("[IperfFileTransfer] (DataChannel) unexpected stream id {} from callback", stream_id);
          return false;
        }
        streams_[stream_id]->stream = nullptr;
        spdlog::info("[IperfFileTransfer] Iperf data stream is closed. stream id {}, conn [{}]", stream_id,
                     static_cast<void*>(data_conn.get()));
      }
      break;
    case BatsConnEvent::BATS_CONNECTION_ESTABLISHED:
      if (is_control_channel) {
        spdlog::info("[IperfFileTransfer] (ControlChannel) Iperf control stream is established.");
        // sender role only. (ctrl_chn_ is ready)
        ctrl_chn_ = data_conn;
        SendFileTransferRequest();
        // 4. start test stream when ack is received.
      } else {
        // read id from user context.
        assert(user != nullptr);
        {
          int stream_id = *(reinterpret_cast<int*>(user));
          std::scoped_lock lock(streams_mutex_);
          streams_[stream_id]->stream = data_conn;
          streams_[stream_id]->is_connected = true;
          streams_[stream_id]->ideal_buffer_length.store(data_conn->GetIdealBufferLength(), std::memory_order_seq_cst);
          CreateFileTransferThread(streams_[stream_id].get());
        }
      }
      break;

    case BatsConnEvent::BATS_CONNECTION_BUFFER_FULL:
      if (!is_control_channel) {
        int stream_id = *(reinterpret_cast<int*>(user));
        streams_[stream_id]->is_writable.store(false, std::memory_order_seq_cst);
      }
      break;
    case BatsConnEvent::BATS_CONNECTION_WRITABLE:
      if (!is_control_channel) {
        int stream_id = *(reinterpret_cast<int*>(user));
        // no instructions reorder
        streams_[stream_id]->is_writable.store(true, std::memory_order_seq_cst);
        streams_[stream_id]->writable_wait->NotifyOne();
      }
      break;
    case BatsConnEvent::BATS_CONNECTION_IDEAL_BUFFER_LENGTH:
      if (!is_control_channel) {
        int stream_id = *(reinterpret_cast<int*>(user));
        streams_[stream_id]->ideal_buffer_length.store(length, std::memory_order_seq_cst);
        spdlog::info("[IperfFileTransfer] Test stream {} ideal buf length update to {}", stream_id, length);
      }
      break;
    case BatsConnEvent::BATS_CONNECTION_DATA_RECEIVED:
      // receiver and sender role.
      if (is_control_channel) {
        spdlog::info("[IperfFileTransfer] (ControlChannel) Iperf control client received data, length: {}", length);
        HandleControlMessage(data_conn, data, length);
      } else {
        // read id from user context.
        assert(user != nullptr);
        int stream_id = *(reinterpret_cast<int*>(user));
        HandleReceivedFileData(stream_id, data, length);
      }
      break;
    case BatsConnEvent::BATS_CONNECTION_ALL_DATA_ACKED:
      if (!is_control_channel) {
        // read id from user context.
        assert(user != nullptr);
        int stream_id = *(reinterpret_cast<int*>(user));
        if (streams_[stream_id]->is_done) {
          streams_[stream_id]->all_acked = true;
          spdlog::info("[IperfFileTransfer] All data in the stream {} has been acked. conn [{}]", stream_id,
                       static_cast<void*>(data_conn.get()));
        }
      }
      break;
    case BatsConnEvent::BATS_CONNECTION_SEND_DATA_ERROR:
      if (!is_control_channel) {
        assert(user != nullptr);
        int stream_id = *(reinterpret_cast<int*>(user));
        streams_[stream_id]->has_err_in_send = true;
        spdlog::info("[IperfFileTransfer] File stream {} send data error! conn [{}]", stream_id,
                     static_cast<void*>(data_conn.get()));
      }
      break;
    default:
      break;
  }
  return true;
}

void IperfFileTransfer::DataListenCallback(const IBatsConnPtr& data_conn, const BatsListenEvent& event, void* user) {
  switch (event) {
    case BatsListenEvent::BATS_LISTEN_NEW_CONNECTION:
      spdlog::info("[IperfFileTransfer] Iperf file stream is accepted on port {}.", test_config_.port);
      {
        assert(data_conn != nullptr);
        auto copied_config = test_config_;
        copied_config.role = TestRole::ROLE_FILE_RECEIVER;  // receiver role.

        std::scoped_lock lock(streams_mutex_);
        auto received_stream = std::make_unique<FileStream>();
        received_stream->id = static_cast<int>(streams_.size());
        received_stream->is_connected = true;
        received_stream->stream = data_conn;
        received_stream->writable_wait = std::make_unique<PredicateWaitStrategy>();
        {
          auto self = shared_from_this();
          auto cb = [self](const IBatsConnPtr& c, const BatsConnEvent& e, const octet* d, int len, void* ctx) -> bool {
            return self->ConnectionCallback(c, e, d, len, ctx);
          };
          const_cast<IBatsConnPtr&>(data_conn)->SetConnectionCallback(cb, &received_stream->id);
          streams_.push_back(std::move(received_stream));
        }
        // TODO(.): is this necessary? create a thread for receiving if disk IO is the bottleneck.
        //  CreateFileRecvThread(streams_.back().get());
      }
      break;
    case BatsListenEvent::BATS_LISTEN_SUCCESS:
      spdlog::info("[IperfFileTransfer] Iperf test is listening on port {}.", test_config_.port);
      break;
    case BatsListenEvent::BATS_LISTEN_FAILED:
      spdlog::info("[IperfFileTransfer] Failed to listen on port {}.", test_config_.port);
      break;
    default:
      break;
  }
}

void IperfFileTransfer::SendResponseOk() {
  octetVec iper_control_data;
  iper_control_data.resize(300);
  octet* ptr = iper_control_data.data();
  // header
  iperf_control_data* control_header = reinterpret_cast<iperf_control_data*>(ptr);
  control_header->test_id = Id();
  control_header->magic = bats_iperf_magic_code;
  control_header->action_code = 0x04;
  control_header->param0 = 0x00;

  ctrl_chn_->SendData(iper_control_data.data(), iper_control_data.size());
  spdlog::info("[IperfFileTransfer] Test ack is sent.");
}

void IperfFileTransfer::SendResponseDenied() {
  octetVec iper_control_data;
  iper_control_data.resize(300);
  octet* ptr = iper_control_data.data();
  // header
  iperf_control_data* control_header = reinterpret_cast<iperf_control_data*>(ptr);
  control_header->test_id = Id();
  control_header->magic = bats_iperf_magic_code;
  control_header->action_code = 0x04;
  control_header->param0 = 0x01;

  ctrl_chn_->SendData(iper_control_data.data(), iper_control_data.size());
  spdlog::info("[IperfFileTransfer] Test ack is sent.");
}

void IperfFileTransfer::OnRecvResponse() {
  BatsConfiguration bats_config;
  bats_config.SetTimeout(5 * 1000);  // 5s (shutdown after idle for 5s)
  bats_config.SetMode(static_cast<TransMode>(test_config_.protocol));
  bats_config.SetCertFile(bats_default_cert_file);
  bats_config.SetKeyFile(bats_default_key_file);

  // reload.
  data_chn_connector_->LoadConfig(bats_config);

  std::scoped_lock lock(streams_mutex_);
  streams_.reserve(test_config_.num_streams);
  // 2. create data(file) streams;
  for (int i = 0; i < test_config_.num_streams; ++i) {
    spdlog::info("[IperfFileTransfer] start a file stream with id {}", i);
    auto stream = std::make_unique<FileStream>();
    stream->id = i;
    stream->stream = nullptr;
    stream->writable_wait = std::make_unique<PredicateWaitStrategy>();
    streams_.push_back(std::move(stream));
    data_chn_connector_->StartConnect(
        test_config_.peer_host, test_config_.port,
        std::bind(&IperfFileTransfer::ConnectionCallback, shared_from_this(), std::placeholders::_1,
                  std::placeholders::_2, std::placeholders::_3, std::placeholders::_4, std::placeholders::_5),
        &(streams_.at(i)->id));
  }
}

void IperfFileTransfer::SendFileTransferRequest() {
  octetVec iper_control_data;
  iper_control_data.resize(300);
  octet* ptr = iper_control_data.data();
  // header
  iperf_control_data* control_header = reinterpret_cast<iperf_control_data*>(ptr);
  control_header->magic = bats_iperf_magic_code;
  control_header->test_id = Id();
  control_header->action_code = 0x01;
  control_header->num_of_streams = test_config_.num_streams;
  control_header->param0 = test_config_.files.size();
  // save test_config_.total_bytes_in_files to param1 (hi bits) and param2 (low bits)
  control_header->param1 = static_cast<uint32_t>(test_config_.total_bytes_in_files >> 32);
  control_header->param2 = static_cast<uint32_t>(test_config_.total_bytes_in_files & 0xFFFFFFFF);

  ptr += sizeof(struct iperf_control_data);

  // body (Serialize)
  ptr = test_config_.Serialize(ptr);

  ctrl_chn_->SendData(iper_control_data.data(), iper_control_data.size());
  spdlog::info("[IperfFileTransfer] File transfer request is sent.");
  state_ = TestState::TEST_EXCHANGE_PARAMETERS;
}

void IperfFileTransfer::HandleFileTransferRequest(const struct iperf_control_data* control_header) {
  TestConfig received_test_config;
  auto data = reinterpret_cast<const octet*>(control_header) + sizeof(struct iperf_control_data);
  received_test_config.Deserialize(data);
  // num_files
  received_test_config.num_files = control_header->param0;

  std::stringstream ss;
  ss << received_test_config;
  spdlog::info("[IperfFileTransfer] File transfer parameters received {}", ss.str());

  // read files length from param1 (hi bits) and param2 (low bits)
  int64_t total_bytes = (static_cast<int64_t>(control_header->param1) << 32) | control_header->param2;
  std::error_code ec;
  const std_fs::space_info si = std_fs::space(std_fs::current_path(), ec);
  if (!ec && si.available <= total_bytes) {
    SendResponseDenied();
    spdlog::error("[IperfFileTransfer] Unable to receive {} bytes, current available space {}", total_bytes,
                  si.available);
    return;
  }
  spdlog::info("[IperfFileTransfer] will receive {} bytes, current available space {}", total_bytes, si.available);

  BatsConfiguration bats_config;
  bats_config.SetTimeout(5 * 1000);  // 5s (shutdown after idle for 5s)
  bats_config.SetCertFile(bats_default_cert_file);
  bats_config.SetKeyFile(bats_default_key_file);
  bats_config.SetMode(static_cast<TransMode>(received_test_config.protocol));
  data_chn_listener_ = std::make_shared<BatsProtocol>(io_, bats_config);
  using namespace std::placeholders;  // NOLINT
  auto resolution = test_config_.resolution;
  received_test_config.is_debug_mode = test_config_.is_debug_mode;
  test_config_ = received_test_config;
  test_config_.role = TestRole::ROLE_RECEIVER;
  test_config_.resolution = resolution;
  spdlog::info("[IperfFileTransfer] data chn StartListen on {} {} debug mode {}", interface_name_,
               received_test_config.port, received_test_config.is_debug_mode);
  data_chn_listener_->StartListen(ReadIPAddress(interface_name_), received_test_config.port,
                                  std::bind(&IperfFileTransfer::DataListenCallback, shared_from_this(), _1, _2, _3),
                                  nullptr);
  SendResponseOk();
}

bool IperfFileTransfer::IsInActive(struct FileStream* file_stream) const {
  return file_stream->is_connected && state_ != TestState::TEST_DONE;
}

void IperfFileTransfer::WriteFileSha256(const std::string sha256_file_name, const void* file_meta) {
  const auto file_meta_data = reinterpret_cast<const struct file_meta*>(file_meta);
  auto file_writer = new (std::nothrow) std::ofstream(sha256_file_name, std::ios::binary);
  file_writer->write(reinterpret_cast<const char*>(&file_meta_data->sha256sum[0]), max_hash_length);
  if (!file_writer->bad()) {
    spdlog::info("[IperfFileTransfer] File stream {} receives and writes the sha256sum");
  }
  // write a newline
  file_writer->write("\n", 1);
  file_writer->close();
  delete file_writer;
}

void IperfFileTransfer::ReadFileNames(const void* file_meta, std::string& file_name, std::string& file_ext) {
  const auto file_meta_data = reinterpret_cast<const struct file_meta*>(file_meta);
  for (int i = 0; i < max_name_length; i++) {
    if (file_meta_data->file_name[i] == 0x00) {
      break;
    }
    file_name.push_back(file_meta_data->file_name[i]);
  }

  for (int i = 0; i < max_extension_length; i++) {
    if (file_meta_data->file_extension[i] == 0x00) {
      break;
    }
    file_ext.push_back(file_meta_data->file_extension[i]);
  }
}

void IperfFileTransfer::HandleReceivedFileData(int stream_id, const octet* data, int length) {
  const auto file_bk_hdr = reinterpret_cast<const struct file_bk_header*>(data);
  const octet* file_content_ptr = nullptr;
  int file_content_len = 0;
  FileStream* cur_file_stream = nullptr;
  {
    std::scoped_lock lock(streams_mutex_);
    if (streams_.empty() || stream_id >= streams_.size()) {
      spdlog::error("[IperfFileTransfer] (DataChannel) unexpected stream id {} from callback", stream_id);
      return;
    }
    cur_file_stream = streams_.at(stream_id).get();
  }
  assert(cur_file_stream != nullptr);
  if (file_bk_hdr->syn) {
    // this is the first block which contains the file meta data.
    const auto file_meta_data = reinterpret_cast<const struct file_meta*>(data + sizeof(struct file_bk_header));
    std::string file_ext;
    file_ext.reserve(max_extension_length);
    cur_file_stream->file_name.clear();
    cur_file_stream->file_name.reserve(max_name_length);
    ReadFileNames(reinterpret_cast<const void*>(file_meta_data), cur_file_stream->file_name, file_ext);

    if (file_bk_hdr->has_sum) {
      WriteFileSha256(cur_file_stream->file_name + ".sha256sum", reinterpret_cast<const void*>(file_meta_data));
    }

    cur_file_stream->file_length = file_meta_data->file_length;
    if (!file_ext.empty()) {
      cur_file_stream->file_name += ".";
      cur_file_stream->file_name += file_ext;
    }
    spdlog::info("[IperfFileTransfer] File stream {} starts to receive file {}, length {} bytes.", stream_id,
                 cur_file_stream->file_name, cur_file_stream->file_length);
    cur_file_stream->file_writer = new (std::nothrow) std::ofstream(cur_file_stream->file_name, std::ios::binary);
    if (!cur_file_stream->file_writer || !cur_file_stream->file_writer->is_open()) {
      spdlog::error("[IperfFileTransfer] File stream {} failed to open file {} for writing.", stream_id,
                    cur_file_stream->file_name);
      return;
    }
    if (!test_config_.is_debug_mode) {
      // active the progress bar.
      shared_cprog_.register_bar(file_bk_hdr->file_seq, "Receiving file : " + cur_file_stream->file_name);
    }
    // write the rest data in this block.
    file_content_len = length - sizeof(struct file_bk_header) - sizeof(struct file_meta);
    file_content_ptr = reinterpret_cast<const octet*>(data + sizeof(struct file_bk_header) + sizeof(struct file_meta));
  } else {
    file_content_len = length - sizeof(struct file_bk_header);
    file_content_ptr = reinterpret_cast<const octet*>(data + sizeof(struct file_bk_header));
  }

  assert(cur_file_stream->file_writer != nullptr);
  assert(file_content_len > 0);
  spdlog::trace("[IperfFileTransfer] (DataChannel) Iperf data stream {} received data, length: {}", stream_id,
                file_content_len);
  const char* file_data_to_write = nullptr;
  int file_bytes_to_write = 0;
  if (file_bk_hdr->has_comp) {
    auto decompressed_bytes = IperfCompressor::Decompress(file_content_ptr, file_content_len, decompressed_buf.data(),
                                                          max_decompressed_buf_sz);
    if (decompressed_bytes != 0) {
      file_data_to_write = reinterpret_cast<const char*>(decompressed_buf.data());
      file_bytes_to_write = decompressed_bytes;
      spdlog::trace("Decompressed {} bytes to {} bytes", file_content_len, decompressed_bytes);
    } else {
      spdlog::error("[IperfFileTransfer] File stream {} failed to decompress the data.", stream_id);
      return;
    }
  } else {
    file_data_to_write = reinterpret_cast<const char*>(file_content_ptr);
    file_bytes_to_write = file_content_len;
  }

  cur_file_stream->file_writer->write(file_data_to_write, file_bytes_to_write);
  if (cur_file_stream->file_writer->bad()) {
    spdlog::error("[IperfFileTransfer] File stream {} failed to write file {}.", stream_id, cur_file_stream->file_name);
    return;
  }
  cur_file_stream->write_bytes += file_content_len;
  assert(file_bk_hdr->file_seq < 10);
  if (!test_config_.is_debug_mode) {
    shared_cprog_.update_bar(file_bk_hdr->file_seq, cur_file_stream->write_bytes, cur_file_stream->file_length);
  }
  if (file_bk_hdr->fin) {
    if (!test_config_.is_debug_mode) {
      shared_cprog_.finish(file_bk_hdr->file_seq);
    }
    spdlog::info(
        "[IperfFileTransfer] ++++++++++++++ File stream {} finished receiving file. file name {}, total received {} "
        "bytes",
        stream_id, cur_file_stream->file_name, cur_file_stream->write_bytes);
    // this is the last block.
    cur_file_stream->file_writer->close();
    delete cur_file_stream->file_writer;
    cur_file_stream->file_writer = nullptr;
    cur_file_stream->write_bytes = 0;
    if (test_config_.num_files > 0) {
      test_config_.num_files--;
    } else {
      spdlog::error("[IperfFileTransfer] Negative number of files detected!");
    }

    if (test_config_.num_files <= 0) {
      spdlog::info("[IperfFileTransfer] All the files were received!");
      state_ = TestState::TEST_DONE;
      test_config_.num_files = 0;
    }
  }
}

void IperfFileTransfer::CreateFileRecvThread(struct FileStream* file_stream) {
  // TODO(.):  not in the plan.
}

void IperfFileTransfer::CreateFileTransferThread(struct FileStream* file_stream) {
  file_stream->thread = std::thread([this, file_stream]() {
    spdlog::info("[IperfFileTransfer] File stream {} sending thread is started.", file_stream->id);
    File cur_file;
    struct file_meta file_meta_data;
    std::ifstream* file_reader = nullptr;
    std::vector<octet> send_buf;
    std::vector<octet> compress_buf;  // for in
    static constexpr int max_send_buf_sz = 300000;
    static constexpr int max_compress_buf_sz = 300000;
    double last_comp_ratio = 1.0;
    send_buf.resize(max_send_buf_sz);
    // compress ratio is about 1.78
    compress_buf.resize(max_compress_buf_sz);

    while (state_ != TestState::TEST_DONE) {
      if (!file_reader) {
        delete file_reader;
      }
      if (file_stream->has_err_in_send) {
        spdlog::error("[IperfFileTransfer] Critical error happened in sending, exit.");
        break;
      }
      // fetch one file from `pending_files_`
      auto success = pending_files_.Dequeue(cur_file);
      if (!success) {
        file_stream->is_done = true;
        break;
      }
      auto file_bk_hdr = reinterpret_cast<struct file_bk_header*>(send_buf.data());
      file_bk_hdr->file_seq = cur_file.bar_id;

      // file_meta_data
      std::memset(&file_meta_data, 0, sizeof(struct file_meta));
      // prepare file header
      file_meta_data.magic = bats_iperf_file_code;
      file_meta_data.stream_identifier = file_stream->id;
      file_meta_data.compression = static_cast<uint64_t>(CompressAlgo::CMA_NONE);
      file_meta_data.reserved = 0;

      // try to find the sha256 file
      std::string file_name = std_fs::path(cur_file.path).filename().string();
      std::string base_path = std_fs::path(cur_file.path).parent_path().string();
      spdlog::info("[IperfFileTransfer] base_path {}.", base_path);
      std::string sha256_file = (std_fs::path(base_path) / (file_name + ".sha256sum")).string();
      if (base_path.empty()) {
        sha256_file = file_name + ".sha256sum";
      } else {
        sha256_file = base_path + "/" + file_name + ".sha256sum";
      }
      if (std_fs::exists(sha256_file)) {
        std::ifstream hash_reader(sha256_file, std::ios::binary);
        hash_reader.read(reinterpret_cast<char*>(file_meta_data.sha256sum), max_hash_length);
        int nread = static_cast<int>(hash_reader.gcount());
        if (nread != max_hash_length || hash_reader.bad()) {
          spdlog::error("[IperfFileTransfer] File stream {} failed to read sha256sum file {}, ignore it.",
                        file_stream->id, sha256_file);
          file_bk_hdr->has_sum = 0;
        } else {
          spdlog::info("[IperfFileTransfer] File stream {} read sha256sum from file {}.", file_stream->id, sha256_file);
          file_bk_hdr->has_sum = 1;
        }
        hash_reader.close();
      } else {
        spdlog::info("[IperfFileTransfer] No sha256sum {} for reading.", sha256_file);
        // no sum file is provided.
        file_bk_hdr->has_sum = 0;
      }

      file_reader = new (std::nothrow) std::ifstream(cur_file.path, std::ios::binary);
      if (!file_reader || !file_reader->is_open()) {
        spdlog::error("[IperfFileTransfer] File stream {} failed to open file {} for reading.", file_stream->id,
                      cur_file.path);
        continue;
      }
      const int total_file_length = cur_file.file_sz;
      file_meta_data.file_length = static_cast<uint64_t>(total_file_length);

      // name and extension
      auto dot_pos = file_name.rfind('.');
      std::string name_only = (dot_pos == std::string::npos) ? file_name : file_name.substr(0, dot_pos);
      std::string extension =
          (dot_pos == std::string::npos) ? "" : file_name.substr(dot_pos + 1, file_name.length() - dot_pos - 1);
      std::strncpy(reinterpret_cast<char*>(file_meta_data.file_name), name_only.c_str(),
                   std::min(name_only.length(), static_cast<size_t>(max_name_length - 1)));
      std::strncpy(reinterpret_cast<char*>(file_meta_data.file_extension), extension.c_str(),
                   std::min(extension.length(), static_cast<size_t>(max_extension_length - 1)));

      shared_cprog_.register_bar(cur_file.bar_id, "Transfering file : " + file_name);
      spdlog::trace("[IperfFileTransfer] File stream {} starts to send file {}, length {} bytes. ", file_stream->id,
                    cur_file.path, total_file_length);

      uint64_t sent_bytes = 0;
      while (file_reader->eof() == false && IsInActive(file_stream)) {
        if (file_reader->bad()) {
          spdlog::error("[IperfFileTransfer] File stream {} failed to read file {}.", file_stream->id, cur_file.path);
          break;
        }

        if (file_stream->has_err_in_send) {
          break;
        }

        while (file_stream->is_writable.load(std::memory_order_seq_cst) == false) {
          if (IsInActive(file_stream) == false) {
            break;
          }
          if (file_stream->has_err_in_send.load(std::memory_order_seq_cst) == true) {
            break;
          }
          if (file_stream->writable_wait->EmptyWait()) {
            continue;
          }
        }

        char* file_read_ptr_in = nullptr;          // Pointer to the buf to receive(read) file data.
        octet* buf_to_send_ptr = send_buf.data();  // Pointer to the buf to the sent data.
        int bytes_to_send = 0;                     // the valid bytes to be sent in `send_buf`
        int ideal_file_read_len =
            0;  // The ideal length to read from file in each round which aims to full utilize the sending.
        int actual_file_read_len = 0;  // The actual length to read from file in each round.

        // 1. send bk header (1byte).
        bytes_to_send += sizeof(struct file_bk_header);
        if (unlikely(sent_bytes == 0)) {
          file_bk_hdr->syn = 1;
          ideal_file_read_len = file_stream->ideal_buffer_length.load(std::memory_order_seq_cst) -
                                sizeof(struct file_meta) - sizeof(struct file_bk_header);
          std::memcpy(buf_to_send_ptr + bytes_to_send, &file_meta_data, sizeof(struct file_meta));
          // 3. file meta data (optional)
          bytes_to_send += sizeof(struct file_meta);
        } else {
          file_bk_hdr->syn = 0;
          ideal_file_read_len =
              file_stream->ideal_buffer_length.load(std::memory_order_seq_cst) - sizeof(struct file_meta);
        }
        assert(ideal_file_read_len <= send_buf.size());

        if (test_config_.is_compression_enabled) {
          actual_file_read_len = std::min(ideal_file_read_len, static_cast<int>(compress_buf.size()));
          file_read_ptr_in = reinterpret_cast<char*>(compress_buf.data());
          file_bk_hdr->has_comp = 1;
        } else {
          actual_file_read_len = ideal_file_read_len;
          file_read_ptr_in = reinterpret_cast<char*>(buf_to_send_ptr + bytes_to_send);
          file_bk_hdr->has_comp = 0;
        }

        file_reader->read(file_read_ptr_in, actual_file_read_len);
        int nread = static_cast<int>(file_reader->gcount());
        if (likely(nread > 0)) {
          // can't read any more. nread < actual_file_read_len
          file_bk_hdr->fin = (sent_bytes + nread == total_file_length);
          if (test_config_.is_compression_enabled) {
            // Perform compress and copy compressed bytes to `send_buf`
            auto compressed_bytes =
                IperfCompressor::Compress(reinterpret_cast<octet*>(file_read_ptr_in), nread,
                                          buf_to_send_ptr + bytes_to_send, max_send_buf_sz - bytes_to_send);
            if (compressed_bytes != 0) {
              // last_comp_ratio is dynamic
              last_comp_ratio = static_cast<double>(nread) / compressed_bytes;
            }
            if (compressed_bytes == 0 || last_comp_ratio < 1.0) {
              // back to uncompressed.
              std::memcpy(buf_to_send_ptr + bytes_to_send, file_read_ptr_in, nread);
              bytes_to_send += nread;
              file_bk_hdr->has_comp = 0;
            } else {
              assert(compressed_bytes <= ideal_file_read_len);
              bytes_to_send += compressed_bytes;
              spdlog::trace("Compressed {} bytes to {} bytes, ratio {} ideal_file_read_len {}", nread, compressed_bytes,
                            last_comp_ratio, ideal_file_read_len);
            }
          } else {
            // 3. file content.
            bytes_to_send += nread;
            assert(bytes_to_send >= nread + sizeof(struct file_bk_header));
          }

          // Wait until writable or error in sending.
          bool send_ok = file_stream->stream->SendData(buf_to_send_ptr, bytes_to_send);
          while (send_ok == false) {
            if (IsInActive(file_stream) == false) {
              break;
            }
            if (file_stream->has_err_in_send.load(std::memory_order_seq_cst) == true) {
              break;
            }
            if (file_stream->is_writable.load(std::memory_order_seq_cst) == true) {
              // retry after timeout when it's writable.
              send_ok = file_stream->stream->SendData(buf_to_send_ptr, bytes_to_send);
            } else {
              // wait only if not writable.
              file_stream->writable_wait->EmptyWait();
            }
          }

          sent_bytes += nread;
          if (!test_config_.is_debug_mode) {
            shared_cprog_.update_bar(cur_file.bar_id, sent_bytes, total_file_length);
            if (file_bk_hdr->fin) {
              shared_cprog_.finish(cur_file.bar_id);
            }
          }
          spdlog::trace("[{}][IperfFileTransfer] File stream {} sends {} bytes for file {} fin {}", sent_bytes,
                        file_stream->id, nread, cur_file.path, static_cast<int>(file_bk_hdr->fin));
        }
      }
    }
    spdlog::info("[IperfFileTransfer] File stream {} sending thread is finished.", file_stream->id);
  });
}
