@echo off
setlocal

uv run conan profile detect --force >NUL 2>NUL

uv run conan install . -of build -s build_type=Release -s compiler.runtime=static -s compiler.runtime_type=Release -o *:shared=False -b missing

cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
