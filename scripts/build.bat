@echo off
setlocal

cmake --build build --config Release

copy .\build\Release\cequip.exe .\build\dist\cequip.exe

echo Build complete: .\build\Release\cequip.exe
