#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
make
exec ./build/chronos "${1:-8080}"
