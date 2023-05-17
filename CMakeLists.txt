cmake_minimum_required(VERSION 3.20)

project(
  jl
  VERSION 0.1.0
  LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(JL_ENABLE_TESTING "Enable testing of the jl library." ON)

add_library(jl INTERFACE)
target_include_directories(jl INTERFACE include/)
target_compile_features(jl INTERFACE cxx_std_23)

add_definitions(-Wall -Wextra -march=native)
add_definitions(-std=c++2b) # because clang16 does not officially support 23 yet

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
