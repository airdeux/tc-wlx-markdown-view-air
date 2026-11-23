@echo off
rem Builds TcWlxMarkdownViewAir.wlx (32-bit) and TcWlxMarkdownViewAir.wlx64 (64-bit) with Visual Studio + CMake
setlocal
cd /d "%~dp0"
cmake -S . -B build-x86 -A Win32 || goto :err
cmake --build build-x86 --config Release || goto :err
cmake -S . -B build-x64 -A x64 || goto :err
cmake --build build-x64 --config Release || goto :err
if not exist bin mkdir bin
copy /y build-x86\Release\TcWlxMarkdownViewAir.wlx bin\ >nul
copy /y build-x64\Release\TcWlxMarkdownViewAir.wlx64 bin\ >nul
copy /y build-x64\Release\lister_test_host.exe bin\ >nul
echo Done: bin\TcWlxMarkdownViewAir.wlx, bin\TcWlxMarkdownViewAir.wlx64
exit /b 0
:err
echo Build failed.
exit /b 1
