#!/bin/bash
set -e

bash scripts/configure.sh
bash scripts/build.sh

sudo cmake --install build

echo "Installation complete."
