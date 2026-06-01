#!/bin/bash

set -euo pipefail

DAIMA_HOME="${DAIMA_HOME:-$HOME/.daima}"
BASHRC="$HOME/.bashrc"
BEGIN_MARKER="# >>> daima >>>"
END_MARKER="# <<< daima <<<"

remove_path_snippet() {
    local rc_file="$1"
    [ -f "$rc_file" ] || return 0

    python3 - "$rc_file" "$BEGIN_MARKER" "$END_MARKER" <<'PY'
import sys
from pathlib import Path

path = Path(sys.argv[1])
begin = sys.argv[2]
end = sys.argv[3]
text = path.read_text()
start = text.find(begin)
if start < 0:
    raise SystemExit(0)
finish = text.find(end, start)
if finish < 0:
    raise SystemExit(0)
finish += len(end)
if finish < len(text) and text[finish] == "\n":
    finish += 1
while start > 0 and text[start - 1] == "\n":
    start -= 1
path.write_text(text[:start] + text[finish:])
PY
}

echo "=== Uninstalling Daima from $DAIMA_HOME ==="
rm -rf "$DAIMA_HOME"
remove_path_snippet "$BASHRC"

echo ""
echo "Daima removed."
echo "Reload your shell to refresh PATH:"
echo "  source ~/.bashrc"
