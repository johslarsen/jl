add_executable(
  jl_test
  bit_test.cpp
  environment_test.cpp
  error_handling_test.cpp
  misc_test.cpp
  ring_test.cpp
  rows_test.cpp
  rw_loop_test.cpp
  span_test.cpp
  stats_test.cpp
  strings_test.cpp
  synchronization_test.cpp
  time_test.cpp
  test.cpp)
target_link_libraries(jl_test jl doctest)
add_test(jl_test jl_test)

add_executable(
  jl_posix_test
  posix/circular_buffer_test.cpp
  posix/fd_mmap_test.cpp
  posix/unique_fd_test.cpp
  posix/unique_mmap_test.cpp
  posix/unique_addr_test.cpp
  posix/unique_socket_test.cpp
  test.cpp)
target_link_libraries(jl_posix_test jl_posix doctest)
add_test(jl_posix_test jl_posix_test)

if (${CURL_FOUND})
  add_executable(jl_curl_test curl/curl_test.cpp test.cpp)
  target_link_libraries(jl_curl_test jl_curl jl_posix doctest)
  add_test(jl_curl_test jl_curl_test)
endif()
