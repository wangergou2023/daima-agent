#!/bin/bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CHAT_ID="web_probe_staged_subagent_$(date +%s)"
PROMPT="帮我改一下 /home/wangergou/code/github/daima-agent ，但我还没想好改哪个模块，你先直接开始。"
ANSWER="先只做架构分析，不改代码。先分析 kernel/turn 和 kernel/tooling，再基于它们做 staged 汇总。"
DIRECTIVE_FILE="$(mktemp)"

cleanup() {
  rm -f "$DIRECTIVE_FILE"
}
trap cleanup EXIT

cat >"$DIRECTIVE_FILE" <<EOF
{
  "dispatch_mode": "staged",
  "tasks": [
    {
      "task_key": "scan-turn",
      "description": "分析 kernel/turn",
      "subagent_type": "explore",
      "target_path": "$ROOT_DIR/kernel/turn",
      "prompt": "分析 $ROOT_DIR/kernel/turn 的目录结构和关键模块。只做代表性覆盖，说明主回合执行链、关键文件和下一步建议。"
    },
    {
      "task_key": "scan-tooling",
      "description": "分析 kernel/tooling",
      "subagent_type": "explore",
      "target_path": "$ROOT_DIR/kernel/tooling",
      "prompt": "分析 $ROOT_DIR/kernel/tooling 的目录结构和关键模块。只做代表性覆盖，说明 delegate/store/wake/guard 相关边界。"
    },
    {
      "task_key": "merge-architecture",
      "depends_on": [
        "scan-turn",
        "scan-tooling"
      ],
      "description": "汇总 turn 与 tooling 边界",
      "subagent_type": "oracle",
      "target_path": "$ROOT_DIR",
      "prompt": "基于前面两个子任务的结果，汇总 kernel/turn 与 kernel/tooling 的职责边界、调用关系和下一步最值得继续看的文件。不要改代码。"
    }
  ]
}
EOF

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
  --auto-interactive-directive-file "$DIRECTIVE_FILE" \
  --require-parent-question-request \
  --require-interview-reply \
  --require-child-session-frames \
  --require-staged-progress \
  --print-summary
