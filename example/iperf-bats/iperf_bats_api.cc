/**
 * @file iperf_bats_api.cc
 * @author Lei Peng (peng.lei@n-hop.com)
 * @brief
 * @version 1.0.0
 * @date 2025-05-26
 *
 * Copyright (c) 2025 The n-hop technologies Limited. All Rights Reserved.
 *
 */
#include "src/example/iperf-bats/iperf_bats_api.h"

#include <spdlog/cfg/env.h>   // support for loading levels from the environment variable
#include <spdlog/fmt/ostr.h>  // support for user defined types
#include <spdlog/spdlog.h>

#include "src/example/iperf-bats/iperf_file_transfer.h"
#include "src/example/iperf-bats/iperf_version.h"

#if __has_include(<filesystem>)
#include <filesystem>  // NOLINT
namespace std_fs = std::filesystem;
#elif __has_include(<experimental/filesystem>)
#include <experimental/filesystem>
namespace std_fs = std::experimental::filesystem;
#else
error "Missing the <filesystem> header."
#endif
#include <string.h>

#include <chrono>  // NOLINT
#include <csignal>
#include <fstream>
#include <functional>  // NOLINT
#include <iostream>
#include <list>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>  // NOLINT
#include <unordered_map>
#include <utility>

#include "src/include/time.h"

int IperfBatsApi::stop_signal_value = 0;

void InitVer(std::string& ver) {
  if (ver.empty()) {
#ifdef _MSC_VER
    ver += "* Windows platform\n";
#elif defined(__GLIBC__)
    ver += "* Linux/Unix platform\n";
#endif
    ver += std::string(BATS_IPERF_VERSION_STR);
    ver += " Compiler: ";
    ver += std::string(BATS_IPERF_COMPILER_INFO_STR);
    ver += std::string(BATS_IPERF_BUILD_TYPE);
  }
}

IperfBatsApi::IperfBatsApi() {
  is_stopped_ = false;
  spdlog::cfg::load_env_levels();
}

IperfBatsApi::~IperfBatsApi() {
  if (log_stream_.is_open()) {
    log_stream_ << std::flush;
    log_stream_.close();
  }
  this->StopAll();
}

void IperfBatsApi::Usage() {
  InitVer(ver_info_);
  std::cout << ver_info_;
  std::cout << "Usage: bats_iperf [options]\n"
            << "Options:\n"
            << "  -t <time>        : Test duration in seconds (default: 10)\n"
            << "  -i <interval>    : Interval for reporting results in seconds (default: 1)\n"
            << "  -I <interface>   : The name of the interface\n"
            << "  -P <num_streams> : Number of parallel streams (default: 1)\n"
            << "  -r               : Enable reverse mode (server to client)\n"
            << "  -p <port>        : Port number to use for the data channel (default: 5201); and \n"
            << "                     The port of signaling channel is set to `port - 1`.\n"
            << "  -m <mode>        : Protocol mode (0: BTP, 1: BRTP, 2: BRCTP, 3: TRANSPARENT, default: BTP)\n"
            << "                     When run as server, this option is ignored.\n"
            << "  -s               : Run as server\n"
            << "  -c <host>        : Run as client and connect to <host>\n"
            << "  -f <file>        : Run as client to send the file\n"
            << "  -l <logfile>     : Log file to write iperf output (default: stdout)\n"
            << "  -L <logfile path>: Log path to write bats protocol log (default: "
               ")\n"
            << "  -b               : Max allowed sending rate in Mbps, only valid for clients\n"
            << "  -h               : Show this help message\n"
            << "  -d               : Emit debugging output\n"
            << "  -T               : Show precise time wall clock in the reports\n"
            << "  -C               : Enable the data compression\n";
}

