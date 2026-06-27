#!/bin/bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PID_FILE="${TMPDIR:-/tmp}/daima-agent.pid"
LOG_FILE="${TMPDIR:-/tmp}/daima-agent-runtime.log"

REBUILD=1
FOREGROUND=1
AGENT_ARGS=()

for arg in "$@"; do
  case "$arg" in
    --no-build)
      REBUILD=0
      ;;
    --background)
      FOREGROUND=0
      ;;
    *)
      AGENT_ARGS+=("$arg")
      ;;
  esac
done

launch_dev_agent() {
  if command -v setsid >/dev/null 2>&1; then
    nohup setsid "$ROOT_DIR/build-kbuild/agent" "${AGENT_ARGS[@]}" >"$LOG_FILE" 2>&1 < /dev/null &
  else
    nohup "$ROOT_DIR/build-kbuild/agent" "${AGENT_ARGS[@]}" >"$LOG_FILE" 2>&1 < /dev/null &
  fi
}

cd "$ROOT_DIR"

if [[ -f "$PID_FILE" ]]; then
  old_pid="$(cat "$PID_FILE" 2>/dev/null || true)"
  if [[ -n "${old_pid:-}" ]] && kill -0 "$old_pid" 2>/dev/null; then
    kill "$old_pid" 2>/dev/null || true
    sleep 1
  fi
  rm -f "$PID_FILE"
fi

pkill -f "$ROOT_DIR/build-kbuild/agent" 2>/dev/null || true
sleep 1

if [[ "$REBUILD" -eq 1 ]]; then
  make -j4
fi

if [[ "$FOREGROUND" -eq 1 ]]; then
  exec ./build-kbuild/agent "${AGENT_ARGS[@]}" 2>&1 | tee "$LOG_FILE"
fi

launch_dev_agent
new_pid="$!"
echo "$new_pid" > "$PID_FILE"

echo "pid=$new_pid"
echo "log=$LOG_FILE"
