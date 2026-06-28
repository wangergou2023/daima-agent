#!/bin/bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CHAT_ID="web_probe_http_replay_$(date +%s)"
PROMPT="帮我改一下 ${ROOT_DIR} ，但我还没想好改哪个模块，你先直接开始。"
ANSWER="先只分析 kernel/turn、kernel/tooling、drivers/tool 的职责边界，不做代码修改，要求同时安排多个 subagent，最后汇总。"
DIRECTIVE_FILE="$(mktemp)"

cleanup() {
  rm -f "$DIRECTIVE_FILE"
}
trap cleanup EXIT

cat >"$DIRECTIVE_FILE" <<EOF
{
  "tasks": [
    {
      "description": "分析 kernel/turn",
      "subagent_type": "explore",
      "target_path": "$ROOT_DIR/kernel/turn",
      "prompt": "分析 $ROOT_DIR/kernel/turn 的目录结构和关键模块。只做代表性覆盖，说明主回合执行链、关键文件和下一步值得继续看的文件。"
    },
    {
      "description": "分析 kernel/tooling",
      "subagent_type": "explore",
      "target_path": "$ROOT_DIR/kernel/tooling",
      "prompt": "分析 $ROOT_DIR/kernel/tooling 的目录结构和关键模块。只做代表性覆盖，说明 delegate/coordinator/store/wake 相关边界和后续建议阅读文件。"
    },
    {
      "description": "分析 drivers/tool",
      "subagent_type": "explore",
      "target_path": "$ROOT_DIR/drivers/tool",
      "prompt": "分析 $ROOT_DIR/drivers/tool 的目录结构和关键模块。只做代表性覆盖，说明工具协议、delegate_task、runtime 封装和后续建议阅读文件。"
    }
  ]
}
EOF

cd "$ROOT_DIR"

"$ROOT_DIR/run.sh" --background >/tmp/daima-agent-http-replay-run.log
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
  --auto-interactive-directive-file "$DIRECTIVE_FILE" \
  --require-parent-question-request \
  --require-interview-reply \
  --require-child-session-frames \
  --require-child-session-history \
  --require-http-question-snapshot \
  --require-http-completion-snapshot \
  --require-http-history-snapshot \
  --snapshot-http-base "http://127.0.0.1:1234" \
  --print-summary
