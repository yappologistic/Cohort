#!/usr/bin/env bash
# Builds the tests and the diagnostic window into build-tests/ and runs the
# unit tests.
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cmake -S "$root" -B "$root/build-tests" -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON -DCOHORT_DIAGNOSTICS=ON
cmake --build "$root/build-tests" --parallel "${COHORT_BUILD_JOBS:-$(nproc)}"
ctest --test-dir "$root/build-tests" --output-on-failure
