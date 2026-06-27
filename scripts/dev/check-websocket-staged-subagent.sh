#!/bin/bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CHAT_ID="web_probe_staged_subagent_$(date +%s)"
PROMPT="帮我改一下 /home/wangergou/code/github/daima-agent ，但我还没想好改哪个模块，你先直接开始。"
ANSWER="先只做架构分析，不改代码。先分析 kernel/turn 和 kernel/tooling，再基于它们做 staged 汇总。"

cd "$ROOT_DIR"

"$ROOT_DIR/run.sh" --background >/tmp/daima-agent-staged-run.log
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
  --auto-interactive-directive-file "$ROOT_DIR/scripts/dev/websocket-staged-directive.json" \
  --require-parent-question-request \
  --require-interview-reply \
  --require-child-session-frames \
  --require-staged-progress \
  --print-summary
