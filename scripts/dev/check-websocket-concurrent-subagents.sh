#!/bin/bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OMO_ROOT="${OMO_ROOT:-$(cd "$ROOT_DIR/../oh-my-openagent" 2>/dev/null && pwd || true)}"
CHAT_ID_A="web_probe_concurrent_a_$(date +%s)"
CHAT_ID_B="web_probe_concurrent_b_$(date +%s)"
OUT_A="$(mktemp)"
OUT_B="$(mktemp)"
RUN_LOG="/tmp/daima-agent-concurrent-run.log"

PROMPT_A="帮我分析 ${ROOT_DIR} 的目录结构和关键模块，要求同时安排多个 subagent：分别分析 kernel、drivers/tool、drivers/llm，最后汇总。"
PROMPT_B="帮我分析 ${OMO_ROOT} 的目录结构和关键模块，要求同时安排多个 subagent：分别分析 packages/omo-opencode、packages/omo-web、packages/omo-agent-service，最后汇总。"

cleanup() {
  rm -f "$OUT_A" "$OUT_B"
}
trap cleanup EXIT

if [ -z "$OMO_ROOT" ] || [ ! -d "$OMO_ROOT" ]; then
  echo "missing oh-my-openagent repo; set OMO_ROOT to a valid path" >&2
  exit 1
fi

cd "$ROOT_DIR"

"$ROOT_DIR/run.sh" --background >"$RUN_LOG"
"$ROOT_DIR/scripts/dev/ensure-agent-ready.sh" /tmp/daima-agent-runtime.log

python3 "$ROOT_DIR/scripts/dev/probe-subagent-websocket.py" \
  --host 127.0.0.1 \
  --port 1234 \
  --chat-id "$CHAT_ID_A" \
  --content "$PROMPT_A" \
  --timeout 150 \
  --wait-for-coordinator \
  --wait-for-final-response \
  --require-child-session-frames \
  --print-summary >"$OUT_A" &
PID_A=$!

python3 "$ROOT_DIR/scripts/dev/probe-subagent-websocket.py" \
  --host 127.0.0.1 \
  --port 1234 \
  --chat-id "$CHAT_ID_B" \
  --content "$PROMPT_B" \
  --timeout 150 \
  --wait-for-coordinator \
  --wait-for-final-response \
  --require-child-session-frames \
  --print-summary >"$OUT_B" &
PID_B=$!

wait "$PID_A"
wait "$PID_B"

python3 - "$OUT_A" "$OUT_B" "$CHAT_ID_A" "$CHAT_ID_B" <<'PY'
import json
import sys
from pathlib import Path

out_a, out_b, chat_a, chat_b = sys.argv[1:]


def load_lines(path: str):
    return Path(path).read_text(encoding="utf-8").splitlines()


def load_summary(lines, label: str):
    for line in reversed(lines):
        line = line.strip()
        if not line:
            continue
        try:
            data = json.loads(line)
        except Exception:
            continue
        if isinstance(data, dict) and "chat_id" in data:
            return data
    raise SystemExit(f"concurrency-check failed: missing summary json for {label}")


def collect_coordinator_ids(lines):
    ids = set()
    for line in lines:
        line = line.strip()
        if not line.startswith("{"):
            continue
        try:
            data = json.loads(line)
        except Exception:
            continue
        coordinator_id = None
        if isinstance(data, dict):
            coordinator_id = data.get("coordinator_id")
            coordinator = data.get("coordinator")
            if not coordinator_id and isinstance(coordinator, dict):
                coordinator_id = coordinator.get("coordinator_id")
        if isinstance(coordinator_id, str) and coordinator_id:
            ids.add(coordinator_id)
    return ids


def require(cond: bool, msg: str):
    if not cond:
        raise SystemExit(f"concurrency-check failed: {msg}")


lines_a = load_lines(out_a)
lines_b = load_lines(out_b)
summary_a = load_summary(lines_a, "probe A")
summary_b = load_summary(lines_b, "probe B")

require(summary_a.get("chat_id") == chat_a, "probe A summary chat_id mismatch")
require(summary_b.get("chat_id") == chat_b, "probe B summary chat_id mismatch")

for label, summary in (("probe A", summary_a), ("probe B", summary_b)):
    require(summary.get("saw_completion") is True, f"{label} did not observe coordinator completion")
    require(summary.get("saw_parent_final_response") is True, f"{label} did not observe final parent response")
    require(summary.get("saw_coordinator_status") or summary.get("saw_coordinator_output"), f"{label} did not observe coordinator progress")
    require(summary.get("saw_structured_subagent_event") is True, f"{label} did not observe structured subagent events")
    require(summary.get("saw_structured_coordinator_snapshot") is True, f"{label} did not observe structured coordinator snapshots")
    require(summary.get("saw_child_session_frames") is True, f"{label} did not observe child_session.frames")
    require(summary.get("dsml_leak") is False, f"{label} leaked DSML")
    require(summary.get("transcript_leak") is False, f"{label} leaked raw transcript markers")

ids_a = collect_coordinator_ids(lines_a)
ids_b = collect_coordinator_ids(lines_b)

require(ids_a, "probe A produced no coordinator ids")
require(ids_b, "probe B produced no coordinator ids")
require(ids_a.isdisjoint(ids_b), f"coordinator ids overlapped across chats: {sorted(ids_a & ids_b)}")

print(json.dumps({
    "chat_a": chat_a,
    "chat_b": chat_b,
    "coordinator_ids_a": sorted(ids_a),
    "coordinator_ids_b": sorted(ids_b),
    "probe_a": summary_a,
    "probe_b": summary_b,
}, ensure_ascii=False))
PY
