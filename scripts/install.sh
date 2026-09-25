#!/usr/bin/env bash
# Builds and installs Cohort system-wide, under /usr by default.
#
# The polkit policy has to land where polkit reads policies,
# /usr/share/polkit-1/actions, and it names the helper by its installed path,
# so this is a system install rather than a per-user one. It asks for sudo
# once, to copy the files.
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
prefix="${1:-/usr}"
"$root/scripts/build.sh" -DCMAKE_INSTALL_PREFIX="$prefix"
sudo cmake --install "$root/build"
if command -v update-desktop-database >/dev/null; then sudo update-desktop-database -q "$prefix/share/applications" || true; fi
if command -v gtk-update-icon-cache >/dev/null; then sudo gtk-update-icon-cache -q "$prefix/share/icons/hicolor" || true; fi
printf 'Installed Cohort to %s/bin/cohort\n' "$prefix"
