#!/bin/bash
# install-robot.sh — 在机器人上安装 daima-agent（从已部署的二进制文件）
# Usage: 在机器人上执行:  bash install-robot.sh

set -euo pipefail

DAIMA_HOME="${DAIMA_HOME:-/data/daima}"
BIN_DIR="$DAIMA_HOME/bin"
SPIFFS_DIR="$DAIMA_HOME/spiffs_data"

echo "=== Daima Robot Install ==="
echo "Home: $DAIMA_HOME"

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
echo "Start: DAIMA_HOME=$DAIMA_HOME $BIN_DIR/daima"
