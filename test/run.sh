#!/usr/bin/env bash
# Host tests for the night light's rules and its ring (no device, no ESP-IDF needed).
set -euo pipefail
cd "$(dirname "$0")"
OUT=$(mktemp -d)
cc -std=c11 -Wall -Wextra -Werror -O1 -o "$OUT/test_nightlight" test_nightlight.c ../main/nightlight.c ../main/nl_art.c
"$OUT/test_nightlight"
rm -rf "$OUT"
