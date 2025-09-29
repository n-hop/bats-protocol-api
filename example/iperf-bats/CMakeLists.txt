set(BATS_IPERF_MAJOR_VERSION 1)
set(BATS_IPERF_MINOR_VERSION 0)
set(BATS_IPERF_VERSION
    "v${BATS_IPERF_MAJOR_VERSION}.${BATS_IPERF_MINOR_VERSION}"
)

include(GetGitRevisionDescription)
include(GetCompilerInfoString)
get_git_head_revision(GIT_VERSION, GIT_SHA1)
get_compiler_info_string(COMPILER_INFO)
string(SUBSTRING ${GIT_SHA1} 0 7 GIT_SHA1_SHORT)
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/iperf_version.h.in
  ${CMAKE_CURRENT_SOURCE_DIR}/iperf_version.h
)
add_definitions(-DDATA_COMPRESSION_ENABLED)

include(FetchContent)
include(ExternalProject)
FetchContent_Declare(
  density
  GIT_REPOSITORY https://github.com/g1mv/density.git
  GIT_TAG density-0.14.2
  GIT_PROGRESS TRUE
  FETCHCONTENT_QUIET FALSE
)
FetchContent_MakeAvailable(density)
message("density source directory is :" ${density_SOURCE_DIR})
include_directories(${density_SOURCE_DIR}/src/)

ExternalProject_Add(
  density_ep
  SOURCE_DIR ${density_SOURCE_DIR}
  CONFIGURE_COMMAND "" # density uses plain Makefiles
  BUILD_COMMAND ${CMAKE_MAKE_PROGRAM} -C ${density_SOURCE_DIR} -j
  BUILD_IN_SOURCE 1
  INSTALL_COMMAND "" # <-- disable "make install" (no install target)
)
set(DENSITY_LIB_PATH
    ${density_SOURCE_DIR}/build/libdensity.a
    CACHE PATH "Path to libdensity.a produced by density"
)

add_library(density::density STATIC IMPORTED GLOBAL)
set_target_properties(
  density::density
  PROPERTIES IMPORTED_LOCATION "${DENSITY_LIB_PATH}"
             INTERFACE_INCLUDE_DIRECTORIES "${density_SOURCE_DIR}/src"
             INTERPROCEDURAL_OPTIMIZATION OFF
)

add_dependencies(density::density density_ep)

add_executable(
  bats_iperf
  iperf.cc
  iperf_bats_api.cc
  iperf_test.cc
  iperf_stream.cc
  iperf_file_transfer.cc
  iperf_common.cc
  iperf_test_api.cc
  iperf_compress_api.cc
)
target_link_libraries(bats_iperf PRIVATE bats::protocol density::density)
set_target_properties(bats_iperf PROPERTIES INTERPROCEDURAL_OPTIMIZATION OFF)
