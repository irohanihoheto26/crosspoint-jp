#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build/kinsoku"
BINARY="$BUILD_DIR/KinsokuTest"

mkdir -p "$BUILD_DIR"

SOURCES=(
  "$ROOT_DIR/test/kinsoku/KinsokuTest.cpp"
  "$ROOT_DIR/lib/Utf8/Utf8.cpp"
)

CXXFLAGS=(
  -std=c++20
  -O2
  -Wall
  -Wextra
  -pedantic
  -I"$ROOT_DIR/lib"
  -I"$ROOT_DIR/lib/Epub"
  -I"$ROOT_DIR/lib/Utf8"
  -I"$ROOT_DIR/lib/GfxRenderer"
)

c++ "${CXXFLAGS[@]}" "${SOURCES[@]}" -o "$BINARY"
"$BINARY" "$@"
