#!/usr/bin/env bash
# Maintainer only: refresh this overlay from an OpenHarmony working tree.
#   usage: tools/export.sh [OHOS_ROOT] [DOCS_DIR]
# After running, review `git status` and commit.
set -euo pipefail
OHOS="${1:-$HOME/oh_n32h7xx/OHOS_MINI}"
DOCS="${2:-}"
DST="$(cd "$(dirname "$0")/.." && pwd)"

DIRS="device/soc/Nsing device/board/Nsing vendor/Nsing applications/sample/n32h7xx_eval"

for d in $DIRS; do
  [ -d "$OHOS/$d" ] || { echo "missing: $OHOS/$d"; exit 1; }
  mkdir -p "$DST/$d"
  rsync -a --delete "$OHOS/$d/" "$DST/$d/"
done

while IFS= read -r f; do
  [ -n "$f" ] || continue
  [ -f "$OHOS/$f" ] || { echo "missing: $OHOS/$f"; exit 1; }
  mkdir -p "$DST/$(dirname "$f")"
  cp -f "$OHOS/$f" "$DST/$f"
done < "$DST/overlay.manifest"

if [ -n "$DOCS" ]; then
  mkdir -p "$DST/docs_Nsing"
  rsync -a "$DOCS/" "$DST/docs_Nsing/"
fi

echo "--- exported; review before commit ---"
git -C "$DST" status --short | head -40
