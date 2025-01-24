cmake_minimum_required(VERSION 3.20)

project(
  jl
  VERSION 0.1.0
  LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(JL_ENABLE_TESTING "Enable testing of the jl library." ON)

add_definitions(-Wall -Wextra -Wno-unknown-warning-option -march=native)

add_library(jl INTERFACE include/jl.h)
target_include_directories(jl INTERFACE include/)
target_compile_features(jl INTERFACE cxx_std_23)

add_library(jl_posix INTERFACE include/jl_posix.h)
target_link_libraries(jl_posix INTERFACE jl)

find_package(CURL)
if (${CURL_FOUND})
  add_library(jl_curl INTERFACE include/jl_curl.h)
  target_link_libraries(jl_curl INTERFACE jl curl)
endif()

find_package(SQLite3)
find_package(PostgreSQL)
add_library(jl_db INTERFACE include/jl_db.h)
if (${SQLite3_FOUND})
  target_compile_definitions(jl_db INTERFACE -DJL_HAS_SQLITE=1)
endif()
if (${PostgreSQL_FOUND})
  target_compile_definitions(jl_db INTERFACE -DJL_HAS_PSQL=1)
endif()
target_link_libraries(jl_db INTERFACE jl ${PostgreSQL_LIBRARIES} ${SQLite3_LIBRARIES})

set(BENCHMARK_ENABLE_TESTING OFF)
if(JL_ENABLE_TESTING AND EXISTS
                         ${CMAKE_SOURCE_DIR}/external/benchmark/CMakeLists.txt)
  add_subdirectory(external/benchmark)
  add_subdirectory(benchmark)
endif()

if(JL_ENABLE_TESTING AND EXISTS
                         ${CMAKE_SOURCE_DIR}/external/doctest/CMakeLists.txt)
  add_definitions(-fno-omit-frame-pointer -g3 -fstack-protector)
  add_subdirectory(external/doctest)
  enable_testing()
  add_subdirectory(test)
endif()
