cmake_minimum_required(VERSION 3.10)
project(project-peregrine VERSION 0.1 LANGUAGES CXX C)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# -----------------------
# Options
# -----------------------
option(PEREGRINE_BUILD_TESTS "Build unit tests" OFF)
option(PEREGRINE_BUILD_STRESS_TESTS "Build performance stress tests" OFF)
option(PEREGRINE_STATIC_LINKING "Link libraries statically" OFF)
option(PEREGRINE_ENABLE_SANITIZERS "Enable Address/Undefined sanitizers (only in Debug)" OFF)
option(PEREGRINE_USE_CLANG_TIDY "Run clang-tidy during builds if available" OFF)
option(PEREGRINE_AF_XDP "Enable AF_XDP kernel bypass networking support" OFF)
option(PEREGRINE_ENABLE_PROFILING "Enable profiling support (frame pointers, symbols)" ON)
option(PEREGRINE_IPERF3 "Enable iperf3 support for benchmarking" OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build." FORCE)
endif()

# Set Release optimization to -O3
if(CMAKE_BUILD_TYPE STREQUAL "Release")
  set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG" CACHE STRING "Release CXX flags")
  set(CMAKE_C_FLAGS_RELEASE "-O3 -DNDEBUG" CACHE STRING "Release C flags")
endif()

# Note: cxx_std_20 has been set somewhere below as well
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# -----------------------
# Platform Detection
# -----------------------
if(UNIX AND NOT APPLE)
  set(LINUX TRUE)
endif()

# -----------------------
# Global warnings and includes
# -----------------------
add_compile_options(-Wall -Wextra -Wpedantic -Wno-interference-size)

# Add color to g++ output
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fdiagnostics-color=always -Wno-unused-result")
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# Profiling support: enable frame pointers and symbols for perf/flamegraph
if(PEREGRINE_ENABLE_PROFILING)
  add_compile_options(-fno-omit-frame-pointer)
  if(NOT WIN32)
    add_compile_options(-g)
  endif()
endif()

if(PEREGRINE_USE_CLANG_TIDY)
  find_program(CLANG_TIDY_EXE NAMES "clang-tidy")
  if(CLANG_TIDY_EXE)
    set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_EXE}")
  else()
    message(WARNING "clang-tidy requested but not found.")
  endif()
endif()

# -----------------------
# Dependencies
# -----------------------
find_package(Threads REQUIRED)

# AF_XDP support (Linux only)
if(PEREGRINE_AF_XDP AND NOT LINUX)
  message(FATAL_ERROR "AF_XDP is only supported on Linux")
endif()

if(PEREGRINE_AF_XDP)
  find_package(PkgConfig)
  if(PkgConfig_FOUND)
    pkg_check_modules(LIBXDP QUIET libxdp)
    if(LIBXDP_FOUND)
      message(STATUS "libxdp found: ${LIBXDP_LIBRARIES}")
    else()
      message(WARNING "libxdp not found. AF_XDP support disabled.")
      set(PEREGRINE_AF_XDP OFF)
    endif()
  else()
    message(WARNING "PkgConfig not found. AF_XDP support disabled.")
    set(PEREGRINE_AF_XDP OFF)
  endif()
endif()

# iperf3 support
if(PEREGRINE_IPERF3)
  find_library(IPERF3_LIBRARY NAMES iperf3)
  find_path(IPERF3_INCLUDE NAMES iperf_api.h)
  if(NOT IPERF3_LIBRARY OR NOT IPERF3_INCLUDE)
    message(WARNING "iperf3 not found. Disabling iperf3 support.")
    set(PEREGRINE_IPERF3 OFF)
  endif()
endif()

# -----------------------
# Sources
# -----------------------
file(GLOB_RECURSE ALL_SRC CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
)

# exclude client/server main files from core library
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
)

target_compile_features(peregrine PUBLIC cxx_std_20)
target_link_libraries(peregrine PUBLIC Threads::Threads)

# AF_XDP linking
if(PEREGRINE_AF_XDP)
  target_link_libraries(peregrine PUBLIC ${LIBXDP_LIBRARIES})
  target_include_directories(peregrine PRIVATE ${LIBXDP_INCLUDE_DIRS})
  target_compile_definitions(peregrine PUBLIC PEREGRINE_AF_XDP_ENABLED)
endif()

# Define macros for build type and testing
if(PEREGRINE_BUILD_TESTS)
  target_compile_definitions(peregrine PUBLIC PEREGRINE_TESTING)
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  target_compile_definitions(peregrine PUBLIC PEREGRINE_DEBUG)
else()
  target_compile_definitions(peregrine PUBLIC PEREGRINE_RELEASE)
endif()

