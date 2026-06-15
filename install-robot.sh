#!/bin/bash
# install-robot.sh — 在机器人上安装 agent（从已部署的二进制文件）
# Usage: 在机器人上执行:  bash install-robot.sh

set -euo pipefail

AGENT_HOME="${AGENT_HOME:-/data/agent-data}"
BIN_DIR="$AGENT_HOME/bin"
SPIFFS_DIR="$AGENT_HOME/spiffs_data"

echo "=== Agent Robot Install ==="
echo "Home: $AGENT_HOME"

mkdir -p "$BIN_DIR" "$SPIFFS_DIR"/{config,web,skills,ca,memory,sessions,cache}

# 从当前目录复制 spiffs_data（如果存在）
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
if [ -d "$SCRIPT_DIR/spiffs_data" ]; then
    echo "Copying spiffs_data..."
    cp -a "$SCRIPT_DIR/spiffs_data/." "$SPIFFS_DIR/"
fi

echo ""
echo "=== Installed ==="
echo "Config: $SPIFFS_DIR/config/config.json"
echo ""
echo "Start: AGENT_HOME=$AGENT_HOME $BIN_DIR/agent"
