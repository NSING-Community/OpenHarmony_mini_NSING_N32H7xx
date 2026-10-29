#!/usr/bin/env bash
# Flag two things before a commit:
#   1) whole-file diffs  -> almost always line-ending conversion (CRLF), not a real change
#   2) literal CR bytes  -> CRLF left in the working tree
#   usage: check_crlf.sh <git-repo> [<git-repo>...]
set -uo pipefail
[ $# -ge 1 ] || { echo "usage: $0 <git-repo> [...]"; exit 2; }
FAIL=0
for repo in "$@"; do
  echo "== $repo"
  git -C "$repo" rev-parse --git-dir >/dev/null 2>&1 || { echo "  not a git repo"; continue; }
  n=0
  while IFS= read -r f; do
    [ -n "$f" ] || continue
    read -r a d <<<"$(git -C "$repo" diff --numstat -- "$f" | awk '{print $1, $2}')"
    if [ "${a:-x}" = "x" ]; then continue; fi          # binary or untracked
    if [ "$a" = "$d" ] && [ "$a" -gt 50 ]; then
      echo "  whole-file diff (suspect line endings): $f ($a/$d)"; FAIL=1; n=$((n+1))
    fi
    if [ -f "$repo/$f" ] && grep -qU $'\r' "$repo/$f" 2>/dev/null; then
      echo "  CRLF present: $f"; FAIL=1; n=$((n+1))
    fi
  done < <(git -C "$repo" status --porcelain | grep -E '^ M|^M ' | awk '{print $2}')
  echo "  issues: $n"
done
[ "$FAIL" -eq 0 ] && echo "RESULT: pass" || echo "RESULT: issues found (see above)"
exit "$FAIL"
