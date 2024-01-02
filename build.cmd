rem cmake -S . -Bbuild -G "Visual Studio 17 2022" -T ClangCL -A x64
cmake -S . -Bbuild -A x64
cd build
cmake --build . --config Release
ctest -C Release
cmake --build . --config Debug
ctest -C Debug
cd ..
