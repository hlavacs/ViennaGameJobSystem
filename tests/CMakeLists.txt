
set(TARGET testlib)

set(SOURCE testlib.cpp)

set(HEADERS ${PROJECT_SOURCE_DIR}/extern/ViennaStrongType/VSTY.h)

add_executable(${TARGET} ${SOURCE} ${HEADERS})

target_compile_features(${TARGET} PUBLIC cxx_std_20)

target_include_directories(${TARGET} PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_include_directories(${TARGET} PUBLIC ${PROJECT_SOURCE_DIR}/extern/ViennaStrongType)

add_test(NAME ${TARGET} COMMAND testlib) # Command can be a target
