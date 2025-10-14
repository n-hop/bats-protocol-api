/**
 * @file bats_connection.h
 * @author Lei Peng (peng.lei@n-hop.com)
 * @brief
 * @version 1.0.0
 * @date 2025-03-19
 *
 * Copyright (c) 2025 The n-hop technologies Limited. All Rights Reserved.
 *
 */
#ifndef INCLUDE_CPP_BATS_CONNECTION_H_
#define INCLUDE_CPP_BATS_CONNECTION_H_
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "include/cpp/bats_config.h"

using octet = unsigned char;
using octetVec = std::vector<octet>;
using octVecIter = std::vector<octet>::iterator;
using octVecConstIter = std::vector<octet>::const_iterator;

enum class BatsListenEvent : uint8_t {
  BATS_LISTEN_NONE = 0,
  BATS_LISTEN_NEW_CONNECTION,     // new connection is accepted.
  BATS_LISTEN_FAILED,             // failed to do listen.
  BATS_LISTEN_SUCCESS,            // listen success.
  BATS_LISTEN_ACCEPTED_ERROR,     // accepted connection error.
  BATS_LISTEN_ALREADY_IN_LISTEN,  // already in listen state.
  BATS_LISTEN_STOPPED,            // listen stopped.
};

///
/// @brief IOContext will emit those events when the state of the connection changes.
///
/// Within one BatsConnection,thecallback is thread-safe.
///
enum class BatsConnEvent : uint8_t {
  BATS_CONNECTION_NONE = 0,
  BATS_CONNECTION_FAILED,               // conenction failed.
  BATS_CONNECTION_ESTABLISHED,          // connection established.
  BATS_CONNECTION_TIMEOUT,              // connection timeout in 2s.
  BATS_CONNECTION_SHUTDOWN_BY_PEER,     // connection shutdown by peer.
  BATS_CONNECTION_WRITABLE,             // connection writable, ready to send data.
  BATS_CONNECTION_DATA_RECEIVED,        // connections has received data from peer.
  BATS_CONNECTION_SEND_COMPLETE,        // connection sent last data complete.
  BATS_CONNECTION_SEND_DATA_ERROR,      // error when sending data.
  BATS_CONNECTION_BUFFER_FULL,          // unable to write since the buffer of this connection is full.
  BATS_CONNECTION_CLOSED,               // connection closed.
  BATS_CONNECTION_ERROR,                // some errors in current connection
  BATS_CONNECTION_ALREADY_CONNECTED,    // connection already established.
  BATS_CONNECTION_IDEAL_BUFFER_LENGTH,  // update it's ideal buffer length when underlying MSS is changed.
  BATS_CONNECTION_ALL_DATA_ACKED,  // send the notification when all the sent data has been acked by peers (valid for
                                   // BRTP).
};

enum class BatsSendFlag : uint8_t {
  BATS_SEND_FLAG_NONE = 0,
  BATS_SEND_FLAG_FIN = 0x01,  // indicate this is the last data to be sent of current connection.
};

class IBatsConnection;
using IBatsConnPtr = std::shared_ptr<IBatsConnection>;
using ConnectionCallback = std::function<bool(const IBatsConnPtr&, const BatsConnEvent&, const octet*, int, void*)>;
using ListenCallback = std::function<void(const IBatsConnPtr&, const BatsListenEvent&, void*)>;

/// @brief BatsConnection is the interface for writing/reading data to/from the network.
class IBatsConnection {
 public:
  virtual ~IBatsConnection() = default;

  ///
  /// @brief Set the callback function to receive the connection events(read/write/close).
  /// @param cb The callback function. Don't block in the callback function.
  /// @param context The user context to be passed to the callback function.
  ///
  virtual void SetConnectionCallback(const ConnectionCallback& cb, void* context) = 0;
  virtual void SetConnectionCallback(ConnectionCallback&& cb, void* context) = 0;

  ///
  /// @brief Interface for sending data to the network.
  /// @param data The data to be sent.
  /// @return true if the data is sent successfully, false otherwise.
  ///         Sending failure can be caused by the following reasons:
  ///           1. The connection is closed.
  ///           2. The connection is not writable (user buffer is full).
  ///           3. The connection is not ready (not connected/established).
  ///
  virtual bool SendData(const octetVec& data, BatsSendFlag flags) = 0;
  virtual bool SendData(const octet* data, int length, BatsSendFlag flags) = 0;

  ///
  /// @brief Send a file to the network.
  ///
  /// only reliable connection(BRTP, BRCTP) supports this function.
  ///
  /// @param file_name The file with full path to be sent.
  /// @return
  ///
  virtual bool SendFile(const std::string& file_name) = 0;

  ///
  /// @brief Indicate whether the connection is writable at current time.
  /// @return true if the connection is writable, false otherwise.
  ///
  virtual bool IsWritable() = 0;

  /// @brief Get the ideal buffer length for the SendData function to send data efficiently.
  /// @return
  virtual uint32_t GetIdealBufferLength() const = 0;

  virtual const BatsConfiguration& GetConf() const = 0;
};

#endif  // INCLUDE_CPP_BATS_CONNECTION_H_
