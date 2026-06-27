#!/bin/bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
HOST="${AGENT_READY_HOST:-127.0.0.1}"
PORT="${AGENT_READY_PORT:-1234}"
TIMEOUT="${AGENT_READY_TIMEOUT:-30}"
LOG_FILE="${1:-${TMPDIR:-/tmp}/daima-agent-runtime.log}"
HEALTH_URL="http://${HOST}:${PORT}/health"
DEADLINE=$(( $(date +%s) + TIMEOUT ))

check_health() {
  curl -fsS --max-time 2 "$HEALTH_URL" >/dev/null 2>&1
}

check_log_ready() {
  [[ -f "$LOG_FILE" ]] || return 1
  grep -Eq 'WebSocket server started on port|Agent 已就绪|All services started!' "$LOG_FILE"
}

while (( $(date +%s) < DEADLINE )); do
  if check_health && check_log_ready; then
    exit 0
  fi
  sleep 0.2
done

echo "agent-ready failed: timeout waiting for $HEALTH_URL and startup markers in $LOG_FILE" >&2
if [[ -f "$LOG_FILE" ]]; then
  tail -n 120 "$LOG_FILE" >&2 || true
fi
exit 1
