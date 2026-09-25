#!/usr/bin/env bash
# Removes what scripts/install.sh installed, using the manifest CMake wrote.
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
manifest="$root/build/install_manifest.txt"
if [[ ! -f "$manifest" ]]; then
  echo "No install manifest in build/; nothing to remove." >&2
  exit 1
fi
xargs -d '\n' sudo rm -f -- < "$manifest"
echo "Removed Cohort. Settings stay in ${XDG_CONFIG_HOME:-$HOME/.config}/cohort/."
