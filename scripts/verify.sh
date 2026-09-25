#!/usr/bin/env bash
# Every stage: the unit tests, the simulated user on each fixture machine,
# and captures of every page in both themes. Output goes to verify-out/,
# which is never committed.
#   scripts/verify.sh [OUTDIR]
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
out="${1:-$root/verify-out}"
rm -rf "$out"
mkdir -p "$out"
"$root/scripts/test.sh"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
"$root/build-tests/cohort-fixture" "$work/legion" legion
echo "== simulated user"
COHORT_CONFIG_DIR="$work/config" COHORT_SYS_ROOT="$work/legion" QT_QPA_PLATFORM=offscreen \
  "$root/build-tests/cohort" --ui-test "$out/ui"
echo "== captures"
for machine in mainline legion older; do
  "$root/scripts/capture.sh" "$out/captures" "$machine"
  "$root/scripts/capture.sh" "$out/captures/compact" "$machine" 400 760
done
echo "All stages passed. Look at the pictures in $out before calling it done."
