#!/usr/bin/env bash
# Fetches doctest.h if not already present
set -euo pipefail
DEST="tests/doctest.h"
if [[ -f "$DEST" ]]; then
  echo "doctest.h already present — skipping download."
  exit 0
fi
echo "Downloading doctest.h..."
curl -fsSL \
  "https://raw.githubusercontent.com/doctest/doctest/master/doctest/doctest.h" \
  -o "$DEST"
echo "Done: $DEST"
