@echo off
setlocal

if "%VCPKG_ROOT%"=="" set VCPKG_ROOT=C:\vcpkg

cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake
