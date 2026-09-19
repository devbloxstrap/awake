@echo off
setlocal
where cl >nul 2>nul
if errorlevel 1 (
  echo ERROR: MSVC cl.exe not found.
  echo Open "x64 Native Tools Command Prompt for VS 2022" and run this file again.
  exit /b 1
)

if not exist build mkdir build
cmake -S . -B build -A x64
if errorlevel 1 exit /b 1
cmake --build build --config Release
if errorlevel 1 exit /b 1

echo.
echo Built: build\Release\Awake.exe
endlocal
