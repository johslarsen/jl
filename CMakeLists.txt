cmake_minimum_required(VERSION 3.5)

project(
  jl
  VERSION 0.1.0
  LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(JL_ENABLE_TESTING "Enable testing of the jl library." ON)

add_library(jl INTERFACE)
target_include_directories(jl INTERFACE include/)

add_definitions(-Wall -Wextra -march=native)

set(BENCHMARK_ENABLE_TESTING OFF)
if(JL_ENABLE_TESTING AND EXISTS
                         ${CMAKE_SOURCE_DIR}/external/benchmark/CMakeLists.txt)
  add_subdirectory(external/benchmark)
  add_subdirectory(benchmark)
endif()

if(JL_ENABLE_TESTING AND EXISTS
                         ${CMAKE_SOURCE_DIR}/external/googletest/CMakeLists.txt)
  set(SANITIZE_FLAGS -fsanitize=undefined,address)
  add_compile_options(${SANITIZE_FLAGS})
  add_link_options(${SANITIZE_FLAGS})
  add_definitions(-fno-omit-frame-pointer -g3 -fstack-protector)
  add_subdirectory(external/googletest)
  enable_testing()
  add_subdirectory(test)
endif()
