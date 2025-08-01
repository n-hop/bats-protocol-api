/**
 * @file bats_iocontext.h
 * @author Lei Peng (peng.lei@n-hop.com)
 * @brief
 * @version 1.0.0
 * @date 2025-03-19
 *
 * Copyright (c) 2025 The n-hop technologies Limited. All Rights Reserved.
 *
 */
#ifndef INCLUDE_CPP_BATS_IOCONTEXT_H_
#define INCLUDE_CPP_BATS_IOCONTEXT_H_

#include <cstring>
#include <functional>

class IOContextImpl;
enum class BATSLogLevel : int {
  LOG_TRACE = 0,
  LOG_DEBUG,
  LOG_INFO,
  LOG_WARN,
  LOG_ERROR,
};
using SigCallback = std::function<void(int sig)>;

///
/// @brief IO event processing instance in BATS protocol
/// \note
/// - Each instance will create at least one thread to process I/O events.
/// - IOContext can be shared by multiple protocol instances.
///
class IOContext {
 public:
  IOContext();
  virtual ~IOContext();
  IOContextImpl& GetImpl() const { return *impl_; }

  ///
  /// @brief Set the log level for current process.
  /// @param level The log level can be set to `TRACE`, `DEBUG`, `INFO`, `WARN`, or `ERROR`.
  ///
  void SetBATSLogLevel(BATSLogLevel level);
  void SetBATSLogFilePath(const std::string& log_path);
  void SetSignalCallback(SigCallback&& cb);

 protected:
  IOContextImpl* impl_ = nullptr;
};

#endif  // INCLUDE_CPP_BATS_IOCONTEXT_H_
