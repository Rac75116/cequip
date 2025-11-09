@echo off
setlocal

cmake --build build --config Release

echo Build complete: .\build\Release\cequip.exe
