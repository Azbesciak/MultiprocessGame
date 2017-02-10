cmake_minimum_required(VERSION 3.6)
project(chat)

set(CMAKE_C_STANDARD 11)

set(SOURCE_FILES chat.c ../libs/structures.h)
add_executable(chat ${SOURCE_FILES})