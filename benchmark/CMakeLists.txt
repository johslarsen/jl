add_executable(circular_buffer circular_buffer.cpp)
target_link_libraries(circular_buffer jl benchmark)

add_executable(unique_socket unique_socket.cpp)
target_link_libraries(unique_socket jl benchmark)
