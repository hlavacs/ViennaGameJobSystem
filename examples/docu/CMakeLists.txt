
SET(TARGET docu)

SET(SOURCE docu.cpp)

add_executable(${TARGET} ${SOURCE} ${HEADERS})

target_compile_features(${TARGET} PUBLIC cxx_std_20)



