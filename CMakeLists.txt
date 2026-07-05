cmake_minimum_required(VERSION 3.30)

project(HeicToJpegConverter LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Gui)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBHEIF REQUIRED libheif)

add_executable(heic_jpeg_converter
        src/main.cpp
)

target_include_directories(heic_jpeg_converter PRIVATE
        ${LIBHEIF_INCLUDE_DIRS}
)

target_link_directories(heic_jpeg_converter PRIVATE
        ${LIBHEIF_LIBRARY_DIRS}
)

target_link_libraries(heic_jpeg_converter PRIVATE
        Qt6::Widgets
        Qt6::Gui
        ${LIBHEIF_LIBRARIES}
)