#!/bin/bash
set -e

VCPKG_ROOT=${VCPKG_ROOT:-$HOME/vcpkg}

CC=clang CXX=clang++ cmake -B build -G Ninja -S . \
    -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
