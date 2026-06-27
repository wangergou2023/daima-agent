#!/bin/bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CHAT_ID="web_probe_reconnect_subagent_$(date +%s)"
PROMPT="帮我改一下 /home/wangergou/code/github/daima-agent ，但我还没想好改哪个模块，你先直接开始。"
ANSWER="先只分析 kernel/turn、kernel/tooling、drivers/tool 的职责边界，不做代码修改，要求同时安排多个 subagent，最后汇总。"

cd "$ROOT_DIR"

"$ROOT_DIR/run.sh" --background >/tmp/daima-agent-reconnect-run.log
"$ROOT_DIR/scripts/dev/ensure-agent-ready.sh" /tmp/daima-agent-runtime.log

python3 "$ROOT_DIR/scripts/dev/probe-websocket-reconnect.py" \
  --host 127.0.0.1 \
  --port 1234 \
  --chat-id "$CHAT_ID" \
  --content "$PROMPT" \
  --answer "$ANSWER" \
  --http-base "http://127.0.0.1:1234" \
  --timeout 150
