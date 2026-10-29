#!/usr/bin/env bash
# Apply (or revert) this overlay on top of an OpenHarmony source tree.
#   usage: tools/apply-overlay.sh <OHOS_ROOT> [--revert]
#
# Safety rules, learned the hard way:
#   - preflight before writing: the base tree must be clean for every file this
#     overlay replaces, otherwise the user's own edits would be silently lost;
#   - every overwritten file is copied into .ohos-overlay-backup/<timestamp>/;
#   - revert restores from that backup (falling back to git checkout) and only
#     removes directories this overlay created - it never ignores failures.
set -euo pipefail
OHOS="${1:?usage: $0 <OHOS_ROOT> [--revert]}"
MODE="${2:-apply}"
SRC="$(cd "$(dirname "$0")/.." && pwd -P)"
OHOS="$(cd "$OHOS" 2>/dev/null && pwd -P || true)"
MANIFEST="$SRC/overlay.manifest"
DIRS="device/soc/Nsing device/board/Nsing vendor/Nsing applications/sample/n32h7xx_eval docs_Nsing"
BACKUP_ROOT="$SRC/.ohos-overlay-backup"

[ -n "$OHOS" ] && [ -d "$OHOS/kernel/liteos_m" ] || { echo "not an OpenHarmony tree: $1"; exit 2; }
[ -f "$MANIFEST" ] || { echo "missing overlay.manifest in $SRC"; exit 2; }
[ "$SRC" != "$OHOS" ] || { echo "overlay directory equals target tree"; exit 2; }

repo_of() {
  case "$1" in
    drivers/hdf_core/*) echo "drivers/hdf_core" ;;
    kernel/liteos_m/*)  echo "kernel/liteos_m"  ;;
    *) echo "" ;;
  esac
}

latest_backup() {
  [ -d "$BACKUP_ROOT" ] || return 1
  ls -1d "$BACKUP_ROOT"/*/ 2>/dev/null | sort | tail -1
}

if [ "$MODE" = "--revert" ]; then
  BK="$(latest_backup || true)"
  fails=0
  while IFS= read -r f; do
    [ -n "$f" ] || continue
    if [ -n "$BK" ] && [ -f "$BK/$f" ]; then
      cp -f "$BK/$f" "$OHOS/$f" || { echo "FAILED restore $f"; fails=$((fails+1)); }
    else
      repo="$(repo_of "$f")"
      if [ -n "$repo" ] && ! ( cd "$OHOS/$repo" && git checkout -- "${f#"$repo"/}" ); then
        echo "FAILED git checkout $f (no backup available)"; fails=$((fails+1))
      fi
    fi
  done < "$MANIFEST"

  while IFS= read -r d; do
    [ -n "$d" ] || continue
    if [ -d "$OHOS/$d" ]; then
      rm -rf "$OHOS/$d" || { echo "FAILED remove $d"; fails=$((fails+1)); }
    fi
  done <<< "$(echo "$DIRS" | tr ' ' '\n')"

  [ "$fails" -eq 0 ] && { echo "reverted: $OHOS"; exit 0; } || { echo "revert finished with $fails error(s)"; exit 1; }
fi

# ---- preflight ----
dirty=0
while IFS= read -r f; do
  [ -n "$f" ] || continue
  repo="$(repo_of "$f")"
  [ -n "$repo" ] || continue
  if [ -n "$(cd "$OHOS/$repo" && git status --porcelain -- "${f#"$repo"/}")" ]; then
    echo "DIRTY  $f  (commit or stash it first)"; dirty=1
  fi
  [ -f "$SRC/$f" ] || { echo "MISSING in overlay: $f"; dirty=1; }
done < "$MANIFEST"
for d in $DIRS; do
  [ -d "$SRC/$d" ] || { echo "MISSING in overlay: $d/"; dirty=1; }
done
[ "$dirty" -eq 0 ] || { echo "preflight failed, nothing written"; exit 3; }

# ---- backup + write ----
TS="$(date +%Y%m%d-%H%M%S)"
BK="$BACKUP_ROOT/$TS"
mkdir -p "$BK"
while IFS= read -r f; do
  [ -n "$f" ] || continue
  if [ -f "$OHOS/$f" ]; then
    mkdir -p "$BK/$(dirname "$f")"
    cp -f "$OHOS/$f" "$BK/$f"
  fi
done < "$MANIFEST"
echo "$TS" > "$BACKUP_ROOT/latest"

for d in $DIRS; do mkdir -p "$OHOS/$d"; rsync -a "$SRC/$d/" "$OHOS/$d/"; done
while IFS= read -r f; do
  [ -n "$f" ] || continue
  mkdir -p "$OHOS/$(dirname "$f")"
  cp -f "$SRC/$f" "$OHOS/$f"
done < "$MANIFEST"

echo "applied to : $OHOS"
echo "backup     : $BK"
echo "next       : export PATH=\$HOME/.local/bin:/$PATH && hb set -p n32h7xx_eval && hb build"
