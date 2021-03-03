del CMakeCache.txt
set CLANG="C:/Program Files (x86)/Microsoft Visual Studio/2019/Community/VC/Tools/Llvm/x64/bin/clang.exe"
cmake -A x64 -G "Visual Studio 16 201" -T"ClangCl" -DCMAKE_CXX_COMPILER=%CLANG% -DCMAKE_C_COMPILER=%CLANG% .
