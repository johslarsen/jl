add_executable(
  jl_test
  bit_test.cpp
  environment_test.cpp
  error_handling_test.cpp
  fd_mmap_test.cpp
  circular_buffer_test.cpp
  misc_test.cpp
  ring_test.cpp
  rows_test.cpp
  rw_loop_test.cpp
  span_test.cpp
  strings_test.cpp
  synchronization_test.cpp
  time_test.cpp
  unique_fd_test.cpp
  unique_mmap_test.cpp
  unique_addr_test.cpp
  unique_socket_test.cpp
  test.cpp)
target_link_libraries(jl_test jl doctest)
add_test(jl_test jl_test)

add_executable(
  jl_curl_test
  curl_test.cpp
  test.cpp)
target_link_libraries(jl_curl_test jl_curl doctest)
add_test(jl_curl_test jl_curl_test)
