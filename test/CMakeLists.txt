add_executable(
  run_test
  environment_test.cpp
  errno_as_error_test.cpp
  fd_mmap_test.cpp
  circular_buffer_test.cpp
  strings_test.cpp
  unique_fd_test.cpp
  unique_mmap_test.cpp
  unique_addr_test.cpp
  unique_socket_test.cpp
  test.cpp)
target_link_libraries(run_test jl gtest)
add_test(all_tests run_test)