TestRole IperfBatsApi::InitFromArgs(int argc, char* argv[]) {
  if (argc < 2) {
    spdlog::error("No arguments provided. Please check the usage.");
    Usage();
    return TestRole::ROLE_NONE;
  }

  io_ = std::make_shared<IOContext>();
  auto self = shared_from_this();
  io_->SetSignalCallback([self](int sig) {
    stop_signal_value = sig;
    self->StopAll();
  });

  BatsConfiguration bats_config;
  bats_config.SetTimeout(1000 * 1000);
  // default to TCP
  bats_config.SetFrameType(FrameType::BATS_HEADER_MIN);
  bats_config.SetMode(static_cast<TransMode>(10));
  bats_config.SetCertFile(bats_default_cert_file);
  bats_config.SetKeyFile(bats_default_key_file);
  ctrl_chn_listener_ = std::make_shared<BatsProtocol>(*io_, bats_config);

  std::unordered_map<std::string, int*> int_args_tbl;
  std::unordered_map<std::string, std::reference_wrapper<std::string>> str_args_tbl;
  std::unordered_map<std::string, std::reference_wrapper<std::vector<std::string>>> vec_args_tbl;

  int_args_tbl.insert({"-P", &user_input_config.num_streams});
  int_args_tbl.insert({"-p", &user_input_config.port});
  int_args_tbl.insert({"-m", &user_input_config.protocol});
  int_args_tbl.insert({"-b", &user_input_config.max_allowed_sending_rate});

  // files `-f`
  std::string cur_vec_arg;
  bool is_reading_vec = false;
  vec_args_tbl.insert({"-f", std::ref(user_input_config.files)});

  str_args_tbl.insert({"-I", std::ref(interface_name_)});
  str_args_tbl.insert({"-l", std::ref(log_file_)});
  str_args_tbl.insert({"-L", std::ref(bats_log_path_)});

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "-h") {
      Usage();
      return TestRole::ROLE_OBSERVER;
    } else if (arg == "-t") {
      user_input_config.time = std::stof(argv[++i]);
    } else if (arg == "-i") {
      user_input_config.interval = std::stof(argv[++i]);
    } else if (arg == "-d") {
      user_input_config.is_debug_mode = true;
    } else if (arg == "-r") {
      user_input_config.is_reverse_enabled = true;
    } else if (arg == "-C") {
      user_input_config.is_compression_enabled = true;
    } else if (arg == "-T") {
      user_input_config.is_precise_time_enabled = true;
    } else if (arg == "-s") {
      if (test_role_ == TestRole::ROLE_NONE) {
        test_role_ = TestRole::ROLE_RECEIVER;
      } else {
        spdlog::warn("[IperfBatsApi] Role already set. Use -s for server or -c for client.");
        return TestRole::ROLE_NONE;
      }
    } else if (int_args_tbl.find(arg) != int_args_tbl.end()) {
      if (i + 1 < argc) {
        *(int_args_tbl.at(arg)) = std::stoi(argv[++i]);
      } else {
        spdlog::warn("[IperfBatsApi] Missing value for {} option.", arg);
        return TestRole::ROLE_NONE;
      }
    } else if (vec_args_tbl.find(arg) != vec_args_tbl.end()) {
      if (i + 1 < argc) {
        cur_vec_arg = arg;
        is_reading_vec = true;
        auto& vec = vec_args_tbl.at(cur_vec_arg);
        auto read_string = std::string(argv[++i]);
        vec.get().push_back(read_string);
        spdlog::info("[IperfBatsApi] add string {} to vec arg {} ", read_string, cur_vec_arg);
      } else {
        spdlog::warn("[IperfBatsApi] Missing value for {} option.", arg);
        return TestRole::ROLE_NONE;
      }
    } else if (str_args_tbl.find(arg) != str_args_tbl.end()) {
      if (i + 1 < argc) {
        str_args_tbl.at(arg).get() = std::string(argv[++i]);
      } else {
        spdlog::warn("[IperfBatsApi] Missing value for {} option.", arg);
        return TestRole::ROLE_NONE;
      }
    } else if (arg == "-c") {
      if (i + 1 < argc) {
        user_input_config.peer_host = std::string(argv[++i]);
        if (test_role_ == TestRole::ROLE_NONE) {
          test_role_ = TestRole::ROLE_SENDER;
        } else {
          spdlog::warn("[IperfBatsApi] Role already set. Use -s for server or -c for client.");
          return TestRole::ROLE_NONE;
        }
      } else {
        spdlog::warn("[IperfBatsApi] Missing value for {} option.", arg);
        return TestRole::ROLE_NONE;
      }
    } else {
      if (is_reading_vec && !cur_vec_arg.empty()) {
        auto& vec = vec_args_tbl.at(cur_vec_arg);
        auto read_string = std::string(argv[i]);
        vec.get().push_back(read_string);
        spdlog::info("[IperfBatsApi] add string {} to vec arg {} ", read_string, cur_vec_arg);
      } else {
        spdlog::error("[IperfBatsApi] Unrecognized argument: {} .", arg);
        Usage();
        return TestRole::ROLE_NONE;
      }
    }
  }
  spdlog::trace("[IperfBatsApi] Parse done: {} .", argc);

  if (!user_input_config.files.empty() && test_role_ == TestRole::ROLE_SENDER) {
    test_role_ = TestRole::ROLE_FILE_SENDER;
  }

  spdlog::set_level(user_input_config.is_debug_mode ? spdlog::level::info : spdlog::level::warn);
  io_->SetBATSLogLevel(user_input_config.is_debug_mode ? BATSLogLevel::LOG_INFO : BATSLogLevel::LOG_ERROR);
  if (bats_log_path_.empty() == false) {
    io_->SetBATSLogFilePath(bats_log_path_);
    spdlog::info("[IperfBatsApi] BATS log path: {}", bats_log_path_);
  }

  if (log_file_.empty() == false) {
    if (!std_fs::exists(std_fs::path(log_file_).parent_path())) {
      std_fs::create_directories(std_fs::path(log_file_).parent_path());

      std::stringstream ss;
      ss << std_fs::path(log_file_).parent_path();
      spdlog::info("[IperfBatsApi] Create log directory: {}", ss.str());
    }
    log_stream_ = std::basic_ofstream<char>(log_file_, std::ios::out | std::ios::app);
    log_stream_.imbue(std::locale());
  }

  user_input_config.role = test_role_;
  // stream user_input_config to sstream
  std::stringstream ss;
  ss << user_input_config;
  spdlog::info("[IperfBatsApi] {}", ss.str());
  if (isReceiverRole(test_role_) && ValidateServerConfig(user_input_config) == false) {
    spdlog::error("[IperfBatsApi] Invalid server configuration.");
    return TestRole::ROLE_NONE;
  }
  if (isSenderRole(test_role_) && ValidateClientConfig(user_input_config) == false) {
    spdlog::error("[IperfBatsApi] Invalid client configuration.");
    return TestRole::ROLE_NONE;
  }

  if (!isReceiverRole(test_role_) && !isSenderRole(test_role_)) {
    spdlog::error("[IperfBatsApi] Test role is not set. Please check the usage or set it SetRole.");
    return TestRole::ROLE_NONE;
  }

  if (isReceiverRole(test_role_)) {
    server_listen_port_ = user_input_config.port - 1;
  }

  if (isSenderRole(test_role_)) {
    IIperfTestPtr test = nullptr;
    if (test_role_ == TestRole::ROLE_SENDER) {
      // when run as ROLE_RECEIVER, test_tbl_ is initialized as empty.
      test = std::make_shared<IperfTest>(*io_, log_stream_, user_input_config);
    } else if (test_role_ == TestRole::ROLE_FILE_SENDER) {
      test = std::make_shared<IperfFileTransfer>(*io_, log_stream_, user_input_config);
    }
    test->SetPrinter(this);
    test->SetBoundInf(interface_name_);
    test->SetResolution(default_resolution);
    test_tbl_.insert({test->Id(), test});
  }

  if (test_tbl_.size() < 1 && isSenderRole(test_role_)) {
    spdlog::error("[IperfBatsApi] No iperf test is added! current test role {}", RoleString(test_role_));
  } else {
    spdlog::info("[IperfBatsApi] start as {}. Ctrl+C to exit. ", RoleString(test_role_));
  }
  return test_role_;
}

