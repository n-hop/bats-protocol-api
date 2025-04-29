# Overview

- `IOContext`: The context of the IO operation, which is responsible for watching the IO events for those registered connections. In each process, it is required to have at least one `IOContext` instance and this only one IO context instance can be shared by multiple protocol instance `BatsProtocol`.
- `BatsProtocol`: The protocol instance of BATS protocol, which can start a new connection to remote server or wait for a new connection from remote client. It must be initialized with a `IOContext` instance.
- `BatsConfiguration`: The configuration interface of BATS protocol, which is used to set the transmission mode, congestion control and other parameters.
- `IBatsConnection`: The connection of BATS protocol, which is used to send and receive messages. `IBatsConnection` is managed by BATS protocol core, users can't create or destroy it directly.
- `Callback functions`: There are two callback functions in BATS protocol, `ListenCallback` is used to receive listen related events, and `ConnectionCallback` is used to receive connection related events. In most cases, the asynchronous events are emitted by `IOContext`'s thread while the direct failure of API calls are emitted by the users' thread.

## API

- C API

C API is located in `include/c` directory. Except for `bats_connection_handle_t`, all others handles which are created through `bats_XXXX_create`, should be destroyed through `bats_XXXX_destroy`. The `bats_connection_handle_t` is managed by the core library, and should not be destroyed by the user.

```cpp
  // example of creating a ctx
  bats_context_handle_t ctx = bats_context_create();
  if (!ctx) {
    printf("[bats_server_example] Failed to create BATS context\n");
    return -1;
  }
  // do something with the ctx
  // destroy the ctx
  bats_context_destroy(ctx);
```

- C++ API

C++ API is located in `include/cpp` directory. It manages all the exported objects in a RAII way. The user does not need to destroy the objects manually.

## Sample test

```bash
# step 1: generate required keys
openssl req  -nodes -new -x509  -keyout server.key -out server.cert

# step 2: run the server
./build/example/bats_server_example ./server.cert ./server.key

# step 3: run the client, and send 10 messages
./build/example/bats_client_example 10
```

bats_client_example usage:

```bash
./build/example/bats_client_example [send_count] [mode]
```

- `send_count`: number of messages to send, default is 0; and 0 means infinite. this field is optional.
- `mode`: transmission mode of current protocol, default is `0` (BTP); this field is optional.

bats_client_example usage:

```bash
./build/example/bats_server_example [cert] [key] [mode]
```

- `cert`: path to the certificate file. this field is required.
- `key`: path to the key file. this field is required.
- `mode`: transmission mode of current protocol, default is `0` (BTP); this field is optional.
