add_executable(circular_buffer circular_buffer.cpp)
target_link_libraries(circular_buffer jl benchmark)

add_executable(curl_benchmark curl.cpp)
target_link_libraries(curl_benchmark jl_curl benchmark)

add_executable(fixed_string fixed_string.cpp)
target_link_libraries(fixed_string jl benchmark)

add_executable(strings strings.cpp)
target_link_libraries(strings jl benchmark)

add_executable(math math.cpp)
target_link_libraries(math benchmark)

add_executable(unique_socket unique_socket.cpp)
target_link_libraries(unique_socket jl benchmark)

add_executable(copy copy.cpp)
target_link_libraries(copy jl benchmark)

add_executable(mmap mmap.cpp)
target_link_libraries(mmap jl benchmark)

add_executable(ring_index ring_index.cpp)
target_link_libraries(ring_index jl benchmark)

add_executable(span span.cpp)
target_link_libraries(span jl benchmark)

add_executable(splice splice.cpp)
target_link_libraries(splice jl benchmark)
