cmake_minimum_required(VERSION 3.16)
project(project-peregrine VERSION 0.1 LANGUAGES C CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# -----------------------
# Options
# -----------------------
set(PEREGRINE_PROFILE "dev" CACHE STRING "Project profile: dev or bench")
set_property(CACHE PEREGRINE_PROFILE PROPERTY STRINGS dev bench)
set(PROFILE "" CACHE STRING "Compatibility alias for PEREGRINE_PROFILE: dev or bench")
set_property(CACHE PROFILE PROPERTY STRINGS "" dev bench)

option(PEREGRINE_BUILD_TESTS "Build unit tests" OFF)
option(PEREGRINE_BUILD_BENCHMARKS "Build Google Benchmark benchmarks" OFF)
option(PEREGRINE_BUILD_STRESS_TESTS "Deprecated alias for PEREGRINE_BUILD_BENCHMARKS" OFF)
option(PEREGRINE_STATIC_LINKING "Link libraries statically" OFF)
option(PEREGRINE_ENABLE_SANITIZERS "Enable Address/Undefined sanitizers (Debug only)" OFF)
option(PEREGRINE_ENABLE_TSAN "Enable ThreadSanitizer (Debug only)" OFF)
option(PEREGRINE_GENERATE_COVERAGE "Enable gcov/lcov-style coverage instrumentation (Debug only)" OFF)
option(PEREGRINE_USE_CLANG_TIDY "Run clang-tidy during builds if available" ON)
option(PEREGRINE_USE_CLANG_FORMAT "Create a clang-format target" ON)
option(PEREGRINE_ENABLE_PROFILING "Enable profiling support (frame pointers, debug symbols)" ON)
option(PEREGRINE_ENABLE_LTO "Enable link-time optimization" OFF)
option(PEREGRINE_ENABLE_NATIVE_TUNING "Enable -march=native tuning" OFF)
option(PEREGRINE_USE_THREAD_PINNING "Enable CPU affinity / thread pinning logic" OFF)
option(PEREGRINE_AF_XDP "Enable AF_XDP support when libxdp is available" OFF)
option(PEREGRINE_FORCE_FETCH_DEPS "Force FetchContent for fetchable third-party dependencies" OFF)
option(PEREGRINE_NETWORK_BACKEND_AFXDP "Select AF_XDP backend at compile time" OFF)
option(PEREGRINE_NETWORK_BACKEND_SYSCALL "Select syscall socket backend at compile time" ON)
option(PEREGRINE_IPERF3 "Enable iperf3 support for network benchmarking" OFF)
option(PEREGRINE_BUILD_CLIENT "Build client binary" ON)
option(PEREGRINE_BUILD_SERVER "Build server binary" ON)
option(PEREGRINE_ENABLE_HDRHISTOGRAM "Enable HdrHistogram support for percentile reporting" ON)
option(PEREGRINE_FETCH_GOOGLE_BENCHMARK "Fetch Google Benchmark with FetchContent when not installed" ON)
option(PEREGRINE_FETCH_HDRHISTOGRAM "Fetch HdrHistogram_c with FetchContent when not installed" ON)

set(PEREGRINE_CORE_INGEST "0" CACHE STRING "Pinned core for ingest thread")
set(PEREGRINE_CORE_DECODE "1" CACHE STRING "Pinned core for decode thread")
set(PEREGRINE_CORE_MATCH "2" CACHE STRING "Pinned core for match thread")
set(PEREGRINE_CORE_LOGGER "3" CACHE STRING "Pinned core for logger thread")
set(PEREGRINE_CORE_NACK "4" CACHE STRING "Pinned core for NACK thread")

if(PEREGRINE_BUILD_STRESS_TESTS)
  set(PEREGRINE_BUILD_BENCHMARKS ON CACHE BOOL "Build Google Benchmark benchmarks" FORCE)
endif()

if(PROFILE)
  set(PEREGRINE_PROFILE "${PROFILE}" CACHE STRING "Project profile: dev or bench" FORCE)
endif()

string(TOLOWER "${PEREGRINE_PROFILE}" PEREGRINE_PROFILE_LOWER)
if(NOT PEREGRINE_PROFILE_LOWER STREQUAL "dev" AND NOT PEREGRINE_PROFILE_LOWER STREQUAL "bench")
  message(FATAL_ERROR "PEREGRINE_PROFILE must be one of: dev, bench")
endif()

if(PEREGRINE_PROFILE_LOWER STREQUAL "bench")
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build." FORCE)
  set(PEREGRINE_USE_THREAD_PINNING ON CACHE BOOL "Enable CPU affinity / thread pinning logic" FORCE)
  set(PEREGRINE_ENABLE_NATIVE_TUNING ON CACHE BOOL "Enable -march=native tuning" FORCE)
  set(PEREGRINE_ENABLE_LTO ON CACHE BOOL "Enable link-time optimization" FORCE)
  set(PEREGRINE_NETWORK_BACKEND_AFXDP ${PEREGRINE_AF_XDP} CACHE BOOL "Select AF_XDP backend at compile time" FORCE)
  if(PEREGRINE_AF_XDP)
    set(PEREGRINE_NETWORK_BACKEND_SYSCALL OFF CACHE BOOL "Select syscall socket backend at compile time" FORCE)
  else()
    set(PEREGRINE_NETWORK_BACKEND_SYSCALL ON CACHE BOOL "Select syscall socket backend at compile time" FORCE)
  endif()
else()
  set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Choose the type of build." FORCE)
  set(PEREGRINE_USE_THREAD_PINNING OFF CACHE BOOL "Enable CPU affinity / thread pinning logic" FORCE)
  set(PEREGRINE_ENABLE_LTO OFF CACHE BOOL "Enable link-time optimization" FORCE)
  set(PEREGRINE_NETWORK_BACKEND_AFXDP ${PEREGRINE_AF_XDP} CACHE BOOL "Select AF_XDP backend at compile time" FORCE)
  if(PEREGRINE_AF_XDP)
    set(PEREGRINE_NETWORK_BACKEND_SYSCALL OFF CACHE BOOL "Select syscall socket backend at compile time" FORCE)
  else()
    set(PEREGRINE_NETWORK_BACKEND_SYSCALL ON CACHE BOOL "Select syscall socket backend at compile time" FORCE)
  endif()
endif()

if(PEREGRINE_PROFILE_LOWER STREQUAL "bench" OR NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
  set(PEREGRINE_BUILD_TESTS OFF CACHE BOOL "Build unit tests" FORCE)
endif()

if(PEREGRINE_NETWORK_BACKEND_AFXDP AND PEREGRINE_NETWORK_BACKEND_SYSCALL)
  message(FATAL_ERROR "Select exactly one network backend: AF_XDP or syscall")
endif()

if(NOT PEREGRINE_NETWORK_BACKEND_AFXDP AND NOT PEREGRINE_NETWORK_BACKEND_SYSCALL)
  message(FATAL_ERROR "At least one network backend must be enabled")
endif()

if(PEREGRINE_ENABLE_SANITIZERS AND PEREGRINE_ENABLE_TSAN)
  message(FATAL_ERROR "Enable at most one sanitizer mode")
endif()

if(PEREGRINE_GENERATE_COVERAGE AND (PEREGRINE_ENABLE_SANITIZERS OR PEREGRINE_ENABLE_TSAN))
  message(FATAL_ERROR "Coverage, ASan, and TSan builds must be separate")
endif()

if((PEREGRINE_ENABLE_SANITIZERS OR PEREGRINE_ENABLE_TSAN) AND NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
  message(FATAL_ERROR "Sanitizers are only supported in Debug builds")
endif()

if(PEREGRINE_GENERATE_COVERAGE AND NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
  message(FATAL_ERROR "Coverage is only supported in Debug builds")
endif()

# -----------------------
# Language setup
# -----------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(UNIX AND NOT APPLE)
  set(PEREGRINE_LINUX TRUE)
endif()

# -----------------------
# Global compile options
# -----------------------
add_compile_options(-Wall -Wextra -Wpedantic -Wno-interference-size)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-fdiagnostics-color=always -Wno-unused-result)
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Release")
  set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG" CACHE STRING "Release CXX flags" FORCE)
  set(CMAKE_C_FLAGS_RELEASE "-O3 -DNDEBUG" CACHE STRING "Release C flags" FORCE)
else()
  set(CMAKE_CXX_FLAGS_DEBUG "-O2 -g" CACHE STRING "Debug CXX flags" FORCE)
  set(CMAKE_C_FLAGS_DEBUG "-O2 -g" CACHE STRING "Debug C flags" FORCE)
endif()

if(PEREGRINE_ENABLE_PROFILING)
  add_compile_options(-fno-omit-frame-pointer)
  if(NOT WIN32)
    add_compile_options(-g)
  endif()
endif()

if(PEREGRINE_ENABLE_NATIVE_TUNING AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-march=native)
endif()

if(PEREGRINE_GENERATE_COVERAGE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(--coverage)
  add_link_options(--coverage)
endif()

include(CheckIPOSupported)
include(FetchContent)
include(ExternalProject)
if(PEREGRINE_ENABLE_LTO)
  check_ipo_supported(RESULT PEREGRINE_IPO_SUPPORTED OUTPUT PEREGRINE_IPO_ERROR)
  if(PEREGRINE_IPO_SUPPORTED)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
  else()
    message(WARNING "LTO requested but not supported: ${PEREGRINE_IPO_ERROR}")
  endif()
endif()

if(PEREGRINE_USE_CLANG_TIDY)
  find_program(CLANG_TIDY_EXE NAMES clang-tidy)
  if(CLANG_TIDY_EXE)
    set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_EXE}")
  else()
    message(WARNING "clang-tidy requested but not found")
  endif()
endif()

if(PEREGRINE_USE_CLANG_FORMAT)
  find_program(CLANG_FORMAT_EXE NAMES clang-format)
  if(CLANG_FORMAT_EXE)
    file(GLOB_RECURSE PEREGRINE_FORMAT_FILES
      CONFIGURE_DEPENDS
      "${CMAKE_CURRENT_SOURCE_DIR}/src/*.[ch]"
      "${CMAKE_CURRENT_SOURCE_DIR}/src/*.[ch]pp"
      "${CMAKE_CURRENT_SOURCE_DIR}/include/*.[ch]"
      "${CMAKE_CURRENT_SOURCE_DIR}/include/*.[ch]pp"
    )
    add_custom_target(format
      COMMAND "${CLANG_FORMAT_EXE}" -i ${PEREGRINE_FORMAT_FILES}
      COMMENT "Running clang-format on src/ and include/"
    )
  else()
    message(WARNING "clang-format target requested but clang-format was not found")
  endif()
endif()

function(peregrine_log_system_package package_name version_value)
  if(version_value)
    message(STATUS "${package_name} found: ${version_value}")
  else()
    message(STATUS "${package_name} found")
  endif()
endfunction()

# -----------------------
# Dependencies
# -----------------------
find_package(Threads REQUIRED)

if(PEREGRINE_AF_XDP AND NOT PEREGRINE_LINUX)
  message(FATAL_ERROR "AF_XDP is only supported on Linux")
endif()

set(PEREGRINE_EXTRA_LIBS "")
set(PEREGRINE_EXTRA_INCLUDE_DIRS "")
set(PEREGRINE_AF_XDP_AVAILABLE OFF)
set(PEREGRINE_HDRHISTOGRAM_TARGET "")
set(PEREGRINE_IPERF3_TARGET "")

if(PEREGRINE_AF_XDP)
  find_package(PkgConfig QUIET)
  if(NOT PkgConfig_FOUND)
    message(FATAL_ERROR "AF_XDP was requested, but pkg-config was not found. Install pkg-config, libxdp, and libbpf development packages.")
  endif()

  pkg_check_modules(LIBXDP QUIET libxdp)
  pkg_check_modules(LIBBPF QUIET libbpf)

  if(NOT LIBXDP_FOUND OR NOT LIBBPF_FOUND)
    message(FATAL_ERROR "AF_XDP was requested, but libxdp and libbpf were not both found. Install the libxdp and libbpf development packages, or disable PEREGRINE_AF_XDP.")
  endif()

  list(APPEND PEREGRINE_EXTRA_LIBS ${LIBXDP_LIBRARIES} ${LIBBPF_LIBRARIES})
  list(APPEND PEREGRINE_EXTRA_INCLUDE_DIRS ${LIBXDP_INCLUDE_DIRS} ${LIBBPF_INCLUDE_DIRS})
  set(PEREGRINE_AF_XDP_AVAILABLE ON)
  message(STATUS "libxdp found: ${LIBXDP_VERSION}")
  message(STATUS "libbpf found: ${LIBBPF_VERSION}")
endif()

if(PEREGRINE_ENABLE_HDRHISTOGRAM)
  if(NOT PEREGRINE_FORCE_FETCH_DEPS)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
      pkg_check_modules(HDRHISTOGRAM QUIET hdr_histogram)
    endif()
  endif()

  if(HDRHISTOGRAM_FOUND)
    peregrine_log_system_package("HdrHistogram" "${HDRHISTOGRAM_VERSION}")
    add_library(peregrine_hdrhistogram INTERFACE)
    target_include_directories(peregrine_hdrhistogram INTERFACE ${HDRHISTOGRAM_INCLUDE_DIRS})
    target_link_libraries(peregrine_hdrhistogram INTERFACE ${HDRHISTOGRAM_LIBRARIES})
    set(PEREGRINE_HDRHISTOGRAM_TARGET peregrine_hdrhistogram)
  elseif(NOT PEREGRINE_FORCE_FETCH_DEPS)
    find_path(HDR_HISTOGRAM_INCLUDE_DIR NAMES hdr/hdr_histogram.h)
    find_library(HDR_HISTOGRAM_LIBRARY NAMES hdr_histogram)

    if(HDR_HISTOGRAM_INCLUDE_DIR AND HDR_HISTOGRAM_LIBRARY)
      message(STATUS "HdrHistogram found in the system library path")
      add_library(peregrine_hdrhistogram INTERFACE)
      target_include_directories(peregrine_hdrhistogram INTERFACE "${HDR_HISTOGRAM_INCLUDE_DIR}")
      target_link_libraries(peregrine_hdrhistogram INTERFACE "${HDR_HISTOGRAM_LIBRARY}")
      set(PEREGRINE_HDRHISTOGRAM_TARGET peregrine_hdrhistogram)
    endif()
  endif()

  if(NOT PEREGRINE_HDRHISTOGRAM_TARGET)
    if(PEREGRINE_FETCH_HDRHISTOGRAM)
      message(STATUS "Fetching HdrHistogram_c v0.11.8")
      FetchContent_Declare(
        hdr_histogram
        GIT_REPOSITORY https://github.com/HdrHistogram/HdrHistogram_c.git
        GIT_TAG 0.11.8
        GIT_SHALLOW TRUE
      )
      set(HDR_HISTOGRAM_BUILD_SHARED OFF CACHE BOOL "" FORCE)
      set(HDR_HISTOGRAM_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
      set(HDR_LOG_REQUIRED OFF CACHE BOOL "" FORCE)
      FetchContent_MakeAvailable(hdr_histogram)
      if(TARGET hdr_histogram_static)
        set(PEREGRINE_HDRHISTOGRAM_TARGET hdr_histogram_static)
      elseif(TARGET hdr_histogram)
        set(PEREGRINE_HDRHISTOGRAM_TARGET hdr_histogram)
      endif()
    else()
      message(FATAL_ERROR "HdrHistogram is enabled, but no system package was found and FetchContent is disabled. Install hdr_histogram or enable PEREGRINE_FETCH_HDRHISTOGRAM.")
    endif()
  endif()
endif()

# -----------------------
# Palloc
# -----------------------
message(STATUS "Fetching Palloc")
set(PALLOC_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(PALLOC_BUILD_STRESS_TESTS OFF CACHE BOOL "" FORCE)
set(PALLOC_USE_CLANG_TIDY OFF CACHE BOOL "" FORCE)
set(PALLOC_SINGLE_THREADED OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  palloc
  GIT_REPOSITORY https://github.com/aaslam12/Palloc.git
  GIT_TAG main
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(palloc)

set(PEREGRINE_BENCHMARK_TARGETS "")
if(PEREGRINE_BUILD_BENCHMARKS)
  if(NOT PEREGRINE_FORCE_FETCH_DEPS)
    find_package(benchmark QUIET)
  endif()
  if(benchmark_FOUND)
    peregrine_log_system_package("Google Benchmark" "${benchmark_VERSION}")
    set(PEREGRINE_BENCHMARK_TARGETS benchmark::benchmark benchmark::benchmark_main)
  elseif(PEREGRINE_FETCH_GOOGLE_BENCHMARK)
    message(STATUS "Fetching Google Benchmark v1.9.5")
    set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_DOWNLOAD_DEPENDENCIES OFF CACHE BOOL "" FORCE)
    set(CMAKE_CXX_CLANG_TIDY "")
    FetchContent_Declare(
      google_benchmark
      GIT_REPOSITORY https://github.com/google/benchmark.git
      GIT_TAG v1.9.5
      GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(google_benchmark)
    if(CLANG_TIDY_EXE)
      set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_EXE}")
    endif()
    set(PEREGRINE_BENCHMARK_TARGETS benchmark::benchmark benchmark::benchmark_main)
  else()
    message(FATAL_ERROR "Google Benchmark is enabled, but no system package was found and FetchContent is disabled. Install benchmark or enable PEREGRINE_FETCH_GOOGLE_BENCHMARK.")
  endif()
endif()

if(PEREGRINE_BUILD_TESTS)
  if(NOT PEREGRINE_FORCE_FETCH_DEPS)
    find_package(Catch2 3 QUIET)
  endif()
  if(Catch2_FOUND)
    peregrine_log_system_package("Catch2" "${Catch2_VERSION}")
    set(PEREGRINE_CATCH2_TARGET Catch2::Catch2WithMain)
  elseif(NOT PEREGRINE_CATCH2_TARGET)
    message(STATUS "Fetching Catch2 v3.15.0")
    FetchContent_Declare(
      catch2
      GIT_REPOSITORY https://github.com/catchorg/Catch2.git
      GIT_TAG v3.15.0
      GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(catch2)
    set(PEREGRINE_CATCH2_TARGET Catch2::Catch2WithMain)
    if(NOT PEREGRINE_CATCH2_TARGET)
      message(FATAL_ERROR "Catch2 fetch completed but no usable target was created")
    endif()
  else()
    message(FATAL_ERROR "Unit tests are enabled, but Catch2 was not found and FetchContent did not provide a usable target.")
  endif()
endif()

if(PEREGRINE_IPERF3)
  if(NOT PEREGRINE_FORCE_FETCH_DEPS)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
      pkg_check_modules(LIBIPERF QUIET libiperf)
    endif()
  endif()

  if(LIBIPERF_FOUND)
    peregrine_log_system_package("libiperf" "${LIBIPERF_VERSION}")
    add_library(peregrine_iperf3 INTERFACE)
    target_include_directories(peregrine_iperf3 INTERFACE ${LIBIPERF_INCLUDE_DIRS})
    target_link_libraries(peregrine_iperf3 INTERFACE ${LIBIPERF_LIBRARIES})
    set(PEREGRINE_IPERF3_TARGET peregrine_iperf3)
  elseif(NOT PEREGRINE_FORCE_FETCH_DEPS)
    find_path(IPERF3_INCLUDE_DIR NAMES iperf_api.h)
    find_library(IPERF3_LIBRARY NAMES iperf libiperf)
    if(IPERF3_INCLUDE_DIR AND IPERF3_LIBRARY)
      message(STATUS "libiperf found in the system library path")
      add_library(peregrine_iperf3 INTERFACE)
      target_include_directories(peregrine_iperf3 INTERFACE "${IPERF3_INCLUDE_DIR}")
      target_link_libraries(peregrine_iperf3 INTERFACE "${IPERF3_LIBRARY}")
      set(PEREGRINE_IPERF3_TARGET peregrine_iperf3)
    endif()
  endif()

  if(NOT PEREGRINE_IPERF3_TARGET)
    message(STATUS "Fetching iperf3 v3.21")
    set(PEREGRINE_IPERF3_INSTALL_DIR "${CMAKE_BINARY_DIR}/_deps/iperf3-install")
    file(MAKE_DIRECTORY "${PEREGRINE_IPERF3_INSTALL_DIR}/include")
    file(MAKE_DIRECTORY "${PEREGRINE_IPERF3_INSTALL_DIR}/lib")
    ExternalProject_Add(iperf3_external
      URL https://downloads.es.net/pub/iperf/iperf-3.21.tar.gz
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE
      CONFIGURE_COMMAND <SOURCE_DIR>/configure --prefix=<INSTALL_DIR> --disable-shared --enable-static
      BUILD_COMMAND make
      INSTALL_COMMAND make install
      INSTALL_DIR "${PEREGRINE_IPERF3_INSTALL_DIR}"
      BUILD_BYPRODUCTS "${PEREGRINE_IPERF3_INSTALL_DIR}/lib/libiperf.a"
    )
    add_library(peregrine_iperf3 STATIC IMPORTED GLOBAL)
    set_target_properties(peregrine_iperf3 PROPERTIES
      IMPORTED_LOCATION "${PEREGRINE_IPERF3_INSTALL_DIR}/lib/libiperf.a"
      INTERFACE_INCLUDE_DIRECTORIES "${PEREGRINE_IPERF3_INSTALL_DIR}/include"
    )
    add_dependencies(peregrine_iperf3 iperf3_external)
    set(PEREGRINE_IPERF3_TARGET peregrine_iperf3)
  endif()
endif()

# -----------------------
# Sources
# -----------------------
file(GLOB_RECURSE ALL_SRC CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
)

set(CLIENT_MAIN "${CMAKE_CURRENT_SOURCE_DIR}/src/client_main.cpp")
set(SERVER_MAIN "${CMAKE_CURRENT_SOURCE_DIR}/src/server_main.cpp")
list(FILTER ALL_SRC EXCLUDE REGEX ".*/client_main.cpp$")
list(FILTER ALL_SRC EXCLUDE REGEX ".*/server_main.cpp$")

# -----------------------
# Core library
# -----------------------
add_library(peregrine STATIC ${ALL_SRC})

target_include_directories(peregrine
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
  PRIVATE
    ${PEREGRINE_EXTRA_INCLUDE_DIRS}
)

target_compile_features(peregrine PUBLIC cxx_std_20)
target_link_libraries(peregrine PUBLIC Threads::Threads palloc ${PEREGRINE_EXTRA_LIBS})
if(PEREGRINE_HDRHISTOGRAM_TARGET)
  target_link_libraries(peregrine PUBLIC ${PEREGRINE_HDRHISTOGRAM_TARGET})
endif()

target_compile_definitions(peregrine
  PUBLIC
    $<$<CONFIG:Debug>:PEREGRINE_DEBUG>
    $<$<NOT:$<CONFIG:Debug>>:PEREGRINE_RELEASE>
    $<$<BOOL:${PEREGRINE_BUILD_TESTS}>:PEREGRINE_TESTING>
    $<$<BOOL:${PEREGRINE_ENABLE_PROFILING}>:PEREGRINE_PROFILING_ENABLED>
    $<$<BOOL:${PEREGRINE_ENABLE_NATIVE_TUNING}>:PEREGRINE_NATIVE_TUNING_ENABLED>
    $<$<BOOL:${PEREGRINE_ENABLE_HDRHISTOGRAM}>:PEREGRINE_HDRHISTOGRAM_ENABLED>
    $<$<BOOL:${PEREGRINE_USE_THREAD_PINNING}>:USE_THREAD_PINNING=1>
    $<$<NOT:$<BOOL:${PEREGRINE_USE_THREAD_PINNING}>>:USE_THREAD_PINNING=0>
    $<$<BOOL:${PEREGRINE_AF_XDP_AVAILABLE}>:PEREGRINE_AF_XDP_ENABLED>
    $<$<BOOL:${PEREGRINE_NETWORK_BACKEND_AFXDP}>:NETWORK_BACKEND_AFXDP>
    $<$<BOOL:${PEREGRINE_NETWORK_BACKEND_SYSCALL}>:NETWORK_BACKEND_SYSCALL>
    PEREGRINE_PROFILE_${PEREGRINE_PROFILE_LOWER}
    CORE_INGEST=${PEREGRINE_CORE_INGEST}
    CORE_DECODE=${PEREGRINE_CORE_DECODE}
    CORE_MATCH=${PEREGRINE_CORE_MATCH}
    CORE_LOGGER=${PEREGRINE_CORE_LOGGER}
    CORE_NACK=${PEREGRINE_CORE_NACK}
)

if(PEREGRINE_ENABLE_SANITIZERS)
  add_compile_options(-fsanitize=address,undefined)
  add_link_options(-fsanitize=address,undefined)
endif()

if(PEREGRINE_ENABLE_TSAN)
  add_compile_options(-fsanitize=thread)
  add_link_options(-fsanitize=thread)
endif()

# -----------------------
# Client executable
# -----------------------
if(PEREGRINE_BUILD_CLIENT AND EXISTS "${CLIENT_MAIN}")
  add_executable(peregrine-client "${CLIENT_MAIN}")
  target_link_libraries(peregrine-client PRIVATE peregrine Threads::Threads)
  target_compile_definitions(peregrine-client PRIVATE PEREGRINE_CLIENT_BINARY)

  if(PEREGRINE_IPERF3)
    target_link_libraries(peregrine-client PRIVATE ${PEREGRINE_IPERF3_TARGET})
    target_compile_definitions(peregrine-client PRIVATE PEREGRINE_IPERF3_ENABLED)
  endif()

  if(PEREGRINE_STATIC_LINKING AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_link_options(peregrine-client PRIVATE -static -static-libgcc -static-libstdc++)
  endif()
endif()

# -----------------------
# Server executable
# -----------------------
if(PEREGRINE_BUILD_SERVER AND EXISTS "${SERVER_MAIN}")
  add_executable(peregrine-server "${SERVER_MAIN}")
  target_link_libraries(peregrine-server PRIVATE peregrine Threads::Threads)
  target_compile_definitions(peregrine-server PRIVATE PEREGRINE_SERVER_BINARY)

  if(PEREGRINE_IPERF3)
    target_link_libraries(peregrine-server PRIVATE ${PEREGRINE_IPERF3_TARGET})
    target_compile_definitions(peregrine-server PRIVATE PEREGRINE_IPERF3_ENABLED)
  endif()

  if(PEREGRINE_STATIC_LINKING AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_link_options(peregrine-server PRIVATE -static -static-libgcc -static-libstdc++)
  endif()
endif()

# -----------------------
# Tests
# -----------------------
include(CTest)
if(PEREGRINE_BUILD_TESTS)
  if(PEREGRINE_CATCH2_TARGET)
    file(GLOB_RECURSE TEST_SRCS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp")
    if(TEST_SRCS)
      add_executable(tests ${TEST_SRCS})
      target_link_libraries(tests PRIVATE peregrine ${PEREGRINE_CATCH2_TARGET})
      if(PEREGRINE_STATIC_LINKING AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_link_options(tests PRIVATE -static -static-libgcc -static-libstdc++)
      endif()
      add_test(NAME unit_tests COMMAND tests --colour-mode ansi)
      set_tests_properties(unit_tests PROPERTIES LABELS "unit")
    else()
      message(STATUS "No unit test sources found under tests/")
    endif()
  else()
    message(WARNING "Catch2 v3 not found. Unit tests will not be built.")
  endif()
endif()

# -----------------------
# Benchmarks
# -----------------------
if(PEREGRINE_BUILD_BENCHMARKS)
  if(PEREGRINE_BENCHMARK_TARGETS)
    file(GLOB_RECURSE BENCHMARK_SRCS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/*.cpp")
    if(BENCHMARK_SRCS)
      add_executable(peregrine-benchmarks ${BENCHMARK_SRCS})
      target_link_libraries(peregrine-benchmarks PRIVATE peregrine ${PEREGRINE_BENCHMARK_TARGETS})
      if(PEREGRINE_STATIC_LINKING AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_link_options(peregrine-benchmarks PRIVATE -static -static-libgcc -static-libstdc++)
      endif()
      add_test(NAME benchmarks_smoke COMMAND peregrine-benchmarks --benchmark_min_time=0.01)
      set_tests_properties(benchmarks_smoke PROPERTIES LABELS "benchmark")
    else()
      message(STATUS "No benchmark sources found under benchmarks/")
    endif()
  else()
    message(WARNING "Google Benchmark support is unavailable. Benchmarks will not be built.")
  endif()
endif()

# -----------------------
# Coverage target
# -----------------------
if(PEREGRINE_GENERATE_COVERAGE AND PEREGRINE_BUILD_TESTS)
  find_program(LCOV_EXE NAMES lcov)
  find_program(GENHTML_EXE NAMES genhtml)
  if(LCOV_EXE AND GENHTML_EXE)
    add_custom_target(coverage
      COMMAND "${LCOV_EXE}" --directory "${CMAKE_BINARY_DIR}" --zerocounters
      COMMAND ctest --output-on-failure --test-dir "${CMAKE_BINARY_DIR}"
      COMMAND "${LCOV_EXE}" --directory "${CMAKE_BINARY_DIR}" --capture --output-file coverage.info
      COMMAND "${GENHTML_EXE}" coverage.info --output-directory coverage-report
      WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
      COMMENT "Generating dev-only coverage report"
    )
  else()
    message(WARNING "Coverage requested but lcov/genhtml not found")
  endif()
endif()

# -----------------------
# Build provenance
# -----------------------
execute_process(
  COMMAND git -C "${CMAKE_CURRENT_SOURCE_DIR}" rev-parse HEAD
  OUTPUT_VARIABLE PEREGRINE_GIT_COMMIT
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
)

if(NOT PEREGRINE_GIT_COMMIT)
  set(PEREGRINE_GIT_COMMIT "unknown")
endif()

configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/build_info.json.in"
  "${CMAKE_CURRENT_BINARY_DIR}/build_info.json"
  @ONLY
)

# -----------------------
# Helpful messages
# -----------------------
message(STATUS "Project: ${PROJECT_NAME} ${PROJECT_VERSION}")
message(STATUS "Profile: ${PEREGRINE_PROFILE_LOWER}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Build tests: ${PEREGRINE_BUILD_TESTS}")
message(STATUS "Build benchmarks: ${PEREGRINE_BUILD_BENCHMARKS}")
message(STATUS "HdrHistogram support: ${PEREGRINE_ENABLE_HDRHISTOGRAM}")
message(STATUS "Force fetch deps: ${PEREGRINE_FORCE_FETCH_DEPS}")
message(STATUS "Fetch Google Benchmark: ${PEREGRINE_FETCH_GOOGLE_BENCHMARK}")
message(STATUS "Fetch HdrHistogram: ${PEREGRINE_FETCH_HDRHISTOGRAM}")
message(STATUS "AF_XDP support: ${PEREGRINE_AF_XDP}")
message(STATUS "Syscall backend: ${PEREGRINE_NETWORK_BACKEND_SYSCALL}")
message(STATUS "AF_XDP backend: ${PEREGRINE_NETWORK_BACKEND_AFXDP}")
message(STATUS "Thread pinning: ${PEREGRINE_USE_THREAD_PINNING}")
message(STATUS "Profiling support: ${PEREGRINE_ENABLE_PROFILING}")
message(STATUS "Native tuning: ${PEREGRINE_ENABLE_NATIVE_TUNING}")
message(STATUS "LTO: ${PEREGRINE_ENABLE_LTO}")
message(STATUS "Coverage: ${PEREGRINE_GENERATE_COVERAGE}")
message(STATUS "iperf3 support: ${PEREGRINE_IPERF3}")
message(STATUS "Platform: ${CMAKE_SYSTEM_NAME}")

# -----------------------
# Install rules
# -----------------------
install(
  TARGETS peregrine
  EXPORT peregrine-targets
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib
  RUNTIME DESTINATION bin
)

if(TARGET peregrine-client)
  install(TARGETS peregrine-client RUNTIME DESTINATION bin)
endif()

if(TARGET peregrine-server)
  install(TARGETS peregrine-server RUNTIME DESTINATION bin)
endif()

if(TARGET peregrine-benchmarks)
  install(TARGETS peregrine-benchmarks RUNTIME DESTINATION bin)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/scripts/perf_profile.sh")
  install(PROGRAMS "${CMAKE_CURRENT_SOURCE_DIR}/scripts/perf_profile.sh" DESTINATION bin)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/include")
  install(DIRECTORY include/ DESTINATION include)
endif()
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/build_info.json" DESTINATION share/peregrine)

install(
  EXPORT peregrine-targets
  FILE peregrine-targets.cmake
  NAMESPACE peregrine::
  DESTINATION lib/cmake/peregrine
)

file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/peregrine-config.cmake"
  "include(\"\${CMAKE_CURRENT_LIST_DIR}/peregrine-targets.cmake\")\n"
  "set(peregrine_FOUND TRUE)\n"
)

install(
  FILES "${CMAKE_CURRENT_BINARY_DIR}/peregrine-config.cmake"
  DESTINATION lib/cmake/peregrine
)

file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/peregrine-config-version.cmake"
  "set(PACKAGE_VERSION \"${CMAKE_PROJECT_VERSION}\")\n"
  "set(PACKAGE_VERSION_COMPATIBLE TRUE)\n"
  "set(PACKAGE_VERSION_EXACT FALSE)\n"
)

install(
  FILES "${CMAKE_CURRENT_BINARY_DIR}/peregrine-config-version.cmake"
  DESTINATION lib/cmake/peregrine
)
