
cmake_minimum_required (VERSION 3.1.0)
project (VGJS)

if (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
  set(CMAKE_CXX_FLAGS_RELEASE /O2)
  add_compile_options(/await)
  add_compile_options(/EHsc)
  set(CMAKE_VS_PLATFORM_NAME "x64")
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang"
    OR CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
    #clang++ *.cpp -g -std=c++20 -stdlib=libc++ -pthread -fcoroutines-ts -o a.out
    add_compile_options(-pthread)
    add_compile_options(-stdlib=libc++)
    add_compile_options(-fcoroutines-ts)
elseif(CMAKE_COMPILER_IS_GNUCXX)
endif()

set(EXECUTABLE_OUTPUT_PATH ${CMAKE_HOME_DIRECTORY}/bin)
SET(INCLUDE ${CMAKE_HOME_DIRECTORY}/include)
SET(HEADERS ${INCLUDE}/VGJS.h ${INCLUDE}/VGJSCoro.h)
include_directories (${INCLUDE})

add_subdirectory (examples/docu)
add_subdirectory (examples/examples)
add_subdirectory (examples/performance)
add_subdirectory (examples/test)

find_package(Doxygen)
if (DOXYGEN_FOUND)
  set(DOXYGEN_USE_MDFILE_AS_MAINPAGE "${CMAKE_SOURCE_DIR}/README.md")
  doxygen_add_docs(doxygen
    ${CMAKE_SOURCE_DIR}/README.md
    ${CMAKE_SOURCE_DIR}/include
  )
endif(DOXYGEN_FOUND)
