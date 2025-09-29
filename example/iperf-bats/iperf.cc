/**
 * @file iperf.cc
 * @author Lei Peng (peng.lei@n-hop.com)
 * @brief
 * @version 1.0.0
 * @date 2025-05-26
 *
 * Copyright (c) 2025 The n-hop technologies Limited. All Rights Reserved.
 *
 */
#include <iostream>
#include <memory>

#include "src/example/iperf-bats/iperf_bats_api.h"
#include "src/example/iperf-bats/iperf_common.h"

int main(int argc, char* argv[]) {
  IperfBatsApiPtr api = std::make_shared<IperfBatsApi>();
  auto role = api->InitFromArgs(argc, argv);
  switch (role) {
    case TestRole::ROLE_SENDER:
    case TestRole::ROLE_FILE_SENDER:
      api->RunTest();
      break;
    case TestRole::ROLE_RECEIVER:
    case TestRole::ROLE_FILE_RECEIVER:
      api->RunServer();
      break;
    case TestRole::ROLE_NONE:
      std::cerr << "Invalid input args. Please check the usage." << '\n';
      break;
    default:
      break;
  }
  return 0;
}
