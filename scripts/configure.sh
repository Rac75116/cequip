#!/bin/bash
set -e

CLANG_MAJOR=$(clang++ -dumpversion | cut -d. -f1)
CLANG_C=$(command -v clang)
CLANG_CXX=$(command -v clang++)

uv run conan profile detect --force >/dev/null 2>&1 || true

uv run conan install . \
    -of "build" \
    -s build_type="Release" \
    -s:h compiler=clang \
    -s:h compiler.version="$CLANG_MAJOR" \
    -s:h compiler.libcxx=libstdc++11 \
    -s:h compiler.cppstd=23 \
    -s:b compiler=clang \
    -s:b compiler.version="$CLANG_MAJOR" \
    -s:b compiler.libcxx=libstdc++11 \
    -s:b compiler.cppstd=23 \
    -c:h tools.build:compiler_executables="{\"c\":\"$CLANG_C\",\"cpp\":\"$CLANG_CXX\"}" \
    -c:b tools.build:compiler_executables="{\"c\":\"$CLANG_C\",\"cpp\":\"$CLANG_CXX\"}" \
    -o "*:shared=False" \
    -b missing

CC=clang CXX=clang++ cmake -B "build" -G "Ninja" -S . \
    -DCMAKE_BUILD_TYPE="Release"
