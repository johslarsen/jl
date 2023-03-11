cmake_minimum_required(VERSION 3.2)

project(
  jl
  VERSION 0.1.0
  LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(JL_ENABLE_TESTING "Enable testing of the jl library." ON)

add_definitions(-Wall -Wextra -fno-omit-frame-pointer -g3 -fstack-protector)

add_library(jl INTERFACE)
target_include_directories(jl INTERFACE include/)

if(JL_ENABLE_TESTING AND EXISTS ${CMAKE_SOURCE_DIR}/external/googletest/CMakeLists.txt)
  add_subdirectory(external/googletest)
  enable_testing()
  add_subdirectory(test)
endif()
