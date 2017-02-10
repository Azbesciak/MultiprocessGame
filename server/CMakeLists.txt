cmake_minimum_required(VERSION 3.6)
project(server)

set(CMAKE_C_STANDARD 11)

set(SOURCE_FILES server.c  ../libs/structures.h)
add_executable(untitled ${SOURCE_FILES})