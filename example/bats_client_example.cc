/**
 * @file bats_client_example.cc
 * @author Lei Peng (peng.lei@n-hop.com)
 * @brief
 * @version 1.0.0
 * @date 2025-03-08
 *
 * Copyright (c) 2025 The n-hop technologies Limited. All Rights Reserved.
 *
 */
#include <csignal>
#include <cstring>
#include <iostream>
#include <thread>

#include "bats_config.h"
#include "bats_connection.h"
#include "bats_iocontext.h"
#include "bats_protocol.h"

static int stop_signal_value(0);

class MySender {
 public:
  MySender(BatsProtocol& protocol, int send_cnt, TransMode mode)
      : protocol_(protocol), send_cnt_(send_cnt), my_mode_(mode) {
    int ideal_length = 1200;
    if (my_mode_ == TransMode::BTP || my_mode_ == TransMode::BRTP) {
      ideal_length = 30000;
    }
    send_data.resize(ideal_length);
    // init with 0x01
    send_data.assign(ideal_length, 0x01);
  }
  ~MySender() = default;
  void MyConnectionCallback(const IBatsConnPtr& new_conn, const BatsConnEvent& event, const octet* data, int length,
                            void* user) {
    switch (event) {
      case BatsConnEvent::BATS_CONNECTION_FAILED:
        std::cout << "[bats_client_example] Connection failed." << std::endl;
        is_connect_failed = true;
        break;
      case BatsConnEvent::BATS_CONNECTION_CLOSED:
        std::cout << "[bats_client_example] Connection closed." << std::endl;
        is_connected = false;
        break;
      case BatsConnEvent::BATS_CONNECTION_SHUTDOWN_BY_PEER:
        std::cout << "[bats_client_example] Connection shutdown by peer." << std::endl;
        is_connected = false;
        is_connect_failed = true;
        break;
      case BatsConnEvent::BATS_CONNECTION_ESTABLISHED:
        std::cout << "[bats_client_example] Connection established." << std::endl;
        my_bats_connection = new_conn;
        is_connected = true;
        break;
      case BatsConnEvent::BATS_CONNECTION_DATA_RECEIVED:
        std::cout << "[bats_client_example] Connection received " << length << " bytes"
                  << " " << recv_cnt_++ << std::endl;
        break;
      case BatsConnEvent::BATS_CONNECTION_BUFFER_FULL:
        is_writable = false;
        // std::cout << "[bats_client_example] Connection buffer full." << std::endl;
        break;
      case BatsConnEvent::BATS_CONNECTION_WRITABLE:
        // std::cout << "[bats_client_example] Connection is writable." << std::endl;
        is_writable = true;
        break;
      case BatsConnEvent::BATS_CONNECTION_SEND_COMPLETE:
        // std::cout << "[bats_client_example] Connection data send complete." << std::endl;
        break;
      default:
        break;
    }
  }

  void StartConnect() {
    std::cout << "[bats_client_example] Start connect to server..." << std::endl;
    using namespace std::placeholders;
    protocol_.StartConnect("127.0.0.1", 12345, std::bind(&MySender::MyConnectionCallback, this, _1, _2, _3, _4, _5),
                           nullptr);
  }

  void StartSend() {
    while (is_connected == false && stop_signal_value != SIGINT) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      if (is_connect_failed) {
        if (timeout_cnt_ >= max_timeout_cnt_) {
          // simple timeout reconnect
          timeout_cnt_ = 0;
          is_connect_failed = false;
          using namespace std::placeholders;
          std::cout << "[bats_client_example] Reconnect to server..." << std::endl;
          protocol_.StartConnect("127.0.0.1", 12345,
                                 std::bind(&MySender::MyConnectionCallback, this, _1, _2, _3, _4, _5), nullptr);
          std::cout << "[bats_client_example] StartConnect returned..." << std::endl;
        }
      }
      timeout_cnt_++;
    }
    std::cout << "[bats_client_example] connection is ready, start sending data." << std::endl;
    if (send_cnt_ == 0) {
      send_cnt_ = std::numeric_limits<uint64_t>::max();
    }
    std::cout << "[bats_client_example] send_cnt: " << send_cnt_ << std::endl;
    while (is_connected && send_cnt_ != 0 && stop_signal_value != SIGINT) {
      if (is_writable == false) {
        std::this_thread::yield();
        continue;
      }
      is_writable = my_bats_connection->SendData(reinterpret_cast<const octet*>(send_data.data()), send_data.size());
      if (is_writable == false) {
        // not a successful sending.
        continue;
      }
      send_cnt_--;
    }

    if (is_connected == false) {
      std::cout << "[bats_client_example] Connection closed, stop send. " << send_cnt_ << std::endl;
    } else {
      std::cout << "[bats_client_example] Sending data is finished. " << send_cnt_ << std::endl;
    }
  }

 private:
  BatsProtocol& protocol_;
  octetVec send_data;
  uint64_t send_cnt_ = 10;
  uint64_t recv_cnt_ = 0;
  uint32_t timeout_cnt_ = 0;
  uint32_t max_timeout_cnt_ = 30;  // 100ms * 30
  bool is_connect_failed = {false};
  bool is_connected = {false};
  bool is_writable = {false};
  IBatsConnPtr my_bats_connection = nullptr;
  TransMode my_mode_ = TransMode::BTP;
};
int main(int argc, char* argv[]) {
  int mode = 0;
  int send_cnt = 0;
  if (argc >= 2) {
    send_cnt = std::stoi(argv[1]);
  }
  if (argc == 3) {
    mode = std::stoi(argv[2]);
  }
  std::cout << "[bats_client_example] <send_cnt> <mode> " << send_cnt << "," << mode << std::endl;
  IOContext io;
  io.SetSignalCallback([](int sig) { stop_signal_value = sig; });
  BatsConfiguration config;
  config.SetMode(static_cast<TransMode>(mode));  // default to BTP
  config.SetTimeout(2000);                       // 2000ms
  BatsProtocol protocol(io, config);
  MySender my_sender(protocol, send_cnt, static_cast<TransMode>(mode));

  my_sender.StartConnect();
  my_sender.StartSend();

  // wait for receiving echo back data otherwise the remote side can't send data back.
  std::this_thread::sleep_for(std::chrono::milliseconds(1000));
  return 0;
}