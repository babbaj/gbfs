cmake_minimum_required(VERSION 3.18)
project(gbfs)

set(CMAKE_CXX_STANDARD 20)

find_package(PkgConfig)

pkg_check_modules(FUSE REQUIRED IMPORTED_TARGET fuse3)
pkg_check_modules(SQLITE REQUIRED IMPORTED_TARGET sqlite3)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O0 -g -lpthread -march=native")

add_executable(gbfs gbfs.cpp)
target_link_libraries(gbfs PkgConfig::FUSE)
target_link_libraries(gbfs PkgConfig::SQLITE)
