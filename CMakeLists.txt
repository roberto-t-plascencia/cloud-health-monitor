cmake_minimum_required(VERSION 3.20)
project(cloud_health_monitor VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(HEALTHMON_BUILD_TESTS "Build unit tests" ON)

find_package(Threads REQUIRED)
find_package(CURL REQUIRED)

include(FetchContent)
FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG        v3.11.3
  GIT_SHALLOW    TRUE
)
set(JSON_BuildTests OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(nlohmann_json)

# Core library: everything except main(), so tests link the same code.
add_library(healthmon_core
  src/config.cpp
  src/curl_prober.cpp
  src/http_server.cpp
  src/logger.cpp
  src/metrics.cpp
  src/prometheus.cpp
  src/retry.cpp
  src/scheduler.cpp
)
target_include_directories(healthmon_core PUBLIC include)
target_link_libraries(healthmon_core
  PUBLIC Threads::Threads nlohmann_json::nlohmann_json
  PRIVATE CURL::libcurl)
target_compile_options(healthmon_core PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion)

add_executable(healthmon src/main.cpp)
target_link_libraries(healthmon PRIVATE healthmon_core CURL::libcurl)
target_compile_options(healthmon PRIVATE -Wall -Wextra -Wpedantic)

install(TARGETS healthmon RUNTIME DESTINATION bin)

if(HEALTHMON_BUILD_TESTS)
  FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.15.2
    GIT_SHALLOW    TRUE
  )
  FetchContent_MakeAvailable(googletest)
  enable_testing()
  include(GoogleTest)

  add_executable(healthmon_tests
    tests/bounded_queue_test.cpp
    tests/config_test.cpp
    tests/http_server_test.cpp
    tests/metrics_test.cpp
    tests/prometheus_test.cpp
    tests/retry_test.cpp
    tests/scheduler_test.cpp
    tests/thread_pool_test.cpp
  )
  target_link_libraries(healthmon_tests PRIVATE healthmon_core CURL::libcurl GTest::gtest_main)
  gtest_discover_tests(healthmon_tests DISCOVERY_TIMEOUT 30)
endif()