void IperfBatsApi::ControlChannelListenCallback(const IBatsConnPtr& ctrl_conn, const BatsListenEvent& event,
                                                void* user) {
  switch (event) {
    case BatsListenEvent::BATS_LISTEN_NEW_CONNECTION:
      is_ctrl_chn_connected_ = true;
      using namespace std::placeholders;  // NOLINT
      ctrl_conn->SetConnectionCallback(
          std::bind(&IperfBatsApi::ControlChannelConnectionCallback, this, _1, _2, _3, _4, _5), nullptr);
      break;
    case BatsListenEvent::BATS_LISTEN_FAILED:
      spdlog::info("[IperfBatsApi] Failed to listen on port {}.", server_listen_port_);
      this->PrintReceiverFailure();
      break;
    case BatsListenEvent::BATS_LISTEN_SUCCESS:
      this->PrintReceiverInfo();
      break;
    default:
      break;
  }
}

bool IperfBatsApi::ControlChannelConnectionCallback(const IBatsConnPtr& ctrl_conn, const BatsConnEvent& event,
                                                    const octet* data, int length, void* user) {
  switch (event) {
    case BatsConnEvent::BATS_CONNECTION_FAILED:
    case BatsConnEvent::BATS_CONNECTION_CLOSED:
    case BatsConnEvent::BATS_CONNECTION_SHUTDOWN_BY_PEER:
      if (user != nullptr) {
        uint32_t id = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(user));
        std::scoped_lock lock(test_tbl_mutex_);
        if (test_tbl_.find(id) != test_tbl_.end()) {
          spdlog::info("[IperfBatsApi] Iperf control stream is closed. test id {}", id);
          test_tbl_.at(id)->CloseCtrlChannel();
          test_tbl_.at(id)->Stop();
        }
      }
      is_ctrl_chn_connected_ = false;
      break;
    case BatsConnEvent::BATS_CONNECTION_ESTABLISHED:
      is_ctrl_chn_connected_ = true;
      spdlog::info("[IperfBatsApi] Iperf control stream is established.");
      break;
    case BatsConnEvent::BATS_CONNECTION_DATA_RECEIVED:
      spdlog::info("[IperfBatsApi] Iperf control stream received data, length: {}", length);
      HandleControlMessage(ctrl_conn, data, length);
      break;
    default:
      break;
  }
  return true;
}

