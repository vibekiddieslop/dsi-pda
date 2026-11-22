#!/bin/bash
# Full build: loader blobs, pda.nds, the test ROM, and the host tests.
# From Windows: wsl.exe -- bash /mnt/<drive>/<path-to-repo>/tools/build.sh
set -e
export PATH="/opt/wonderful/bin:/usr/local/bin:/usr/bin:/bin"
export BLOCKSDS=/opt/wonderful/thirdparty/blocksds/core
export BLOCKSDSEXT=/opt/wonderful/thirdparty/blocksds/external
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

echo "== loader blobs"
make --no-print-directory blobs
echo "== pda.nds"
make --no-print-directory -j8 "$@"
echo "== testboot.nds"
make --no-print-directory -C tools/testboot -j8
echo "== host tests"
bash tests/host/run.sh