if(PEREGRINE_ENABLE_SANITIZERS AND CMAKE_BUILD_TYPE STREQUAL "Debug")
  target_compile_options(peregrine PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(peregrine PRIVATE -fsanitize=address,undefined)
endif()

# -----------------------
# Client Executable
# -----------------------
if(EXISTS "${CLIENT_MAIN}")
  add_executable(peregrine-client "${CLIENT_MAIN}")
  target_link_libraries(peregrine-client PRIVATE peregrine Threads::Threads)
  
  # iperf3 linking for client benchmarking
  if(PEREGRINE_IPERF3 AND IPERF3_LIBRARY)
    target_link_libraries(peregrine-client PRIVATE ${IPERF3_LIBRARY})
    target_include_directories(peregrine-client PRIVATE ${IPERF3_INCLUDE})
    target_compile_definitions(peregrine-client PRIVATE PEREGRINE_IPERF3_ENABLED)
  endif()

  if(PEREGRINE_STATIC_LINKING)
    target_link_options(peregrine-client PRIVATE -static -static-libgcc -static-libstdc++)
  endif()

  if(PEREGRINE_ENABLE_SANITIZERS AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(peregrine-client PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(peregrine-client PRIVATE -fsanitize=address,undefined)
  endif()
else()
  message(STATUS "client_main.cpp not found. Client executable will not be built.")
endif()

# -----------------------
# Server Executable
# -----------------------
if(EXISTS "${SERVER_MAIN}")
  add_executable(peregrine-server "${SERVER_MAIN}")
  target_link_libraries(peregrine-server PRIVATE peregrine Threads::Threads)
  
  # iperf3 linking for server benchmarking
  if(PEREGRINE_IPERF3 AND IPERF3_LIBRARY)
    target_link_libraries(peregrine-server PRIVATE ${IPERF3_LIBRARY})
    target_include_directories(peregrine-server PRIVATE ${IPERF3_INCLUDE})
    target_compile_definitions(peregrine-server PRIVATE PEREGRINE_IPERF3_ENABLED)
  endif()

  if(PEREGRINE_STATIC_LINKING)
    target_link_options(peregrine-server PRIVATE -static -static-libgcc -static-libstdc++)
  endif()

  if(PEREGRINE_ENABLE_SANITIZERS AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(peregrine-server PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(peregrine-server PRIVATE -fsanitize=address,undefined)
  endif()
else()
  message(STATUS "server_main.cpp not found. Server executable will not be built.")
endif()

# -----------------------
# Tests
# -----------------------
include(CTest)
if(PEREGRINE_BUILD_TESTS AND NOT CMAKE_BUILD_TYPE STREQUAL "Release")
  find_package(Catch2 3 REQUIRED)

  file(GLOB_RECURSE TEST_SRCS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp")
  if(TEST_SRCS)
    add_executable(tests ${TEST_SRCS})
    target_link_libraries(tests PRIVATE peregrine Catch2::Catch2WithMain)
    if(PEREGRINE_STATIC_LINKING)
      target_link_options(tests PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
    add_test(NAME unit_tests COMMAND tests --colour-mode ansi)
    set_tests_properties(unit_tests PROPERTIES LABELS "unit")
  endif()
endif()

# -----------------------
# Stress Tests
# -----------------------
if(PEREGRINE_BUILD_STRESS_TESTS)
  find_library(JEMALLOC_LIB jemalloc)
  find_path(JEMALLOC_INCLUDE jemalloc/jemalloc.h)

  file(GLOB STRESS_SRCS "stress_tests/*.cpp")
  foreach(stress_src ${STRESS_SRCS})
    get_filename_component(test_name ${stress_src} NAME_WE)
    add_executable(${test_name} ${stress_src})
    if(JEMALLOC_LIB AND JEMALLOC_INCLUDE)
      target_include_directories(${test_name} PRIVATE ${JEMALLOC_INCLUDE})
      target_link_libraries(${test_name} PRIVATE peregrine ${JEMALLOC_LIB})
    else()
      target_link_libraries(${test_name} PRIVATE peregrine)
    endif()
    if(PEREGRINE_STATIC_LINKING)
      target_link_options(${test_name} PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
  endforeach()
endif()

# -----------------------
# Helpful messages
# -----------------------
message(STATUS "Project: ${PROJECT_NAME} ${PROJECT_VERSION}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Build tests: ${PEREGRINE_BUILD_TESTS}")
message(STATUS "AF_XDP support: ${PEREGRINE_AF_XDP}")
message(STATUS "Profiling support: ${PEREGRINE_ENABLE_PROFILING}")
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

install(
  DIRECTORY include/
  DESTINATION include
)

install(
  EXPORT peregrine-targets
  FILE peregrine-targets.cmake
  NAMESPACE peregrine::
  DESTINATION lib/cmake/peregrine
)

# Create and install config file
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/peregrine-config.cmake"
  "include(\"\${CMAKE_CURRENT_LIST_DIR}/peregrine-targets.cmake\")\n"
  "set(peregrine_FOUND TRUE)\n"
)

install(
  FILES "${CMAKE_CURRENT_BINARY_DIR}/peregrine-config.cmake"
  DESTINATION lib/cmake/peregrine
)

# Create and install version file
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/peregrine-config-version.cmake"
  "set(PACKAGE_VERSION \"${CMAKE_PROJECT_VERSION}\")\n"
  "set(PACKAGE_VERSION_COMPATIBLE TRUE)\n"
  "set(PACKAGE_VERSION_EXACT FALSE)\n"
)

install(
  FILES "${CMAKE_CURRENT_BINARY_DIR}/peregrine-config-version.cmake"
  DESTINATION lib/cmake/peregrine
)