void IperfBatsApi::HandleControlMessage(const IBatsConnPtr& ctrl_conn, const octet* data, int length) {
  IIperfTestPtr received_test = nullptr;
  const struct iperf_control_data* control_header = reinterpret_cast<const struct iperf_control_data*>(data);
  switch (control_header->action_code) {
    case 0x01:
      // file transfer request
      spdlog::info("[IperfBatsApi] Created a file transfer request with ID: {} by request", control_header->test_id);
      received_test =
          std::make_shared<IperfFileTransfer>(*io_, log_stream_, control_header->test_id, user_input_config);
      break;
    case 0x02:
      spdlog::info("[IperfBatsApi] Created a new test with ID: {} by request", control_header->test_id);
      received_test = std::make_shared<IperfTest>(*io_, log_stream_, control_header->test_id, user_input_config);
      //  general iperf test (param exchange)
    default:
      break;
  }

  {
    std::scoped_lock lock(test_tbl_mutex_);
    if (test_tbl_.find(control_header->test_id) == test_tbl_.end()) {
      received_test->SetRole(TestRole::ROLE_RECEIVER);
      received_test->SetResolution(default_resolution);
      received_test->SetPrinter(this);
      received_test->SetBoundInf(interface_name_);
      received_test->HandleControlMessage(ctrl_conn, data, length);
      test_tbl_.insert({received_test->Id(), received_test});

      // set it again for using user context
      using namespace std::placeholders;  // NOLINT
      ctrl_conn->SetConnectionCallback(
          std::bind(&IperfBatsApi::ControlChannelConnectionCallback, this, _1, _2, _3, _4, _5),
          reinterpret_cast<void*>(static_cast<uintptr_t>(received_test->Id())));
    } else {
      test_tbl_.at(control_header->test_id)->HandleControlMessage(ctrl_conn, data, length);
    }
  }
}

