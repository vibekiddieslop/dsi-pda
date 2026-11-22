#!/bin/bash
# Builds and runs the host tests for source/store with the native compiler.
# Usage (from Windows): wsl.exe -- bash /mnt/<drive>/<path-to-repo>/tests/host/run.sh
set -e
cd "$(dirname "$0")"
mkdir -p build
gcc -std=gnu11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
    test_main.c ../../source/store/*.c ../../source/input/*.c -o build/host_tests
./build/host_tests
