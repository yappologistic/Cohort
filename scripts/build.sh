#!/usr/bin/env bash
# Builds Cohort and its helper into build/. Extra arguments go to CMake.
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cmake -S "$root" -B "$root/build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF "$@"
cmake --build "$root/build" --parallel "${COHORT_BUILD_JOBS:-$(nproc)}"
