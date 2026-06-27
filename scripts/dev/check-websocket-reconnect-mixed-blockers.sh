#!/bin/bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CHAT_ID="web_probe_reconnect_mixed_$(date +%s)"
PROMPT="帮我改一下 /home/wangergou/code/github/daima-agent ，但我还没想好改哪个模块，你先直接开始。"
ANSWER="先只分析 kernel/turn、kernel/tooling、drivers/tool 的职责边界，不做代码修改，要求同时安排多个 subagent，最后汇总。"
DIRECTIVE_FILE="$ROOT_DIR/scripts/dev/websocket-mixed-blockers-directive.json"

cd "$ROOT_DIR"

"$ROOT_DIR/run.sh" --background >/tmp/daima-agent-reconnect-mixed-run.log
"$ROOT_DIR/scripts/dev/ensure-agent-ready.sh" /tmp/daima-agent-runtime.log

python3 "$ROOT_DIR/scripts/dev/probe-subagent-websocket.py" \
  --host 127.0.0.1 \
  --port 1234 \
  --chat-id "$CHAT_ID" \
  --content "$PROMPT" \
  --timeout 150 \
  --wait-for-coordinator \
  --wait-for-final-response \
  --auto-interactive-answer "$ANSWER" \
  --auto-interactive-directive-file "$DIRECTIVE_FILE" \
  --auto-sudo-cancel \
  --require-parent-question-request \
  --require-interview-reply \
  --require-child-session-frames \
  --require-child-session-history \
  --require-http-question-snapshot \
  --require-http-completion-snapshot \
  --require-http-history-snapshot \
  --require-sudo-request-with-context \
  --require-mixed-parent-child-blockers \
  --require-blocker-signal \
  --snapshot-http-base "http://127.0.0.1:1234" \
  --print-summary
