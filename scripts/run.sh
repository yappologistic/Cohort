#!/usr/bin/env bash
# Runs the built window. Until Cohort is installed, a change asks for an
# administrator's password each time, because polkit only trusts the helper
# at its installed path.
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
exec "$root/build/cohort" "$@"
