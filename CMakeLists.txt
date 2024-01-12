cmake_minimum_required(VERSION 3.0)
project(echo_server)
add_executable(echo_server echo_server.cpp)
target_compile_features(echo_server PUBLIC cxx_std_20)