bool IperfBatsApi::ValidateClientConfig(const TestConfig& test_config) {
  if (test_config.peer_host.empty()) {
    return false;
  }

  if (IsSupportedProtocol(test_config.protocol) == false) {
    spdlog::error(
        "[IperfBatsApi] Invalid protocol specified. Valid protocols are 0 (BTP), 1 (BRTP), 2 (BRCTP), 3 "
        "(TRANSPARENT).");
    Usage();
    return false;
  }

  if (test_config.role == TestRole::ROLE_FILE_SENDER) {
    if (!(test_config.protocol == 1 || test_config.protocol == 2)) {
      spdlog::error("[IperfBatsApi] File transfer only supports with BRTP.");
      return false;
    }
  }

  if (test_config.role == TestRole::ROLE_FILE_SENDER) {
    if (test_config.files.size() > 10) {
      spdlog::error("[IperfBatsApi] File transfer only supports up to 10 files at one time.");
      return false;
    }
    bool has_at_least_one = false;
    for (const auto& full_file_path : test_config.files) {
      if (!std_fs::exists(std_fs::path(full_file_path))) {
        spdlog::warn("[IperfBatsApi] File {} not found.", full_file_path);
      } else {
        spdlog::info("[IperfBatsApi] Found the file {}.", full_file_path);
        has_at_least_one = true;
      }
    }
    if (!has_at_least_one) {
      spdlog::error("[IperfBatsApi] None of the specified files exist.");
      return false;
    }
  }

  if (test_config.role == TestRole::ROLE_FILE_SENDER) {
    // num_streams <= num_files
    if (test_config.files.size() < static_cast<size_t>(test_config.num_streams)) {
      spdlog::error(
          "[IperfBatsApi] The number of files ({}) is less than the number of parallel streams ({}). "
          "Not recommended to do so since some streams will be idle",
          test_config.files.size(), test_config.num_streams);
      return false;
    }
  }
  return true;
}

bool IperfBatsApi::ValidateServerConfig(const TestConfig& test_config) {
  if (interface_name_.empty()) {
    return false;
  }
  if (ReadIPAddress(interface_name_).empty()) {
    spdlog::error("[IperfBatsApi] Invalid interface name.");
    return false;
  }
  if (IsSupportedProtocol(test_config.protocol) == false) {
    spdlog::error(
        "[IperfBatsApi] Invalid protocol specified. Valid protocols are 0 (BTP), 1 (BRTP), 2 (BRCTP), 3 "
        "(TRANSPARENT).");
    Usage();
    return false;
  }

  if (!test_config.files.empty()) {
    spdlog::error("[IperfBatsApi] '-f' is for bats_iperf client only");
    return false;
  }

  if (test_config.max_allowed_sending_rate > 0) {
    spdlog::error("[IperfBatsApi] '-b' is for bats_iperf client only");
    return false;
  }
  return true;
}

