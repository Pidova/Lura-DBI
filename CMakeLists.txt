cmake_minimum_required(VERSION 3.31)

set(CMAKE_PROJECT_TOP_LEVEL_INCLUDES "${CMAKE_CURRENT_SOURCE_DIR}/conan_provider.cmake")

project(QEMU LANGUAGES CXX C)
set(CMAKE_CXX_STANDARD 23)

if(NOT DEFINED ENV{QEMU_INCLUDE_DIR})
    message(FATAL_ERROR "Environment variable QEMU_INCLUDE_DIR is not set!")
endif()

find_package(lz4 REQUIRED)
find_package(glib REQUIRED)
find_package(Boost REQUIRED)
find_package(RapidJSON REQUIRED)
find_package(capstone REQUIRED)

add_library(QEMU SHARED QEMU/main.cpp)
add_executable(Example Example/main.cpp)

target_include_directories(QEMU PRIVATE $ENV{QEMU_INCLUDE_DIR})

target_link_libraries(QEMU PRIVATE
        lz4::lz4
        glib::glib
        Boost::boost
        rapidjson
        capstone::capstone
)
target_link_libraries(Example PRIVATE
        lz4::lz4
        glib::glib
        Boost::boost
        rapidjson
        capstone::capstone
)