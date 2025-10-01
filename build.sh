#!/bin/bash
set -e

BUILD_TESTS="OFF"

if [[ "$1" == "test" ]]; then
  echo "Building with tests enabled."
  BUILD_TESTS="ON"
else
  echo "Building for normal release (without tests)."
fi

cmake -S . -B build -G "Ninja" -D CMAKE_BUILD_TYPE=Debug -D BUILD_TESTING=${BUILD_TESTS}

cmake --build build

if [[ "$1" == "test" ]]; then
  echo "Running tests..."
  ctest --test-dir build --verbose
fi

echo "Build complete."