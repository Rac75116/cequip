@echo off
setlocal

call scripts\configure.bat
call scripts\build.bat

cmake --install build --config Release

echo "Installation complete."
