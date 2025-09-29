/**
 * @file bats_server_c_example.c
 * @author Lei Peng (peng.lei@n-hop.com)
 * @brief Server example for BATS protocol using C API.
 * @version 1.0.0
 * @date 2025-03-25
 *
 * Copyright (c) 2025 The n-hop technologies Limited. All Rights Reserved.
 *
 */
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "include/c/c_bats_api.h"
#include "include/c/c_bats_types.h"

static int stop_signal_value = 0;
static bool is_stopped = false;

void signal_handler(int signum) { stop_signal_value = signum; }

bool bats_connection_callback(bats_connection_handle_t conn, bats_conn_event_t event, const unsigned char* data,
                              int length, void* user_data) {
  static int recv_cnt = 0;
  switch (event) {
    case bats_connection_established:
      printf("[bats_server_example] Connection established\n");
      break;
    case bats_connection_data_received:
      printf("[bats_server_example] Data received: len %d SEQ %d\n", length, recv_cnt++);
      bats_connection_send_data(conn, data, length);
      break;
    case bats_connection_writable:
      printf("[bats_server_example] Connection writable\n");
      break;
    case bats_connection_buffer_full:
      printf("[bats_server_example] Connection buffer full\n");
      break;
    case bats_connection_send_complete:
      printf("[bats_server_example] Send complete\n");
      break;
    case bats_connection_shutdown_by_peer:
      printf("[bats_server_example] Connection shutdown by peer\n");
      is_stopped = true;
      break;
    case bats_connection_closed:
      printf("[bats_server_example] Connection closed\n");
      is_stopped = true;
      break;
    default:
      printf("[bats_server_example] Unknown connection event %d\n", (int)event);
      break;
  }
  return true;
}

void bats_listener_callback(bats_connection_handle_t conn, bats_listen_event_t event, void* user_data) {
  switch (event) {
    case bats_listen_new_connection:
      printf("[bats_server_example] New connection accepted\n");
      bats_connection_set_callback(conn, bats_connection_callback, user_data);
      break;
    case bats_listen_failed:
      printf("[bats_server_example] Failed to listen\n");
      break;
    case bats_listen_accepted_error:
      printf("[bats_server_example] Failed to accept connection\n");
      break;
    case bats_listen_success:
      printf("[bats_server_example] Listening successfully\n");
      break;
    default:
      printf("[bats_server_example] Unknown listen event %d\n", (int)event);
      break;
  }
}

int main(int argc, char* argv[]) {
  if (argc > 4 || argc < 3) {
    printf("[bats_server_example] Invalid arguments; Usage: %s <cert_file> <key_file> <mode>\n", argv[0]);
    return -1;
  }
  char* cert_file = argv[1];
  char* key_file = argv[2];
  int mode = 0;
  if (argc == 4) {
    mode = atoi(argv[3]);
  }
  bats_context_handle_t ctx = bats_context_create();
  if (!ctx) {
    printf("[bats_server_example] Failed to create BATS context\n");
    return -1;
  }
  bats_context_set_log_level(ctx, bats_log_level_info);
  bats_context_set_signal_callback(ctx, signal_handler);

  bats_config_handle_t config = bats_config_create();
  if (!config) {
    printf("[bats_server_example] Failed to create BATS configuration\n");
    bats_context_destroy(ctx);
    return -1;
  }

  // Set configuration parameters
  bats_config_set_mode(config, mode);  // 11
  bats_config_set_local_port(config, 12345);
  bats_config_set_timeout(config, 2);
  bats_config_set_cc(config, bats_cc_gcc);
  bats_config_cert_file(config, cert_file);
  bats_config_key_file(config, key_file);

  bats_protocol_handle_t protocol = bats_protocol_create(ctx, config);
  if (!protocol) {
    printf("Failed to create BATS protocol\n");
    bats_config_destroy(config);
    bats_context_destroy(ctx);
    return -1;
  }

  // Set up listener callback
  bats_error_t err = bats_protocol_start_listen(protocol, "127.0.0.1", 12345, bats_listener_callback, NULL);
  if (err != bats_error_none) {
    printf("[bats_server_example] Failed to start listening: %d\n", err);
    bats_protocol_destroy(protocol);
    bats_config_destroy(config);
    bats_context_destroy(ctx);
    return -1;
  }

  printf("Listening on port 12345...\n");
  printf("Ctrl+C to exit.\n");
  while (stop_signal_value != SIGINT && is_stopped == false) {
    sleep(1);
  }
  printf("exiting...\n");
  // bats_context_set_signal_callback

  // Clean up
  bats_protocol_destroy(protocol);
  bats_config_destroy(config);
  bats_context_destroy(ctx);
  return 0;
}
