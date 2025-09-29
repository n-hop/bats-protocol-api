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
#if __has_include(<filesystem>)
#include <filesystem>  // NOLINT
namespace std_fs = std::filesystem;
#elif __has_include(<experimental/filesystem>)
#include <experimental/filesystem>
namespace std_fs = std::experimental::filesystem;
#else
error "Missing the <filesystem> header."
#endif
#include "include/cpp/bats_connection.h"
#include "include/cpp/bats_iocontext.h"
#include "include/cpp/bats_protocol.h"

static int stop_signal_value(0);
static bool is_stopped = false;

// User context to be passed to the listener callback.
void* user_context = &stop_signal_value;

int main(int argc, char* argv[]) {
  if (argc > 4 || argc < 3) {
    std::cout << "Usage: " << argv[0] << " <cert_file> <key_file> <mode>" << '\n';
    return -1;
  }
  auto cert_file = argv[1];
  auto key_file = argv[2];

  if (!std_fs::exists(cert_file)) {
    std::cout << "Cert file " << cert_file << " does not exist." << '\n';
    return 0;
  }
  if (!std_fs::exists(key_file)) {
    std::cout << "Key file " << key_file << " does not exist." << '\n';
    return 0;
  }
  int mode = 0;
  if (argc == 4) {
    mode = std::stoi(argv[3]);
  }
  std::cout << "cert file: " << cert_file << ", key file: " << key_file << ", mode: " << mode << '\n';

  // connection callback which is used for observing the connection events.
  auto data_receive_callback = [](const IBatsConnPtr& new_conn, const BatsConnEvent& event, const octet* data,
                                  int length, void* user) -> bool {
    switch (event) {
      case BatsConnEvent::BATS_CONNECTION_DATA_RECEIVED:
        break;
      case BatsConnEvent::BATS_CONNECTION_CLOSED:
        std::cout << "[bats_server_example] Connection closed." << '\n';
        is_stopped = true;
        break;
      case BatsConnEvent::BATS_CONNECTION_SHUTDOWN_BY_PEER:
        std::cout << "[bats_server_example] Connection is shutdown by peer." << '\n';
        is_stopped = true;
        break;
      default:
        std::cout << "[bats_server_example] Connection event: " << static_cast<int>(event) << '\n';
        break;
    }
    return true;
  };

  // listener callback which is used for observing the listener events.
  auto listener_callback = [&data_receive_callback](const IBatsConnPtr& new_conn, const BatsListenEvent& event,
                                                    void* user) {
    if (user != user_context) {
      std::cout << "[bats_server_example] User context is not matched. " << '\n';
      is_stopped = true;
    } else {
      int* sig = reinterpret_cast<int*>(user);
      std::cout << "[bats_server_example] User context value " << *sig << '\n';
    }
    switch (event) {
      case BatsListenEvent::BATS_LISTEN_NEW_CONNECTION:
        // accepted new connection on `new_conn`
        std::cout << "[bats_server_example] Connection established." << '\n';
        // set connection callback for receiving data.
        new_conn->SetConnectionCallback(data_receive_callback, user_context);
        break;
      case BatsListenEvent::BATS_LISTEN_FAILED:
        std::cout << "[bats_server_example] Failed to listen." << '\n';
        break;
      default:
        break;
    }
  };

  IOContext io;
  io.SetBATSLogLevel(BATSLogLevel::LOG_WARN);
  io.SetSignalCallback([](int sig) { stop_signal_value = sig; });

  BatsConfiguration config;
  config.SetMode(static_cast<TransMode>(mode));  // default to BTP
  config.SetCertFile(cert_file);
  config.SetKeyFile(key_file);

  BatsProtocol protocol(io);
  protocol.LoadConfig(config);

  // BATS server start listening on port 12345
  protocol.StartListen("127.0.0.1", 12345, listener_callback, user_context);
  std::cout << "Ctrl+C to exit." << '\n';
  while (stop_signal_value != SIGINT && is_stopped == false) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
  }
  return 0;
}
