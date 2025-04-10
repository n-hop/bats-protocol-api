cmake_minimum_required(VERSION 3.27 FATAL_ERROR)
project(
  bats_application_example
  VERSION 0.1
  LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)

# BATS Protocol Library
if(NOT CMAKE_CXX_COMPILER MATCHES "arm" AND NOT CMAKE_CXX_COMPILER MATCHES
                                            "aarch")
  set(BATS_PROTO_LIBRARY ${CMAKE_SOURCE_DIR}/lib/libbats_protocol_library.a)
else()
  message(
    FATAL_ERROR "FATAL: Not supported on ARM architecture. Please use x86_64.")
endif()
message(STATUS "BATS Protocol library: ${BATS_PROTO_LIBRARY}")
message(STATUS "CMAKE_CXX_COMPILER: ${CMAKE_CXX_COMPILER}")
message(STATUS "CMAKE_CXX_COMPILER_ID: ${CMAKE_CXX_COMPILER_ID}")
message(STATUS "CMAKE_CXX_COMPILER_VERSION: ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "CMAKE_SYSTEM_PROCESSOR: ${CMAKE_SYSTEM_PROCESSOR}")
message(STATUS "CMAKE_SYSTEM_NAME: ${CMAKE_SYSTEM_NAME}")

add_library(bats::protocol STATIC IMPORTED)
set_target_properties(
  bats::protocol
  PROPERTIES IMPORTED_LOCATION "${BATS_PROTO_LIBRARY}"
             INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/include"
             INTERFACE_LINK_LIBRARIES "pthread;dl")
# INTERFACE_COMPILE_OPTIONS "-DBATS_PROTOCOL_LIBRARY"
include_directories(${CMAKE_SOURCE_DIR}/include)

# EXAMPLE
add_executable(bats_server_example example/bats_server_example.cc)
target_link_libraries(bats_server_example PRIVATE bats::protocol)

add_executable(bats_client_example example/bats_client_example.cc)
target_link_libraries(bats_client_example PRIVATE bats::protocol)
