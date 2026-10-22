#!/usr/bin/env bash
# Builds and runs the portable-core unit tests (Linux / macOS / WSL / MSYS2 - any g++/clang++ with C++20).
# nlohmann/json: pass its include dir as $NLOHMANN_INCLUDE, or install it system-wide
# (apt install nlohmann-json3-dev).
set -euo pipefail
cd "$(dirname "$0")/.."

CXX="${CXX:-g++}"
OUT="${TMPDIR:-/tmp}/swapmoveset_test_core"
INC=()
if [[ -n "${NLOHMANN_INCLUDE:-}" ]]; then INC+=("-I${NLOHMANN_INCLUDE}"); fi

"$CXX" -std=c++20 -Wall -Wextra -Wpedantic -Werror -O1 -g \
  ${SANITIZE:+-fsanitize=address,undefined -fno-sanitize-recover=all} \
  -Isrc "${INC[@]}" \
  tests/test_core.cpp src/core/*.cpp -o "$OUT"
"$OUT"
