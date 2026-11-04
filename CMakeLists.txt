cmake_minimum_required(VERSION 3.16)
project(esp32_air_quality_station VERSION 1.0.0 LANGUAGES CXX)

# Host build: portable library, simulator, receiver and unit tests.
# The ESP-IDF firmware lives in firmware/ and is built with idf.py.

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(AQ_BUILD_TESTS "Build unit tests" ON)
option(AQ_BUILD_APPS "Build the host simulator and receiver" ON)
option(AQ_WERROR "Treat warnings as errors" OFF)
option(AQ_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(AQ_COVERAGE "Instrument for gcov coverage" OFF)

add_library(aq_warnings INTERFACE)
target_compile_options(aq_warnings INTERFACE -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor
                                             -Wold-style-cast -Wcast-align -Wformat=2)
if(AQ_WERROR)
  target_compile_options(aq_warnings INTERFACE -Werror)
endif()
if(AQ_SANITIZE)
  target_compile_options(aq_warnings INTERFACE -fsanitize=address,undefined -fno-sanitize-recover=all
                                               -fno-omit-frame-pointer)
  target_link_options(aq_warnings INTERFACE -fsanitize=address,undefined)
endif()
if(AQ_COVERAGE)
  target_compile_options(aq_warnings INTERFACE --coverage -O0 -g)
  target_link_options(aq_warnings INTERFACE --coverage)
endif()

# Portable core: drivers, filters, AQI, wire format, config. Also compiled by ESP-IDF.
add_library(aqcore STATIC
  lib/src/air_quality.cpp
  lib/src/backoff.cpp
  lib/src/config.cpp
  lib/src/crc.cpp
  lib/src/scd41.cpp
  lib/src/sensirion.cpp
  lib/src/sht31.cpp
  lib/src/station.cpp
  lib/src/telemetry.cpp)
target_include_directories(aqcore PUBLIC lib/include)
target_link_libraries(aqcore PRIVATE aq_warnings)

# Host-only sensor and room simulation.
add_library(aqsim STATIC sim/src/room.cpp sim/src/sim_bus.cpp)
target_include_directories(aqsim PUBLIC sim/include)
target_link_libraries(aqsim PUBLIC aqcore PRIVATE aq_warnings)

if(AQ_BUILD_APPS)
  add_executable(aq_simulator apps/simulator/main.cpp)
  target_include_directories(aq_simulator PRIVATE apps/common)
  target_link_libraries(aq_simulator PRIVATE aqsim aq_warnings)

  add_executable(aq_receiver apps/receiver/main.cpp)
  target_include_directories(aq_receiver PRIVATE apps/common)
  target_link_libraries(aq_receiver PRIVATE aqcore aq_warnings)
endif()

if(AQ_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
