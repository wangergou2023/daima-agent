#!/bin/bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
AGENT_HOME="${AGENT_HOME:-$HOME/.agent-data}"
WORKSPACE_ROOT="${WORKSPACE_ROOT:-$AGENT_HOME/spiffs_data/workspace}"
REPO_ROOT="${OPENCODE_ROOT:-$WORKSPACE_ROOT/opencode}"
CLONE_URL="${OPENCODE_CLONE_URL:-https://github.com/sst/opencode.git}"
CHAT_ID="web_probe_opencode_$(date +%s)"
RUN_LOG="/tmp/daima-agent-opencode-run.log"
RUNTIME_LOG="/tmp/daima-agent-runtime.log"
PROBE_OUT="$(mktemp)"

cleanup() {
  rm -f "$PROBE_OUT"
}
trap cleanup EXIT

ensure_repo() {
  if [ -d "$REPO_ROOT" ] && [ -n "$(find "$REPO_ROOT" -mindepth 1 -maxdepth 1 2>/dev/null | head -n 1)" ]; then
    return 0
  fi
  if [ -e "$REPO_ROOT" ] && [ ! -d "$REPO_ROOT" ]; then
    echo "existing path is not a directory: $REPO_ROOT" >&2
    return 1
  fi
  mkdir -p "$WORKSPACE_ROOT"
  mkdir -p "$(dirname "$REPO_ROOT")"
  git clone --depth=1 "$CLONE_URL" "$REPO_ROOT"
}

dump_runtime_diag() {
  local coordinator_id="$1"
  if [ ! -f "$RUNTIME_LOG" ]; then
    return 0
  fi
  echo
  echo "=== runtime diagnostics (${coordinator_id:-no-coordinator}) ==="
  strings "$RUNTIME_LOG" | rg -n \
    "delegate_bg launch|delegate_store attach_task|${coordinator_id}|coordinator=|restore queued child|budget hold|skip claimed/nonqueued child|wake_state" \
    | tail -n 120 || true
}

ensure_repo
echo "workspace_root=$WORKSPACE_ROOT"
echo "repo_root=$REPO_ROOT"

cd "$ROOT_DIR"
"$ROOT_DIR/run.sh" --background >"$RUN_LOG"
"$ROOT_DIR/scripts/dev/ensure-agent-ready.sh" "$RUNTIME_LOG"

set +e
python3 "$ROOT_DIR/scripts/dev/probe-subagent-websocket.py" \
  --host 127.0.0.1 \
  --port 1234 \
  --chat-id "$CHAT_ID" \
  --content "帮我分析 ${REPO_ROOT} 的目录结构和关键模块，要求同时安排多个 subagent，分别分析 packages/app、packages/cli、packages/session-ui，最后汇总。" \
  --timeout 180 \
  --wait-for-coordinator \
  --wait-for-final-response \
  --require-child-session-frames \
  --require-child-session-history \
  --print-summary >"$PROBE_OUT" 2>&1
STATUS=$?
set -e

cat "$PROBE_OUT"

COORDINATOR_ID="$(
  python3 - "$PROBE_OUT" <<'PY'
import json
import sys
from pathlib import Path

lines = Path(sys.argv[1]).read_text(encoding="utf-8", errors="replace").splitlines()
coordinator_id = ""
for line in lines:
    line = line.strip()
    if not line.startswith("{"):
        continue
    try:
        data = json.loads(line)
    except Exception:
        continue
    if not isinstance(data, dict):
        continue
    if isinstance(data.get("coordinator_id"), str) and data.get("coordinator_id"):
        coordinator_id = data["coordinator_id"]
    coordinator = data.get("coordinator")
    if isinstance(coordinator, dict) and isinstance(coordinator.get("coordinator_id"), str) and coordinator.get("coordinator_id"):
        coordinator_id = coordinator["coordinator_id"]
print(coordinator_id)
PY
)"

python3 - "$PROBE_OUT" <<'PY'
import json
import sys
from pathlib import Path

lines = Path(sys.argv[1]).read_text(encoding="utf-8", errors="replace").splitlines()
last_coord = None
for line in lines:
    line = line.strip()
    if not line.startswith("{"):
        continue
    try:
        data = json.loads(line)
    except Exception:
        continue
    coord = data.get("coordinator")
    if isinstance(coord, dict):
        last_coord = coord

print()
print("=== parsed coordinator summary ===")
if not isinstance(last_coord, dict):
    print("no coordinator snapshot captured")
    sys.exit(0)

print(json.dumps({
    "coordinator_id": last_coord.get("coordinator_id"),
    "status": last_coord.get("status"),
    "agent_count": last_coord.get("agent_count"),
    "completed_count": last_coord.get("completed_count"),
    "running_count": last_coord.get("running_count"),
    "queued_count": last_coord.get("queued_count"),
    "failed_count": last_coord.get("failed_count"),
    "agents": [
        {
            "task_id": agent.get("task_id"),
            "description": agent.get("description"),
            "status": agent.get("status"),
            "scope_path": agent.get("scope_path"),
        }
        for agent in last_coord.get("agents", [])
        if isinstance(agent, dict)
    ],
}, ensure_ascii=False, indent=2))
PY

if [ "$STATUS" -ne 0 ]; then
  dump_runtime_diag "$COORDINATOR_ID"
  exit "$STATUS"
fi

dump_runtime_diag "$COORDINATOR_ID"
