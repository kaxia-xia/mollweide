cmake_minimum_required(VERSION 3.5)
project(mapbeauty LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find stb image headers
find_path(STB_INCLUDE_DIR stb/stb_image.h)
if(NOT STB_INCLUDE_DIR)
    set(STB_INCLUDE_DIR /data/data/com.termux/files/usr/include)
endif()

# Find libpng and libjpeg for stb_image
find_library(PNG_LIBRARY png)
find_library(JPEG_LIBRARY jpeg)

add_executable(globe globe.cpp)
target_include_directories(globe PRIVATE ${STB_INCLUDE_DIR})
target_link_libraries(globe PRIVATE ${PNG_LIBRARY} ${JPEG_LIBRARY} m)
