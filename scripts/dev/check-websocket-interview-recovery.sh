#!/bin/bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CHAT_ID="web_probe_interview_recovery_$(date +%s)"
PROMPT="帮我改一下 /home/wangergou/code/github/daima-agent ，但我还没想好改哪个模块，你先直接开始。"
ANSWER="先只分析 kernel/turn、kernel/tooling、drivers/tool 的职责边界，不做代码修改，要求同时安排多个 subagent，最后汇总。"

cd "$ROOT_DIR"

"$ROOT_DIR/run.sh" --background >/tmp/daima-agent-interview-run.log
"$ROOT_DIR/scripts/dev/ensure-agent-ready.sh" /tmp/daima-agent-runtime.log

python3 "$ROOT_DIR/scripts/dev/probe-subagent-websocket.py" \
  --host 127.0.0.1 \
  --port 1234 \
  --chat-id "$CHAT_ID" \
  --content "$PROMPT" \
  --timeout 120 \
  --wait-for-final-response \
  --auto-interactive-answer "$ANSWER" \
  --require-parent-question-request \
  --require-interview-reply \
  --require-child-session-frames \
  --print-summary
