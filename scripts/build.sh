#!/bin/bash
set -e

cmake --build build

cp ./build/cequip ./build/dist/cequip

echo "Build complete: ./build/cequip"
