
add_executable(vgjs docu.cpp example_coro.cpp example_func.cpp example_mixed.cpp example_tags.cpp main.cpp test.cpp)

target_compile_features(vgjs PUBLIC cxx_std_20)

