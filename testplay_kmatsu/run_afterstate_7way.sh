#!/usr/bin/env bash
# Backward-compatible command; the comparison now includes SqrtBudget.
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
exec bash "$SCRIPT_DIR/run_afterstate_8way.sh" "$@"
