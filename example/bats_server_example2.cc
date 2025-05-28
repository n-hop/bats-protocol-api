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
IBatsConnPtr accepted_conn = nullptr;
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
  // connection callback which is used for observing the connection events.
  auto connection_callback = [](const IBatsConnPtr& new_conn, const BatsConnEvent& event, const octet* data, int length,
                                void* user) {
    switch (event) {
      break;
      case BatsConnEvent::BATS_CONNECTION_SHUTDOWN_BY_PEER:
        std::cout << "[bats_server_example] Connection is shutdown by peer." << std::endl;
        exit(0);
        break;
    }
  };
  // listener callback which is used for observing the listener events.
  auto listener_callback = [&connection_callback](const IBatsConnPtr& new_conn, const BatsListenEvent& event,
                                                  void* user) {
    switch (event) {
      case BatsListenEvent::BATS_LISTEN_NEW_CONNECTION:
        // accepted new connection on `new_conn`
        std::cout << "[bats_server_example] Connection established." << std::endl;
        // set connection callback for receiving data.
        accepted_conn = new_conn;
        accepted_conn->SetConnectionCallback(connection_callback, nullptr);
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
  protocol.StartListen("127.0.0.1", 12345, listener_callback, nullptr);
  std::cout << "Ctrl+C to exit." << std::endl;

  int recv_cnt = 0;
  octetVec receive_buffer;
  receive_buffer.resize(30000);
  while (stop_signal_value != SIGINT) {
    while (accepted_conn == nullptr && stop_signal_value != SIGINT) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (accepted_conn) {
      auto ret = accepted_conn->RecvData(receive_buffer.data(), receive_buffer.size());
      std::cout << "[bats_server_example] Connection received " << ret << " bytes!" << recv_cnt++ << std::endl;
      if (ret <= 0) {
        std::cout << "[bats_server_example] Connection is shutdown by peer." << std::endl;
      }
    }
  }
  return 0;
}
