cmake_minimum_required(VERSION 3.10)
project(drone_server C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_FLAGS "-Wall -O2")

include_directories(.)
# 添加 FFmpeg 头文件路径
include_directories(/usr/include/aarch64-linux-gnu)

add_executable(drone_server
    main.c
    udp.c
    uart.c
    video.c
)

# 链接 FFmpeg 库
target_link_libraries(drone_server 
    pthread 
    avcodec 
    avutil 
    swscale
)