#!/bin/bash
set -e

BUILD_TESTS="OFF"
SAVE_TMP_OBJECTS="OFF"


# Parse args in any order
for arg in "$@"; do
  case "$arg" in
    test)
      echo "Building with tests enabled."
      BUILD_TESTS="ON"
      ;;
    save-temps)
      echo "Configuring to save temporary object files and assembly output."
      SAVE_TMP_OBJECTS="ON"
      ;;
    *)
      echo "Unknown option: $arg"
      ;;
  esac
done


if [[ "$SAVE_TMP_OBJECTS" == "ON" ]]; then
  echo "Configuring to save temporary object files and assembly output."
  temp_objs=(
    -DCMAKE_C_FLAGS="-save-temps=obj -fverbose-asm"
    -DCMAKE_CXX_FLAGS="-save-temps=obj -fverbose-asm"
  )
else
  temp_objs=()
fi

cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DBUILD_TESTING="${BUILD_TESTS}" \
  "${temp_objs[@]}"


cmake --build build

if [[ "$BUILD_TESTS" == "ON" ]]; then
  echo "Running tests..."
  ctest --test-dir build --verbose --output-on-failure
fi

echo "Build complete."