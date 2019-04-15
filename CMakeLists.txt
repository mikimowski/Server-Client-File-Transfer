cmake_minimum_required(VERSION 3.13)
project(computer_networking_file_transfer C)

set(CMAKE_C_STANDARD 11)

add_executable(netstore-client client.c err.c)
add_executable(netstore-server server.c err.c)