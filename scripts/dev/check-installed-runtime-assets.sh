#!/bin/bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
AGENT_HOME="${AGENT_HOME:-$HOME/.agent-data}"
INSTALLED_WEB_DIR="$AGENT_HOME/spiffs_data/web"
INSTALLED_CONFIG="$AGENT_HOME/spiffs_data/config/config.json"
SOURCE_INDEX="$ROOT_DIR/spiffs_data/web/index.html"

fail() {
  echo "install-check failed: $*" >&2
  exit 1
}

[ -d "$INSTALLED_WEB_DIR" ] || fail "missing installed web dir: $INSTALLED_WEB_DIR"
[ -f "$INSTALLED_CONFIG" ] || fail "missing installed config: $INSTALLED_CONFIG"

mapfile -t script_paths < <(python3 - "$SOURCE_INDEX" <<'PY'
import re
import sys

html = open(sys.argv[1], "r", encoding="utf-8").read()
for match in re.finditer(r'<script src="/([^"]+\.js)"></script>', html):
    print(match.group(1))
PY
)

for rel in "${script_paths[@]}"; do
  [ -f "$INSTALLED_WEB_DIR/$rel" ] || fail "missing installed web asset: $rel"
done

python3 - "$INSTALLED_CONFIG" <<'PY'
import json
import sys

path = sys.argv[1]
with open(path, "r", encoding="utf-8") as f:
    data = json.load(f)

common = data.get("common") or {}
web = data.get("web") or {}

if common.get("terminal_security_level") not in {"plan", "build"}:
    raise SystemExit("install-check failed: common.terminal_security_level missing or invalid")

pet = web.get("default_pet_package_id")
if pet == "guga.codex-pet":
    raise SystemExit("install-check failed: web.default_pet_package_id still uses legacy guga.codex-pet")
if not pet:
    raise SystemExit("install-check failed: web.default_pet_package_id missing")
PY

echo "install-check ok: installed runtime assets/config are consistent"
