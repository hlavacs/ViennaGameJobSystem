
cmake_minimum_required (VERSION 3.1.0)
project (VGJS)

set(default_build_type "Debug")
set(CMAKE_CXX_FLAGS_RELEASE "/O2")

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(EXECUTABLE_OUTPUT_PATH ${CMAKE_HOME_DIRECTORY}/bin)
set(CMAKE_VS_PLATFORM_NAME "x64")

add_subdirectory (src)

