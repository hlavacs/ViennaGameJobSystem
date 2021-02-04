
cmake_minimum_required (VERSION 3.1.0)
project (VGJS)

set(default_build_type "Debug")
#set(CMAKE_CXX_FLAGS "/await" "/EHsc")
set(CMAKE_CXX_FLAGS_RELEASE /O2)

add_compile_options(/await)
add_compile_options(/EHsc)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(EXECUTABLE_OUTPUT_PATH ${CMAKE_HOME_DIRECTORY}/bin)
set(CMAKE_VS_PLATFORM_NAME "x64")

SET(INCLUDE ${CMAKE_HOME_DIRECTORY}/include)
SET(HEADERS ${INCLUDE}/VGJS.h ${INCLUDE}/VGJSCoro.h)

include_directories (${INCLUDE})

add_subdirectory (examples/docu)
add_subdirectory (examples/examples)
add_subdirectory (examples/performance)
add_subdirectory (examples/test)

