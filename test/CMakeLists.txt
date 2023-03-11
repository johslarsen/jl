add_executable(
  run_test
  environment_test.cpp
  errno_as_error_test.cpp
  ring_buffer_test.cpp
  tmpfd_test.cpp
  unique_fd_test.cpp
  unique_mmap_test.cpp
  test.cpp)
target_link_libraries(run_test jl gtest)
add_test(all_tests run_test)
