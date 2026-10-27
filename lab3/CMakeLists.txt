cmake_minimum_required(VERSION 3.16)
project(PlutoSDR_Lab3 C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_BUILD_TYPE Debug)

find_package(SoapySDR REQUIRED)

add_executable(main main.cpp)
target_link_libraries(main PRIVATE ${SoapySDR_LIBRARIES})