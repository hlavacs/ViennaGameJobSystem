
SET(TARGET examples)

SET(SOURCE example_coro.cpp example_func.cpp example_mixed.cpp example_tags.cpp main.cpp)

add_executable(${TARGET} ${SOURCE} ${HEADERS})

target_compile_features(${TARGET} PUBLIC cxx_std_20)


