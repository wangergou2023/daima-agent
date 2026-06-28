#!/bin/bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CHAT_ID="web_probe_sudo_blocker_$(date +%s)"
PROMPT="帮我改一下 ${ROOT_DIR} ，但我还没想好改哪个模块，你先直接开始。"
ANSWER="先不要改代码，先把范围拆清楚后再继续。"
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
      "description": "分析 drivers/tool",
      "subagent_type": "explore",
      "target_path": "$ROOT_DIR/drivers/tool",
      "prompt": "分析 $ROOT_DIR/drivers/tool 的目录结构和关键模块。只做代表性覆盖，说明工具协议、delegate_task、runtime 封装和后续建议阅读文件。"
    },
    {
      "description": "验证 sudo 权限链路",
      "subagent_type": "explore",
      "target_path": "$ROOT_DIR",
      "prompt": "验证 sudo 权限链路，并基于真实工具结果解释为什么会请求 sudo、如果用户取消会如何阻塞。不要假装执行，必须基于 preflight_tool 的真实输出总结。",
      "preflight_tool": {
        "tool_name": "terminal",
        "input": {
          "command": "sudo ls /root",
          "workdir": "$ROOT_DIR"
        },
        "continue_on_error": false
      }
    }
  ]
}
EOF

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
  --auto-interactive-directive-file "$DIRECTIVE_FILE" \
  --auto-sudo-cancel \
  --require-parent-question-request \
  --require-interview-reply \
  --require-child-session-frames \
  --require-sudo-request-with-context \
  --require-mixed-parent-child-blockers \
  --require-blocker-signal \
  --print-summary
