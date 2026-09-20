@echo off
setlocal
cmake -S . -B build -A x64 || exit /b 1
cmake --build build --config Release || exit /b 1
echo.
echo Awake 2.0 built successfully:
echo %CD%\build\Release\Awake.exe
endlocal
