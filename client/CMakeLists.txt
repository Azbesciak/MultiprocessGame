cmake_minimum_required(VERSION 3.6)
project(client)

set(CMAKE_C_STANDARD 11)

set(SOURCE_FILES client.c ../libs/structures.h)
add_executable(client ${SOURCE_FILES})