#!/bin/bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

DAIMA_HOME="${DAIMA_HOME:-$HOME/.daima}"
BIN_DIR="$DAIMA_HOME/bin"
CONFIG_DIR="$DAIMA_HOME/spiffs_data/config"
WEB_DIR="$DAIMA_HOME/spiffs_data/web"
SKILLS_DIR="$DAIMA_HOME/spiffs_data/skills"
CA_DIR="$DAIMA_HOME/spiffs_data/ca"
TARGET_BIN="$BIN_DIR/daima"
BASHRC="$HOME/.bashrc"

copy_if_missing() {
    local src="$1"
    local dst="$2"
    if [ ! -e "$dst" ]; then
        install -Dm644 "$src" "$dst"
    fi
}

ensure_path_snippet() {
    local rc_file="$1"
    local begin="# >>> daima >>>"
    local end="# <<< daima <<<"

    [ -f "$rc_file" ] || touch "$rc_file"

    if grep -Fq "$begin" "$rc_file"; then
        return
    fi

    cat >> "$rc_file" <<'EOF'

# >>> daima >>>
if [ -d "$HOME/.daima/bin" ] && [[ ":$PATH:" != *":$HOME/.daima/bin:"* ]]; then
    export PATH="$HOME/.daima/bin:$PATH"
fi
# <<< daima <<<
EOF
}

echo "=== Building daima ==="
make clean
make

echo "=== Installing to $DAIMA_HOME ==="
mkdir -p "$BIN_DIR" "$CONFIG_DIR" "$WEB_DIR" "$SKILLS_DIR" "$CA_DIR"
mkdir -p "$DAIMA_HOME/spiffs_data/memory" "$DAIMA_HOME/spiffs_data/sessions" "$DAIMA_HOME/spiffs_data/cache"

rm -f "$DAIMA_HOME/daima"
install -m755 "./build-kbuild/daima" "$TARGET_BIN"
install -m644 "./spiffs_data/ca/cacert.pem" "$CA_DIR/cacert.pem"

cp -a "./spiffs_data/web/." "$WEB_DIR/"
cp -a "./spiffs_data/skills/." "$SKILLS_DIR/"
rm -rf "$SKILLS_DIR/robot-control" \
       "$SKILLS_DIR/feishu-card-writer" \
       "$SKILLS_DIR/pet-director"

for pet_dir in ./spiffs_data/*.codex-pet; do
    [ -d "$pet_dir" ] || continue
    cp -a "$pet_dir" "$DAIMA_HOME/spiffs_data/"
done

install -m644 "./spiffs_data/config/config.example.json" "$CONFIG_DIR/config.example.json"
if [ ! -e "$CONFIG_DIR/config.json" ]; then
    if [ -f "./spiffs_data/config/config.json" ]; then
        install -Dm644 "./spiffs_data/config/config.json" "$CONFIG_DIR/config.json"
    else
        install -Dm644 "./spiffs_data/config/config.example.json" "$CONFIG_DIR/config.json"
    fi
elif ! grep -q '"vector"' "$CONFIG_DIR/config.json"; then
    python3 - "$CONFIG_DIR/config.json" <<'PY'
import json
import sys

path = sys.argv[1]
with open(path, "r", encoding="utf-8") as f:
    data = json.load(f)
data.setdefault("vector", {})["enabled"] = False
with open(path, "w", encoding="utf-8") as f:
    json.dump(data, f, ensure_ascii=False, indent=2)
    f.write("\n")
PY
fi
copy_if_missing "./spiffs_data/config/BOOTSTRAP.md" "$CONFIG_DIR/BOOTSTRAP.md"
copy_if_missing "./spiffs_data/config/IDENTITY.md" "$CONFIG_DIR/IDENTITY.md"
copy_if_missing "./spiffs_data/config/SOUL.md" "$CONFIG_DIR/SOUL.md"
copy_if_missing "./spiffs_data/config/USER.md" "$CONFIG_DIR/USER.md"
copy_if_missing "./spiffs_data/config/AGENTS.md" "$CONFIG_DIR/AGENTS.md"

ensure_path_snippet "$BASHRC"

echo ""
echo "Daima installed successfully."
echo "Home: $DAIMA_HOME"
echo "Binary: $TARGET_BIN"
echo ""
echo "If this is your first install, edit:"
echo "  $CONFIG_DIR/config.json"
echo ""
echo "Then reload your shell:"
echo "  source ~/.bashrc"
echo ""
echo "After that you can run:"
echo "  daima"
