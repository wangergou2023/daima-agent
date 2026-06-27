#!/bin/bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

AGENT_HOME="${AGENT_HOME:-$HOME/.agent-data}"
BIN_DIR="$AGENT_HOME/bin"
CONFIG_DIR="$AGENT_HOME/spiffs_data/config"
WEB_DIR="$AGENT_HOME/spiffs_data/web"
SKILLS_DIR="$AGENT_HOME/spiffs_data/skills"
CA_DIR="$AGENT_HOME/spiffs_data/ca"
RUN_DIR="$AGENT_HOME/run"
TARGET_BIN="$BIN_DIR/agent"
PID_FILE="$RUN_DIR/agent.pid"
LOG_FILE="${TMPDIR:-/tmp}/daima-agent-runtime.log"
BASHRC="$HOME/.bashrc"

ensure_parent_dir() {
    local target="$1"
    mkdir -p "$(dirname "$target")"
}

install_data_file() {
    local src="$1"
    local dst="$2"
    ensure_parent_dir "$dst"
    install -m644 "$src" "$dst"
}

copy_if_missing() {
    local src="$1"
    local dst="$2"
    if [ ! -e "$dst" ]; then
        install_data_file "$src" "$dst"
    fi
}

ensure_path_snippet() {
    local rc_file="$1"
    local begin="# >>> agent >>>"
    local end="# <<< agent <<<"

    [ -f "$rc_file" ] || touch "$rc_file"

    if grep -Fq "$begin" "$rc_file"; then
        return
    fi

    cat >> "$rc_file" <<'EOF'

# >>> agent >>>
if [ -d "$HOME/.agent-data/bin" ] && [[ ":$PATH:" != *":$HOME/.agent-data/bin:"* ]]; then
    export PATH="$HOME/.agent-data/bin:$PATH"
fi
# <<< agent <<<
EOF
}

resolve_web_port() {
    local config_path="$1"
    python3 - "$config_path" <<'PY'
import json
import sys

path = sys.argv[1]
default = 1234
try:
    with open(path, "r", encoding="utf-8") as f:
        data = json.load(f)
    value = int(data.get("common", {}).get("web_port", default))
    print(value if 1 <= value <= 65535 else default)
except Exception:
    print(default)
PY
}

stop_existing_agent() {
    local pid=""
    if [ -f "$PID_FILE" ]; then
        pid="$(cat "$PID_FILE" 2>/dev/null || true)"
        if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
            kill "$pid" 2>/dev/null || true
            sleep 1
        fi
        rm -f "$PID_FILE"
    fi

    pkill -f "$SCRIPT_DIR/build-kbuild/agent" 2>/dev/null || true
    pkill -f "$TARGET_BIN" 2>/dev/null || true
    sleep 1
}

launch_installed_agent() {
    if command -v setsid >/dev/null 2>&1; then
        nohup setsid "$TARGET_BIN" >"$LOG_FILE" 2>&1 < /dev/null &
    else
        nohup "$TARGET_BIN" >"$LOG_FILE" 2>&1 < /dev/null &
    fi
}

wait_for_agent_ready() {
    local port="$1"
    local deadline=$((SECONDS + 20))
    local health_url="http://127.0.0.1:${port}/health"

    while [ "$SECONDS" -lt "$deadline" ]; do
        if curl -fsS --max-time 2 "$health_url" >/dev/null 2>&1; then
            return 0
        fi
        sleep 0.2
    done
    return 1
}

echo "=== Building agent binary ==="
make clean
make

echo "=== Installing to $AGENT_HOME ==="
mkdir -p "$BIN_DIR" "$CONFIG_DIR" "$WEB_DIR" "$SKILLS_DIR" "$CA_DIR" "$RUN_DIR"
mkdir -p "$AGENT_HOME/spiffs_data/memory" "$AGENT_HOME/spiffs_data/sessions" "$AGENT_HOME/spiffs_data/cache"

rm -f "$TARGET_BIN"
install -m755 "./build-kbuild/agent" "$TARGET_BIN"
install_data_file "./spiffs_data/ca/cacert.pem" "$CA_DIR/cacert.pem"

cp -a "./spiffs_data/web/." "$WEB_DIR/"
cp -a "./spiffs_data/skills/." "$SKILLS_DIR/"
rm -rf "$SKILLS_DIR/robot-control" \
       "$SKILLS_DIR/feishu-card-writer" \
       "$SKILLS_DIR/pet-director"

for pet_dir in ./spiffs_data/*.codex-pet; do
    [ -d "$pet_dir" ] || continue
    cp -a "$pet_dir" "$AGENT_HOME/spiffs_data/"
done

install_data_file "./spiffs_data/config/config.example.json" "$CONFIG_DIR/config.example.json"
if [ ! -e "$CONFIG_DIR/config.json" ]; then
    if [ -f "./spiffs_data/config/config.json" ]; then
        install_data_file "./spiffs_data/config/config.json" "$CONFIG_DIR/config.json"
    else
        install_data_file "./spiffs_data/config/config.example.json" "$CONFIG_DIR/config.json"
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

WEB_PORT="$(resolve_web_port "$CONFIG_DIR/config.json")"

echo "=== Restarting installed agent on port $WEB_PORT ==="
stop_existing_agent
launch_installed_agent
NEW_PID="$!"
echo "$NEW_PID" > "$PID_FILE"

if ! wait_for_agent_ready "$WEB_PORT"; then
    echo "Agent install succeeded, but runtime health check failed: http://127.0.0.1:${WEB_PORT}/health" >&2
    if [ -f "$LOG_FILE" ]; then
        tail -n 120 "$LOG_FILE" >&2 || true
    fi
    exit 1
fi

make clean
echo ""
echo "Agent installed successfully."
echo "Home: $AGENT_HOME"
echo "Binary: $TARGET_BIN"
echo "PID: $NEW_PID"
echo "Web UI: http://127.0.0.1:${WEB_PORT}"
echo ""
echo "If this is your first install, edit:"
echo "  $CONFIG_DIR/config.json"
echo ""
echo "Then reload your shell:"
echo "  source ~/.bashrc"
echo ""
echo "After that you can run:"
echo "  agent"
