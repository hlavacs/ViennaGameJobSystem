
SET(TARGET test)

SET(SOURCE test.cpp)

add_executable(${TARGET} ${SOURCE} ${HEADERS})

target_compile_features(${TARGET} PUBLIC cxx_std_20)

