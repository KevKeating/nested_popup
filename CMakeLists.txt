cmake_minimum_required(VERSION 3.24)
project(nested_popup LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

qt_add_executable(nested_popup main.cpp)

target_link_libraries(nested_popup PRIVATE Qt6::Widgets)
