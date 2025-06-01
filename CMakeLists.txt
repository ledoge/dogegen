cmake_minimum_required(VERSION 3.23)
project(dogegen)

set(CMAKE_CXX_STANDARD 17)

add_executable(dogegen main.cpp)
target_link_libraries(dogegen d3d11 d3dcompiler dwmapi ws2_32)
