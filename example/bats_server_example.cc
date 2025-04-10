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

#include <cstring>
#include <iostream>

#include "bats_connection.h"
#include "bats_iocontext.h"
#include "bats_protocol.h"

int main(int argc, char* argv[]) {
  if (argc != 3) {
    std::cout << "Usage: " << argv[0] << " <cert_file> <key_file>" << std::endl;
    return -1;
  }
  auto cert_file = argv[1];
  auto key_file = argv[2];
  std::cout << "cert file: " << cert_file << ", key file: " << key_file << std::endl;
  int recv_cnt = 0;
  // connection callback which is used for observing the connection events.
  auto data_receive_callback = [&recv_cnt](const IBatsConnPtr& new_conn, const BatsConnEvent& event, const octet* data,
                                           int length, void* user) {
    switch (event) {
      case BatsConnEvent::BATS_CONNECTION_DATA_RECEIVED:
        // std::cout << "[bats_server_example] Connection received " << length << " bytes, and each back " << recv_cnt++
        //          << std::endl;
        // new_conn->SendData(data, length);
        break;
      case BatsConnEvent::BATS_CONNECTION_SHUTDOWN_BY_PEER:
        std::cout << "[bats_server_example] Connection is shutdown by peer." << std::endl;
        exit(0);
        break;
    }
  };
  // listener callback which is used for observing the listener events.
  auto listener_callback = [&recv_cnt, &data_receive_callback](const IBatsConnPtr& new_conn,
                                                               const BatsListenEvent& event, void* user) {
    switch (event) {
      case BatsListenEvent::BATS_LISTEN_NEW_CONNECTION:
        // accepted new connection on `new_conn`
        std::cout << "[bats_server_example] Connection established." << std::endl;
        // set connection callback for receiving data.
        new_conn->SetConnectionCallback(data_receive_callback);
        break;
      case BatsListenEvent::BATS_LISTEN_FAILED:
        std::cout << "[bats_server_example] Failed to listen." << std::endl;
        break;
      default:
        break;
    }
  };

  IOContext io;
  BatsConfiguration config;
  config.SetMode(TransMode::BTP);  // default to BTP
  config.SetCertFile(cert_file);
  config.SetKeyFile(key_file);

  BatsProtocol protocol(io);
  protocol.LoadConfig(config);

  // BATS server start listening on port 12345
  protocol.StartListen(12345, listener_callback);

  std::cout << "press any key to exit." << std::endl;
  getchar();
  return 0;
}
