cmake_minimum_required(VERSION 3.16)
project(QtUAVClient CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

# 自动运行 MOC 等 Qt 工具
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# 查找 Qt6
find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network)

# 查找 FFmpeg（正确方式）
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED
    libavcodec
    libavformat
    libavutil
    libswscale
)

# 构建可执行文件
add_executable(QtUAVClient
    main.cpp
    mainwindow.cpp
    mainwindow.h
)

# 链接 Qt
target_link_libraries(QtUAVClient
    PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::Network
)

# 链接 FFmpeg（最关键）
target_include_directories(QtUAVClient PRIVATE ${FFMPEG_INCLUDE_DIRS})
target_link_directories(QtUAVClient PRIVATE ${FFMPEG_LIBRARY_DIRS})
target_link_libraries(QtUAVClient PRIVATE ${FFMPEG_LIBRARIES})
target_compile_options(QtUAVClient PRIVATE ${FFMPEG_CFLAGS_OTHER})