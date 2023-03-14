cmake_minimum_required(VERSION 3.16)
project(chatServer C)

set(CMAKE_C_STANDARD 11)
# set(CMAKE_C_FLAGS  "${CMAKE_C_FLAGS} -g -Wall -Wextra -Wpedantic -fsanitize=address")

set (SOURCE 
	chatServer.c
	chatServer.h
	)

add_executable(${PROJECT_NAME} ${SOURCE})
