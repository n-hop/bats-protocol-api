# overview

- Protocol (API)
  - Server API
  - Client API
- Connection (API)
  - Send/Receive

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

