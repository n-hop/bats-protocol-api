/**
 * @file iperf_file_transfer.h
 * @author Peng LEI (peng.lei@n-hop.com)
 * @brief
 * @version 1.0.0
 * @date 2025-09-08
 *
 * Copyright (c) 2025 The n-hop technologies Limited. All Rights Reserved.
 *
 */

#ifndef SRC_EXAMPLE_IPERF_BATS_IPERF_FILE_TRANSFER_H_
#define SRC_EXAMPLE_IPERF_BATS_IPERF_FILE_TRANSFER_H_
#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "src/example/iperf-bats/iperf_compress_api.h"
#include "src/example/iperf-bats/iperf_test_api.h"
#include "src/example/iperf-bats/progressbar.h"
#include "src/include/queue.h"

/** @brief Iperf test architecture overview
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *
 *                 IperfBatsApi
 *                     |
 *                     | (manages)
 *                     |
 *                 IperfFileTransfer
 *                /        \
 *           [streams]    TestConfig
 *               /   \
 *              /     \
 *       sending   report of sending & receiving
 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 ** @brief File transfer scheduling
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *               [pending list of files]
 *              /          |           \
 *             (stream fetches the task(file))
 *            /            |            \
 *  [stream 0]  ...   [stream n]  ...  [stream n+1]  ...
 *      |                  |                 |
 *    path 0             path n           path n+1
 *      |                  |                 |
 * ................................................................
 *
 */
class File {
 public:
  int bar_id = 0;
  int64_t file_sz = 0;
  std::string path;
};

using FileQueue = Queue<File>;

class FileStream {
 public:
  FileStream() = default;
  FileStream(FileStream&&) = delete;
  FileStream& operator=(FileStream&&) = delete;

  uint32_t id = 0;
  std::atomic<bool> is_connected = false;
  std::atomic<bool> is_writable = true;
  std::atomic<bool> is_done = false;          // file transfer is done.(thread exits.)
  std::atomic<bool> all_acked = false;        // all data has been acked.
  std::atomic<bool> has_err_in_send = false;  // error when sending data.
  std::atomic<int> ideal_buffer_length = 4096;
  IBatsConnPtr stream = nullptr;
  std::ofstream* file_writer = nullptr;
  uint64_t write_bytes = 0;
  uint32_t retry_cnt = 0;
  std::string file_name;     // the current file name in receiving.
  uint64_t file_length = 0;  // the current file length in receiving.
  std::thread thread;
  std::unique_ptr<WaitStrategy<PredicateWaitStrategy>> writable_wait = nullptr;
};

using FileStreamPtr = std::unique_ptr<FileStream>;

class IperfFileTransfer : public IIperfTest, public std::enable_shared_from_this<IperfFileTransfer> {
 public:
  IperfFileTransfer(IOContext& io, std::basic_ofstream<char>& log_stream, uint64_t id, const TestConfig& config);
  IperfFileTransfer(IOContext& io, std::basic_ofstream<char>& log_stream, const TestConfig& config);
  virtual ~IperfFileTransfer();
  bool Start() override;
  bool Stop() override;
  void HandleControlMessage(const IBatsConnPtr& ctrl_conn, const octet* data, int length) override;
  void OnSentFinished() override;
  bool IsStreamReady() override;
  bool IsStreamDone() override;
  bool TestFinishState() override;
  void PrintStreamSendSummary() override;

 private:
  bool IsInActive(struct FileStream* file_stream) const;
  void HandleReceivedFileData(int stream_id, const octet* data, int length);
  void CreateFileRecvThread(struct FileStream* file_stream);
  void CreateFileTransferThread(struct FileStream* file_stream);
  void OnRecvResponse();
  void SendResponseOk();
  void SendResponseDenied();
  void SendFileTransferRequest();
  void HandleFileTransferRequest(const struct iperf_control_data* control_header);
  bool ConnectionCallback(const IBatsConnPtr& data_conn, const BatsConnEvent& event, const octet* data, int length,
                          void* user);
  void DataListenCallback(const IBatsConnPtr& data_conn, const BatsListenEvent& event, void* user);

  // ==== File related operations;
  void ReadFileNames(const void* file_meta, std::string& name, std::string& ext);
  void WriteFileSha256(const std::string sha256_file_name, const void* file_meta);
  static constexpr int max_decompressed_buf_sz = 300000;
  std::vector<octet> decompressed_buf;

  BatsProtocolPtr data_chn_listener_ = nullptr;
  BatsProtocolPtr data_chn_connector_ = nullptr;
  std::mutex streams_mutex_;
  std::vector<FileStreamPtr> streams_;
  FileQueue pending_files_;
  CompoundProgressBar shared_cprog_;
};

#endif  // SRC_EXAMPLE_IPERF_BATS_IPERF_FILE_TRANSFER_H_
