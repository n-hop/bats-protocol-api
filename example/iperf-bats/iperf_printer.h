/**
 * @file iperf_printer.h
 * @author Peng LEI (peng.lei@n-hop.com)
 * @brief
 * @version 1.0.0
 * @date 2025-06-17
 *
 * Copyright (c) 2025 The n-hop technologies Limited. All Rights Reserved.
 *
 */
#ifndef SRC_EXAMPLE_IPERF_BATS_IPERF_PRINTER_H_
#define SRC_EXAMPLE_IPERF_BATS_IPERF_PRINTER_H_
#include <memory>
/// @brief Interface for printing intervals, headers to console or log files.
class IPrinter {
 public:
  virtual ~IPrinter() = default;
  virtual void PrintSentSummaryHeader() = 0;
  virtual void PrintRecvSummaryHeader() = 0;
  virtual void PrintReceiverInfo() = 0;
  virtual void PrintSenderInfo() = 0;
  virtual void PrintReceiverFailure() = 0;
  virtual void PrintSenderFailure() = 0;
};
using IPrinterPtr = std::shared_ptr<IPrinter>;

#endif  // SRC_EXAMPLE_IPERF_BATS_IPERF_PRINTER_H_
