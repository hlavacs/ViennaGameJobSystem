
SET(TARGET performance)

SET(SOURCE performance.cpp)

add_executable(${TARGET} ${SOURCE} ${HEADERS})

target_compile_features(${TARGET} PUBLIC cxx_std_20)