void IperfBatsApi::RunServer() {
  if (!isReceiverRole(test_role_)) {
    spdlog::error("[IperfBatsApi] RunServer can only be called when test_role_ is ROLE_RECEIVER/ROLE_FILE_RECEIVER.");
    return;
  }
  using namespace std::placeholders;  // NOLINT
  spdlog::info("[IperfBatsApi] ctrl chn StartListen on {}", interface_name_);
  ctrl_chn_listener_->StartListen(ReadIPAddress(interface_name_), server_listen_port_,
                                  std::bind(&IperfBatsApi::ControlChannelListenCallback, this, _1, _2, _3), nullptr);
  EnterReceiverIntervalLoop();
}

void IperfBatsApi::RunTest() {
  if (!isSenderRole(test_role_)) {
    spdlog::error("[IperfBatsApi] RunTest can only be called when test_role_ is ROLE_SENDER.");
    return;
  }

  if (test_tbl_.size() != 1) {
    spdlog::error("[IperfBatsApi] RunTest can only be called when there is exactly one test in the table.");
    return;
  }
  auto iperf_test = test_tbl_.begin()->second;

  // 3. start the test streams.
  iperf_test->Start();

  // 4. wait when all test streams are connected.
  constexpr int max_timeout_cnt = 10 * 1000;  // 10 seconds
  int timeout_cnt = 0;
  while (iperf_test->IsStreamReady() == false && stop_signal_value != SIGINT) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    if (++timeout_cnt > max_timeout_cnt) {
      spdlog::error("[IperfBatsApi] Unable to establish all test streams after {} seconds. Exiting.",
                    max_timeout_cnt / 1000);
      return;
    }
  }
  EnterSenderIntervalLoop();
}

void IperfBatsApi::EnterReceiverIntervalLoop() {
  // 5. watch the test interval and print the report.
  std::list<std::pair<IIperfTestPtr, int>> dying_tests;
  while (stop_signal_value != SIGINT) {
    std::this_thread::sleep_for(std::chrono::milliseconds((1000 / default_resolution)));
    std::scoped_lock lock(test_tbl_mutex_);
    for (auto iter = test_tbl_.begin(); iter != test_tbl_.end();) {
      auto& test = iter->second;
      test->TestFinishState();
      if (test->GetState() == TestState::TEST_DONE && test->IsStreamDone()) {
        spdlog::info("[IperfBatsApi] ROLE_RECEIVER is done. Test ID: {}", test->Id());
        dying_tests.emplace_back(test, 0);
        iter = test_tbl_.erase(iter);
      } else {
        spdlog::trace("Test id {} state {} , is done {}", test->Id(), static_cast<int>(test->GetState()),
                      test->IsStreamDone());
        iter++;
      }
    }

    for (auto iter = dying_tests.begin(); iter != dying_tests.end();) {
      auto& test = iter->first;
      auto& cnt = iter->second;
      if (is_ctrl_chn_connected_ == false || cnt > 10) {
        // avoid to close connection before sender receives the FIN ack.
        spdlog::info("[IperfBatsApi] Going to teardown the Test ID: {}", test->Id());
        iter = dying_tests.erase(iter);
        // or we can exit after tests are done.
      } else {
        cnt++;
        iter++;
      }
    }
  }

  // wait sender
  this->StopAll();
}

