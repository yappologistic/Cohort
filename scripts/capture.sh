#!/usr/bin/env bash
# Photographs every page of a fixture machine, in both themes, into DIR.
#   scripts/capture.sh DIR [mainline|legion|older] [width height]
# Uses the build-tests tree and renders offscreen (COHORT_CAPTURE_PLATFORM=wayland
# puts the windows on the desktop instead). Settings are isolated; the desktop's fonts and
# palette are left as they are, because they are what the window is set in.
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
out="${1:?usage: capture.sh DIR [machine] [width height]}"
kind="${2:-legion}"
mkdir -p "$out"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
"$root/build-tests/cohort-fixture" "$work/sys-root" "$kind"
for theme in dark light; do
  mkdir -p "$work/config-$theme/cohort"
  printf '[General]\ntheme=%s\n' "$theme" > "$work/config-$theme/cohort/cohort.conf"
  for page in power battery fans keyboard; do
    QT_QPA_PLATFORM="${COHORT_CAPTURE_PLATFORM:-offscreen}" COHORT_CONFIG_DIR="$work/config-$theme" COHORT_SYS_ROOT="$work/sys-root" \
      "$root/build-tests/cohort" --screenshot "$out/$kind-$page-$theme.png" --page "$page" \
      ${3:+--size "$3" "$4"}
  done
done
