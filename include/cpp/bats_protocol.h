/**
 * @file bats_protocol.h
 * @author Lei Peng (peng.lei@n-hop.com)
 * @brief
 * @version 1.0.0
 * @date 2025-01-01
 *
 * Copyright (c) 2025 The n-hop technologies Limited. All Rights Reserved.
 *
 */

#ifndef INCLUDE_CPP_BATS_PROTOCOL_H_
#define INCLUDE_CPP_BATS_PROTOCOL_H_

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "bats_config.h"
#include "bats_connection.h"
#include "bats_iocontext.h"

class ProtoImpl;
using ProtoImplPtr = std::shared_ptr<ProtoImpl>;

///
/// @brief BatsProtocol is the interface for starting a connection or waiting for a new connection.
///
class BatsProtocol {
 public:
  explicit BatsProtocol(IOContext& io_context);
  BatsProtocol(IOContext& io_context, const BatsConfiguration& conf);
  virtual ~BatsProtocol();

  ///
  /// @brief Load the configuration for the protocol.
  /// @param conf The configuration to be loaded.
  /// @return true if the configuration is loaded successfully, false otherwise.
  ///
  bool LoadConfig(const BatsConfiguration& conf);

  ///
  /// @brief Start connecting to the remote address and port.
  /// @param remote_addr The remote address to connect to.
  /// @param remote_port The remote port to connect to.
  /// @param context The user context to be passed to the callback function.
  /// @param cb The callback function to receive connection events. Don't block in the callback function.
  ///

  void StartConnect(const std::string& remote_addr, uint16_t remote_port, const ConnectionCallback& cb, void* context);

  ///
  /// @brief Start connecting to the remote address and port.
  /// @param cb The callback function for connection events.
  /// @param context The user context to be passed to the callback function.
  ///
  void StartConnect(const ConnectionCallback& cb, void* context);

  void StopConnection(const IBatsConnPtr& conn);

  ///
  /// @brief Start listening on the local port.
  /// @param local_addr The local address to be bind and listen.
  /// @param local_port The local port to listen on.
  /// @param cb The callback function to receive listen events(failed/new connection). Don't block in the callback
  /// @param context The user context to be passed to the callback function.
  ///
  void StartListen(const std::string& local_addr, uint16_t local_port, const ListenCallback& cb, void* context);

  ///
  /// @brief Start listening on the local port which is set in the BatsConfiguration.
  /// @param cb The callback function to receive listen events(failed/new connection).
  /// @param context The user context to be passed to the callback function.
  ///
  void StartListen(const ListenCallback& cb, void* context);

  void StopListen();

 protected:
  ProtoImplPtr impl_ = nullptr;
  IOContext& io_context_;
};

#endif  // INCLUDE_CPP_BATS_PROTOCOL_H_
