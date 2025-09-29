/**
 * @file iperf_common.cc
 * @author Lei Peng (peng.lei@n-hop.com)
 * @brief
 * @version 1.0.0
 * @date 2025-06-03
 *
 * Copyright (c) 2025 The n-hop technologies Limited. All Rights Reserved.
 *
 */

#include "src/example/iperf-bats/iperf_common.h"

#include "include/cpp/bats_config.h"

#ifdef __linux__
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>  // For AF_INET
#include <sys/types.h>
#elif defined(_WIN32) || defined(_WIN64)
// Windows specific includes would go here, e.g., <winsock2.h>, <iphlpapi.h>
#endif
#include <string.h>

#include <iostream>
#include <string>

std::string ReadIPAddress(const std::string& interface_name) {
  if (interface_name.empty()) {
    return "";
  }
  if (interface_name == "loopback" || interface_name == "lo") {
    return "127.0.0.1";
  }
#ifdef __linux__
  struct ifaddrs *ifaddr, *ifa;
  char host_ip[NI_MAXHOST];
  if (getifaddrs(&ifaddr) == -1) {
    std::cerr << "Error getting interface addresses: " << strerror(errno) << '\n';
    return "";
  }

  std::string found_ip;
  for (ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
    if (ifa->ifa_addr == nullptr) {
      continue;
    }

    if (strcmp(ifa->ifa_name, interface_name.c_str()) == 0 && ifa->ifa_addr->sa_family == AF_INET) {
      struct sockaddr_in* sockaddr_ipv4 = reinterpret_cast<struct sockaddr_in*>(ifa->ifa_addr);
      if (inet_ntop(AF_INET, &sockaddr_ipv4->sin_addr, host_ip, sizeof(host_ip)) != nullptr) {
        found_ip = host_ip;
        break;
      } else {
        std::cerr << "inet_ntop failed for interface " << interface_name << ": " << strerror(errno) << '\n';
      }
    }
  }
  freeifaddrs(ifaddr);
  return found_ip;
#elif defined(_WIN32) || defined(_WIN64)
  std::cerr << "ReadIPAddress for Windows is not fully implemented in this example." << '\n';
  return "";
#else
  std::cerr << "ReadIPAddress is not implemented for this platform." << '\n';
  return "";
#endif
}

std::string RoleString(const TestRole& role) {
  switch (role) {
    case TestRole::ROLE_SENDER:
      return "BATS sender";
    case TestRole::ROLE_RECEIVER:
      return "BATS receiver";
    case TestRole::ROLE_FILE_SENDER:
      return "BATS file sender";
    case TestRole::ROLE_FILE_RECEIVER:
      return "BATS file receiver";
    case TestRole::ROLE_OBSERVER:
      return "Observer";
    default:
      return "unknown";
  }
}

bool IsSupportedProtocol(int protocol) {
  /*
  if (static_cast<TransMode>(protocol) < TransMode::BTP || static_cast<TransMode>(protocol) > TransMode::TRANSPARENT) {
    return false;
  }*/
  return true;
}

bool IsReliableProtocol(int protocol) {
  if (static_cast<TransMode>(protocol) == TransMode::BRTP || static_cast<TransMode>(protocol) == TransMode::BRCTP ||
      protocol == 10) {
    return true;
  }
  return false;
}

static constexpr double KILO = 1000.0;
static constexpr double MEGA = 1000.0 * KILO;
static constexpr double GIGA = 1000.0 * MEGA;

std::string TestInterval::get_bitrate_string() const {
  // show bitrate in Kbit/Sec Mbit/sec Gbit/sec depends on the value of `bitrate`
  std::stringstream ss;
  // Convert bytes/sec to bits/sec
  double bits_per_second = bitrate * 8;
  ss << std::fixed << std::setprecision(2);
  if (bits_per_second == 0) {
    ss << 0.00 << " bits/sec";
    return ss.str();
  }
  if (bits_per_second >= GIGA) {
    ss << (bits_per_second / GIGA) << " Gbits/sec";
  } else if (bits_per_second >= MEGA) {
    ss << (bits_per_second / MEGA) << " Mbits/sec";
  } else {
    ss << (bits_per_second / KILO) << " Kbits/sec";
  }
  return ss.str();
}

std::string TestInterval::get_bytes_string() const {
  // show bytes in Kbytes, Mbytes, Gbytes depends on the value of `bytes`
  std::stringstream ss;
  ss << std::fixed << std::setprecision(2);
  if (bytes == 0) {
    ss << 0.00 << " Bytes";
    return ss.str();
  }
  if (bytes >= GIGA) {
    ss << (bytes / GIGA) << " GBytes";
  } else if (bytes >= MEGA) {
    ss << (bytes / MEGA) << " MBytes";
  } else {
    ss << (bytes / KILO) << " KBytes";
  }
  return ss.str();
}
