/**
 * @file bats_server_example.cc
 * @author Lei Peng (peng.lei@n-hop.com)
 * @brief
 * @version 1.0.0
 * @date 2025-03-08
 *
 * Copyright (c) 2025 The n-hop technologies Limited. All Rights Reserved.
 *
 */
#include <chrono>
#include <csignal>
#include <cstring>
#include <iostream>
#include <thread>

#include "bats_connection.h"
#include "bats_iocontext.h"
#include "bats_protocol.h"

static int stop_signal_value(0);
static bool is_stopped = false;
void* user_context = &stop_signal_value;

int main(int argc, char* argv[]) {
  if (argc > 4 || argc < 3) {
    std::cout << "Usage: " << argv[0] << " <cert_file> <key_file> <mode>" << std::endl;
    return -1;
  }
  auto cert_file = argv[1];
  auto key_file = argv[2];
  int mode = 0;
  if (argc == 4) {
    mode = std::stoi(argv[3]);
  }
  std::cout << "cert file: " << cert_file << ", key file: " << key_file << ", mode: " << mode << std::endl;
  int recv_cnt = 0;
  // connection callback which is used for observing the connection events.
  auto data_receive_callback = [&recv_cnt](const IBatsConnPtr& new_conn, const BatsConnEvent& event, const octet* data,
                                           int length, void* user) {
    switch (event) {
      case BatsConnEvent::BATS_CONNECTION_DATA_RECEIVED:
        // std::cout << "[bats_server_example] Connection received " << length << " bytes, and echo back " << recv_cnt++
        //          << std::endl;
        // Not recommended to send data in the callback, since it may block the IO thread or SendData may fail.
        // new_conn->SendData(data, length);
        // new_conn->Close();
        break;
      case BatsConnEvent::BATS_CONNECTION_CLOSED:
        std::cout << "[bats_server_example] Connection closed." << std::endl;
        is_stopped = true;
        break;
      case BatsConnEvent::BATS_CONNECTION_SHUTDOWN_BY_PEER:
        std::cout << "[bats_server_example] Connection is shutdown by peer." << std::endl;
        is_stopped = true;
        break;
    }
  };
  // listener callback which is used for observing the listener events.
  auto listener_callback = [&recv_cnt, &data_receive_callback](const IBatsConnPtr& new_conn,
                                                               const BatsListenEvent& event, void* user) {
    if (user != user_context) {
      std::cout << "[bats_server_example] User context is not matched." << std::endl;
      is_stopped = true;
    } else {
      int* sig = reinterpret_cast<int*>(user);
      std::cout << "[bats_server_example] User context value " << *sig << std::endl;
    }
    switch (event) {
      case BatsListenEvent::BATS_LISTEN_NEW_CONNECTION:
        // accepted new connection on `new_conn`
        std::cout << "[bats_server_example] Connection established." << std::endl;
        // set connection callback for receiving data.
        new_conn->SetConnectionCallback(data_receive_callback, user_context);
        break;
      case BatsListenEvent::BATS_LISTEN_FAILED:
        std::cout << "[bats_server_example] Failed to listen." << std::endl;
        break;
      default:
        break;
    }
  };

  IOContext io;
  io.SetBATSLogLevel(BATSLogLevel::INFO);
  io.SetSignalCallback([](int sig) { stop_signal_value = sig; });
  BatsConfiguration config;
  config.SetMode(static_cast<TransMode>(mode));  // default to BTP
  config.SetCertFile(cert_file);
  config.SetKeyFile(key_file);

  BatsProtocol protocol(io);
  protocol.LoadConfig(config);

  // BATS server start listening on port 12345
  protocol.StartListen("127.0.0.1", 12345, listener_callback, user_context);
  std::cout << "Ctrl+C to exit." << std::endl;
  while (stop_signal_value != SIGINT && is_stopped == false) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
  }
  return 0;
}