void IperfBatsApi::EnterSenderIntervalLoop() {
  // 5. watch the test interval and print the report.
  auto iperf_test = test_tbl_.begin()->second;
  while (stop_signal_value != SIGINT) {
    std::this_thread::sleep_for(std::chrono::milliseconds((1000 / default_resolution)));
    if (iperf_test->TestFinishState()) {
      iperf_test->OnSentFinished();
      spdlog::info("[IperfBatsApi] IperfTest sender exit..");
      break;
    }
  }

  // 6. ROLE_SENDER wait for TEST_DONE signal.
  int max_wait_time = 15;  // 3 seconds
  while (max_wait_time > 0) {
    if (stop_signal_value == SIGINT) {
      break;
    }
    if (iperf_test->GetState() == TestState::TEST_DONE && iperf_test->IsStreamDone()) {
      break;
    }
    spdlog::info("[IperfBatsApi] wait state {}, stream done {}", static_cast<int>(iperf_test->GetState()),
                 iperf_test->IsStreamDone());
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    max_wait_time--;
  }

  if (max_wait_time == 0 && iperf_test->IsStreamDone() == false) {
    spdlog::info("[IperfBatsApi] ROLE_SENDER is done with timeout on fin-ack. Test ID: {}", iperf_test->Id());
    iperf_test->PrintStreamSendSummary();
  } else {
    spdlog::info("[IperfBatsApi] ROLE_SENDER is done. Test ID: {}", iperf_test->Id());
  }
  this->StopAll();
}

void IperfBatsApi::StopAll() {
  if (is_stopped_.exchange(true)) {
    // may invoked by multiple threads; prevent meaningless multiple calls.
    return;
  }
  spdlog::info("[IperfBatsApi] Iperf test is going to tear down. Current time: {}", get_time_in_ms());
  stop_signal_value = SIGINT;
  for (auto& [_, test] : test_tbl_) {
    test->Stop();
  }
  spdlog::info("[IperfBatsApi] All Iperf tests were stopped");
}

void IperfBatsApi::PrintRecvSummaryHeader() {
  std::cout << recv_report_bw_udp_header;
  if (log_stream_.is_open()) {
    log_stream_ << recv_report_bw_udp_header;
    log_stream_ << std::flush;
  }
}

void IperfBatsApi::PrintSentSummaryHeader() {
  std::cout << send_report_bw_udp_header;
  if (log_stream_.is_open()) {
    log_stream_ << send_report_bw_udp_header;
    log_stream_ << std::flush;
  }
}

void IperfBatsApi::PrintReceiverInfo() {
  std::stringstream ss_info;
  InitVer(ver_info_);
  ss_info << ver_info_;
  ss_info << "-----------------------------------------------------------\n"
          << "Server listening on " << server_listen_port_ << " (signaling channel)\n"
          << "-----------------------------------------------------------\n";
  std::cout << ss_info.str();
  if (log_stream_.is_open()) {
    log_stream_ << ss_info.str();
    log_stream_ << std::flush;
  }
}

void IperfBatsApi::PrintSenderInfo() {
  auto& iperf_test = test_tbl_.begin()->second;
  const auto& config = iperf_test->GetConfig();
  std::stringstream ss_info;
  InitVer(ver_info_);
  ss_info << ver_info_;
  ss_info << "-----------------------------------------------------------\n"
          << "Connecting to host " << config.peer_host << ", port " << config.port - 1 << "\n"
          << "-----------------------------------------------------------\n";
  std::cout << ss_info.str();
  if (log_stream_.is_open()) {
    log_stream_ << ss_info.str();
    log_stream_ << std::flush;
  }
}

void IperfBatsApi::PrintReceiverFailure() {
  std::stringstream ss_info;
  InitVer(ver_info_);
  ss_info << ver_info_;
  ss_info << "Server failed to listen on " << server_listen_port_ << " (signaling channel)\n";
  std::cout << ss_info.str();
  if (log_stream_.is_open()) {
    log_stream_ << ss_info.str();
    log_stream_ << std::flush;
  }
}

void IperfBatsApi::PrintSenderFailure() {
  auto& iperf_test = test_tbl_.begin()->second;
  const auto& config = iperf_test->GetConfig();
  std::stringstream ss_info;
  InitVer(ver_info_);
  ss_info << ver_info_;
  ss_info << "Client failed to connect to host " << config.peer_host << ", port " << config.port - 1 << "\n";
  std::cout << ss_info.str();
  if (log_stream_.is_open()) {
    log_stream_ << ss_info.str();
    log_stream_ << std::flush;
  }
}
