#!/usr/bin/env bash
# Builds and runs the runtime tests: the plugin's own game-side code against a fake game
# (tests/stubs replaces CommonLibSSE-NG; Win32 / XInput / ImGui are faked in test_runtime.cpp).
#   NLOHMANN_INCLUDE=<dir containing nlohmann/>   SANITIZE=thread|address,undefined   CXX=clang++
set -euo pipefail
cd "$(dirname "$0")/.."

CXX="${CXX:-g++}"
OUT="${TMPDIR:-/tmp}/swapmoveset_test_runtime"
INC=()
if [[ -n "${NLOHMANN_INCLUDE:-}" ]]; then INC+=("-I${NLOHMANN_INCLUDE}"); fi

"$CXX" -std=c++23 -Wall -Wextra -O1 -g -pthread -D__stdcall= \
  ${SANITIZE:+-fsanitize=${SANITIZE}} \
  -Itests/stubs -Isrc "${INC[@]}" -include src/pch.h \
  tests/test_runtime.cpp src/Runtime.cpp src/InputPoller.cpp src/MenuUI.cpp src/main.cpp src/core/*.cpp \
  -o "$OUT"

# the OAR condition (needs the OAR API headers) is only syntax-checked against the stubs
"$CXX" -std=c++23 -fsyntax-only -D__stdcall= -Itests/stubs -Isrc -include src/pch.h \
  src/PackActiveCondition.cpp src/oar_api/OpenAnimationReplacer-ConditionTypes.cpp src/oar_api/OpenAnimationReplacerAPI-Conditions.cpp

"$OUT"
