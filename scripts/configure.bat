@echo off
setlocal

uv run conan profile detect --force >NUL 2>NUL

uv run conan install .  ^
    -of build ^
    -s build_type=Release ^
    -s compiler.cppstd=23 ^
    -s compiler.runtime=static ^
    -s compiler.runtime_type=Release ^
    -o *:shared=False ^
    -b missing

set GEN_PATH=%CD%\build\Release\generators

cmake -B build -S . -DCMAKE_BUILD_TYPE=Release ^
	-DCMAKE_PREFIX_PATH="%GEN_PATH%" ^
	-DBoost_DIR="%GEN_PATH%" ^
	-DCMAKE_FIND_PACKAGE_PREFER_CONFIG=ON
