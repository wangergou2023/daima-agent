#!/bin/bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CHAT_ID="web_probe_sudo_blocker_$(date +%s)"
PROMPT="帮我改一下 /home/wangergou/code/github/daima-agent ，但我还没想好改哪个模块，你先直接开始。"
ANSWER="先不要改代码，先把范围拆清楚后再继续。"

cd "$ROOT_DIR"

"$ROOT_DIR/run.sh" --background >/tmp/daima-agent-sudo-run.log
"$ROOT_DIR/scripts/dev/ensure-agent-ready.sh" /tmp/daima-agent-runtime.log

python3 "$ROOT_DIR/scripts/dev/probe-subagent-websocket.py" \
  --host 127.0.0.1 \
  --port 1234 \
  --chat-id "$CHAT_ID" \
  --content "$PROMPT" \
  --timeout 120 \
  --wait-for-coordinator \
  --wait-for-final-response \
  --auto-interactive-answer "$ANSWER" \
  --auto-interactive-directive-file "$ROOT_DIR/scripts/dev/websocket-mixed-blockers-directive.json" \
  --auto-sudo-cancel \
  --require-parent-question-request \
  --require-interview-reply \
  --require-child-session-frames \
  --require-sudo-request-with-context \
  --require-mixed-parent-child-blockers \
  --require-blocker-signal \
  --print-summary
