/**
 * @file iperf_verion.h.in
 * @author Peng LEI (peng.lei@n-hop.com)
 * @brief
 * @version 1.0.0
 * @date 2025-06-27
 *
 * Copyright (c) 2025 The n-hop technologies Limited. All Rights Reserved.
 *
 */
#ifndef SRC_EXAMPLE_IPERF_BATS_IPERF_VERSION_H_
#define SRC_EXAMPLE_IPERF_BATS_IPERF_VERSION_H_

#include <cstdint>
#include <string>

const uint32_t BATS_IPERF_MAJOR_VERSION = 1;
const uint32_t BATS_IPERF_MINOR_VERSION = 0;
const char BATS_IPERF_BUILD_TYPE[] = {"Release \n"};
const char BATS_IPERF_VERSION_STR[] = {
    "BATS Iperf version: v1.0(437f43e)  Build "
    "date: " __DATE__ " " __TIME__};

const char BATS_IPERF_COMPILER_INFO_STR[] = {"gcc11 "};

#endif  // SRC_EXAMPLE_IPERF_BATS_IPERF_VERSION_H_
