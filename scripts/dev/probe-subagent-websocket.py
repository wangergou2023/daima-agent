#!/usr/bin/env python3
import argparse
import base64
import json
import os
import socket
import sys
import time
import urllib.error
import urllib.parse
import urllib.request


def _masked_frame(opcode: int, payload: bytes) -> bytes:
    mask = os.urandom(4)
    header = bytearray([0x80 | (opcode & 0x0F)])
    n = len(payload)
    if n < 126:
        header.append(0x80 | n)
    elif n < 65536:
        header.append(0x80 | 126)
        header.extend(n.to_bytes(2, "big"))
    else:
        header.append(0x80 | 127)
        header.extend(n.to_bytes(8, "big"))
    masked = bytes(b ^ mask[i % 4] for i, b in enumerate(payload))
    return bytes(header) + mask + masked


def send_ws_text(sock: socket.socket, text: str) -> None:
    sock.sendall(_masked_frame(0x1, text.encode("utf-8")))


def send_ws_pong(sock: socket.socket, payload: bytes) -> None:
    sock.sendall(_masked_frame(0xA, payload))


def send_ws_close(sock: socket.socket) -> None:
    try:
        sock.sendall(_masked_frame(0x8, b""))
    except OSError:
        pass


def recv_exact(sock: socket.socket, n: int):
    payload = bytearray()
    while len(payload) < n:
        chunk = sock.recv(n - len(payload))
        if not chunk:
            return None
        payload.extend(chunk)
    return bytes(payload)


def recv_ws_frame(sock: socket.socket, timeout: int):
    sock.settimeout(timeout)
    try:
        b1 = sock.recv(1)
    except socket.timeout:
        return "timeout", b""
    if not b1:
        return None
    b2 = recv_exact(sock, 1)
    if not b2:
        return None
    opcode = b1[0] & 0x0F
    masked = b2[0] >> 7
    length = b2[0] & 0x7F
    if length == 126:
        raw = recv_exact(sock, 2)
        if not raw:
            return None
        length = int.from_bytes(raw, "big")
    elif length == 127:
        raw = recv_exact(sock, 8)
        if not raw:
            return None
        length = int.from_bytes(raw, "big")
    mask = recv_exact(sock, 4) if masked else None
    if masked and not mask:
        return None
    payload = recv_exact(sock, length)
    if payload is None:
        return None
    if masked and mask:
        payload = bytes(b ^ mask[i % 4] for i, b in enumerate(payload))
    return opcode, payload


def fetch_http_subagent_state(base_url: str, chat_id: str, timeout: int):
    normalized = str(base_url or "").rstrip("/")
    if not normalized:
        raise ValueError("base_url is required")
    url = f"{normalized}/api/subagent_state?chat_id={urllib.parse.quote(chat_id)}"
    request = urllib.request.Request(url, headers={"Cache-Control": "no-store"})
    with urllib.request.urlopen(request, timeout=timeout) as response:
        body = response.read().decode("utf-8", errors="replace")
    return json.loads(body)


def wait_for_http_snapshot_condition(base_url: str, chat_id: str, timeout: int, predicate):
    deadline = time.time() + max(1, timeout)
    last_error = ""
    while time.time() < deadline:
        try:
            snapshot = fetch_http_subagent_state(base_url, chat_id, min(5, max(1, timeout)))
            if predicate(snapshot):
                return True, snapshot, last_error
        except Exception as exc:
            last_error = str(exc)
        time.sleep(0.2)
    return False, None, last_error


def snapshot_has_child_session_frames(snapshot: dict) -> bool:
    coordinators = snapshot.get("coordinators")
    if not isinstance(coordinators, list):
        return False
    for coordinator in coordinators:
        agents = coordinator.get("agents") if isinstance(coordinator, dict) else None
        if not isinstance(agents, list):
            continue
        for agent in agents:
            child = agent.get("child_session") if isinstance(agent, dict) else None
            if isinstance(child, dict) and isinstance(child.get("frames"), list) and child.get("frames"):
                return True
    return False


def snapshot_has_child_session_history(snapshot: dict) -> bool:
    coordinators = snapshot.get("coordinators")
    if not isinstance(coordinators, list):
        return False
    for coordinator in coordinators:
        agents = coordinator.get("agents") if isinstance(coordinator, dict) else None
        if not isinstance(agents, list):
            continue
        for agent in agents:
            child = agent.get("child_session") if isinstance(agent, dict) else None
            if isinstance(child, dict) and isinstance(child.get("history"), list) and child.get("history"):
                return True
    return False


def snapshot_has_pending_request(snapshot: dict, request_type: str | None = None) -> bool:
    coordinators = snapshot.get("coordinators")
    if not isinstance(coordinators, list):
        return False
    for coordinator in coordinators:
        agents = coordinator.get("agents") if isinstance(coordinator, dict) else None
        if not isinstance(agents, list):
            continue
        for agent in agents:
            pending = agent.get("pending_request") if isinstance(agent, dict) else None
            if not isinstance(pending, dict):
                continue
            pending_type = str(pending.get("request_type", "")).strip()
            pending_id = str(pending.get("request_id", "")).strip()
            if not pending_type or not pending_id:
                continue
            if request_type and pending_type != request_type:
                continue
            return True
    return False


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=1234)
    parser.add_argument("--chat-id", default=f"web_probe_{int(time.time())}")
    parser.add_argument("--content", required=True)
    parser.add_argument(
        "--raw-message-type",
        default="message",
        help="Websocket message type to send. Defaults to message; use tool_call for structured tool probes.",
    )
    parser.add_argument("--timeout", type=int, default=90)
    parser.add_argument(
        "--auto-sudo-password",
        default="",
        help="Automatically reply to sudo_request with this password. Use empty string with --auto-sudo-cancel to cancel.",
    )
    parser.add_argument(
        "--auto-sudo-cancel",
        action="store_true",
        help="Automatically cancel sudo_request instead of submitting a password.",
    )
    parser.add_argument(
        "--wait-for-coordinator",
        action="store_true",
        help="Keep the websocket open after the first response and wait for coordinator_done.",
    )
    parser.add_argument(
        "--wait-for-final-response",
        action="store_true",
        help="After coordinator_done, keep the websocket open and wait for a later final parent response.",
    )
    parser.add_argument(
        "--auto-interactive-answer",
        default="",
        help="Automatically reply to interactive_request(question_text) with this answer.",
    )
    parser.add_argument(
        "--auto-interactive-directive",
        default="",
        help="JSON object string to attach as delegate_directive on interactive_reply.",
    )
    parser.add_argument(
        "--auto-interactive-directive-file",
        default="",
        help="Path to a JSON file whose object content will be attached as delegate_directive on interactive_reply.",
    )
    parser.add_argument(
        "--auto-interactive-cancel",
        action="store_true",
        help="Automatically cancel interactive_request(question_text).",
    )
    parser.add_argument(
        "--require-parent-question-request",
        action="store_true",
        help="Fail unless a parent-level interactive_request(question_text) is observed before execution continues.",
    )
    parser.add_argument(
        "--require-interview-reply",
        action="store_true",
        help="Fail unless the probe actually sends an interactive_reply for question_text.",
    )
    parser.add_argument(
        "--require-child-session-frames",
        action="store_true",
        help="Fail unless coordinator snapshots include child_session.frames.",
    )
    parser.add_argument(
        "--require-child-session-history",
        action="store_true",
        help="Fail unless coordinator snapshots include child_session.history.",
    )
    parser.add_argument(
        "--print-summary",
        action="store_true",
        help="Print a final JSON summary of what the probe observed.",
    )
    parser.add_argument(
        "--snapshot-http-base",
        default="",
        help="Optional HTTP base URL used to verify /api/subagent_state during the websocket run, for example http://127.0.0.1:1234",
    )
    parser.add_argument(
        "--require-http-question-snapshot",
        action="store_true",
        help="Fail unless /api/subagent_state shows a pending parent question_text request before the probe replies.",
    )
    parser.add_argument(
        "--require-http-completion-snapshot",
        action="store_true",
        help="Fail unless /api/subagent_state shows child_session.frames after coordinator completion.",
    )
    parser.add_argument(
        "--require-http-history-snapshot",
        action="store_true",
        help="Fail unless /api/subagent_state shows child_session.history after coordinator completion.",
    )
    parser.add_argument(
        "--require-sudo-request-with-context",
        action="store_true",
        help="Fail unless a sudo request is observed with delegated task/session/coordinator context.",
    )
    parser.add_argument(
        "--require-blocker-signal",
        action="store_true",
        help="Fail unless a blocker-related subagent/coordinator signal is observed.",
    )
    parser.add_argument(
        "--require-mixed-parent-child-blockers",
        action="store_true",
        help="Fail unless the same websocket run observes both a parent question_text interview and a delegated sudo/task blocker.",
    )
    parser.add_argument(
        "--require-staged-progress",
        action="store_true",
        help="Fail unless coordinator progress snapshots show a staged dependency state with queued work before final completion.",
    )
    args = parser.parse_args()
    interactive_directive = None
    if args.auto_interactive_directive_file:
        with open(args.auto_interactive_directive_file, "r", encoding="utf-8") as fh:
            interactive_directive = json.load(fh)
    elif args.auto_interactive_directive:
        interactive_directive = json.loads(args.auto_interactive_directive)
    if interactive_directive is not None and not isinstance(interactive_directive, dict):
        print("probe failed: interactive directive must be a JSON object", file=sys.stderr)
        return 2

    sock = socket.create_connection((args.host, args.port), timeout=10)
    key = base64.b64encode(os.urandom(16)).decode()
    req = (
        f"GET / HTTP/1.1\r\n"
        f"Host: {args.host}:{args.port}\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        f"Sec-WebSocket-Key: {key}\r\n"
        "Sec-WebSocket-Version: 13\r\n\r\n"
    )
    sock.sendall(req.encode())
    resp = sock.recv(4096)
    if b"101 Switching Protocols" not in resp:
        print("handshake failed", file=sys.stderr)
        return 2

    send_ws_text(
        sock,
        json.dumps(
            {"type": args.raw_message_type, "chat_id": args.chat_id, "content": args.content},
            ensure_ascii=False,
        ),
    )

    frames = []
    saw_start_response = False
    saw_start_event = False
    saw_completion = False
    saw_sync_final_response = False
    saw_parent_final_response = False
    saw_coordinator_status = False
    saw_coordinator_output = False
    saw_structured_subagent_event = False
    saw_structured_coordinator_snapshot = False
    saw_child_session_snapshot = False
    saw_child_session_frames = False
    saw_child_session_history = False
    saw_blocker_signal = False
    saw_sudo_request = False
    saw_sudo_request_with_task_context = False
    saw_permission_unblocked = False
    saw_interactive_request = False
    saw_question_request = False
    saw_question_request_with_task_context = False
    saw_parent_question_request = False
    saw_interactive_reply_sent = False
    dsml_leak = False
    transcript_leak = False
    deadline = time.time() + args.timeout
    saw_completion_at = None
    saw_http_question_snapshot = False
    saw_http_completion_snapshot = False
    saw_http_history_snapshot = False
    last_http_snapshot_error = ""
    saw_staged_progress = False
    saw_staged_dependency_child = False

    def note_staged_progress_from_coordinator(coordinator: dict) -> None:
        nonlocal saw_staged_progress, saw_staged_dependency_child
        if not isinstance(coordinator, dict):
            return
        dispatch_mode = str(coordinator.get("dispatch_mode", "")).strip()
        queued_count = coordinator.get("queued_count")
        running_count = coordinator.get("running_count")
        completed_count = coordinator.get("completed_count")
        if dispatch_mode != "staged":
            return
        agents = coordinator.get("agents")
        if isinstance(agents, list):
            for agent in agents:
                if not isinstance(agent, dict):
                    continue
                depends_on = str(agent.get("depends_on", "")).strip()
                status = str(agent.get("status", "")).strip()
                if depends_on and status in ("running", "done"):
                    saw_staged_dependency_child = True
                    saw_staged_progress = True
        try:
            queued_num = int(queued_count)
            running_num = int(running_count)
            completed_num = int(completed_count)
        except Exception:
            return
        if queued_num > 0 and (running_num > 0 or completed_num > 0):
            saw_staged_progress = True

    while time.time() < deadline:
        frame = recv_ws_frame(sock, min(5, max(1, int(deadline - time.time()))))
        if frame is None:
            break
        if frame[0] == "timeout":
            continue
        opcode, payload = frame
        if opcode == 0x9:
            send_ws_pong(sock, payload)
            continue
        if opcode == 0xA:
            continue
        if opcode == 0x8:
            break
        text = payload.decode("utf-8", errors="ignore")
        frames.append(text)
        try:
            data = json.loads(text)
        except Exception:
            continue
        msg_type = data.get("type")
        if msg_type == "response":
            content = str(data.get("content", ""))
            if "<｜｜DSML｜｜" in content:
                dsml_leak = True
            if any(token in content for token in ("FILE:", "SEARCH:", "<bash>", "<fileio>", "<tool>", "<read-file", "```bash", "```json", "```shell")):
                transcript_leak = True
            if saw_start_response and args.wait_for_final_response and saw_completion:
                saw_parent_final_response = True
                break
            saw_start_response = True
            saw_start_event = True
            if not args.wait_for_coordinator and not args.wait_for_final_response:
                saw_sync_final_response = True
                break
            continue
        if msg_type == "sudo_request":
            saw_sudo_request = True
            if data.get("task_id") or data.get("session_id") or data.get("coordinator_id"):
                saw_sudo_request_with_task_context = True
            if args.auto_sudo_password or args.auto_sudo_cancel:
                send_ws_text(
                    sock,
                    json.dumps(
                        {
                            "type": "sudo_password",
                            "chat_id": data.get("chat_id", args.chat_id),
                            "session_id": data.get("session_id", ""),
                            "task_id": data.get("task_id", ""),
                            "coordinator_id": data.get("coordinator_id", ""),
                            "request_id": data.get("request_id", ""),
                            "password": "" if args.auto_sudo_cancel else args.auto_sudo_password,
                            "cancelled": bool(args.auto_sudo_cancel),
                        },
                        ensure_ascii=False,
                    ),
                )
            continue
        if msg_type == "interactive_request":
            saw_interactive_request = True
            request_type = data.get("request_type", "")
            if request_type == "question_text":
                saw_question_request = True
                if not saw_start_event:
                    saw_start_event = True
                if data.get("task_id") or data.get("session_id") or data.get("coordinator_id"):
                    saw_question_request_with_task_context = True
                else:
                    saw_parent_question_request = True
                if args.snapshot_http_base and args.require_http_question_snapshot:
                    ok, _, error = wait_for_http_snapshot_condition(
                        args.snapshot_http_base,
                        args.chat_id,
                        5,
                        lambda snapshot: (
                            snapshot_has_pending_request(snapshot, "question_text") or
                            (
                                isinstance(snapshot, dict) and
                                isinstance(snapshot.get("pending_request"), dict) and
                                str(snapshot["pending_request"].get("request_type", "")).strip() == "question_text" and
                                str(snapshot["pending_request"].get("request_id", "")).strip() != ""
                            )
                        ),
                    )
                    saw_http_question_snapshot = ok
                    if error:
                        last_http_snapshot_error = error
                if args.auto_interactive_answer or args.auto_interactive_cancel:
                    reply_payload = {
                        "type": "interactive_reply",
                        "chat_id": data.get("chat_id", args.chat_id),
                        "session_id": data.get("session_id", ""),
                        "task_id": data.get("task_id", ""),
                        "coordinator_id": data.get("coordinator_id", ""),
                        "request_type": request_type,
                        "request_id": data.get("request_id", ""),
                        "value": "" if args.auto_interactive_cancel else args.auto_interactive_answer,
                        "cancelled": bool(args.auto_interactive_cancel),
                    }
                    if interactive_directive is not None:
                        reply_payload["delegate_directive"] = interactive_directive
                    send_ws_text(
                        sock,
                        json.dumps(reply_payload, ensure_ascii=False),
                    )
                    saw_interactive_reply_sent = True
            elif request_type == "sudo_password" and (args.auto_sudo_password or args.auto_sudo_cancel):
                send_ws_text(
                    sock,
                    json.dumps(
                        {
                            "type": "interactive_reply",
                            "chat_id": data.get("chat_id", args.chat_id),
                            "session_id": data.get("session_id", ""),
                            "task_id": data.get("task_id", ""),
                            "coordinator_id": data.get("coordinator_id", ""),
                            "request_type": request_type,
                            "request_id": data.get("request_id", ""),
                            "value": "" if args.auto_sudo_cancel else args.auto_sudo_password,
                            "password": "" if args.auto_sudo_cancel else args.auto_sudo_password,
                            "cancelled": bool(args.auto_sudo_cancel),
                        },
                        ensure_ascii=False,
                    ),
                )
                saw_interactive_reply_sent = True
            continue
        if msg_type in ("subagent_start", "subagent_done", "subagent_progress", "subagent_blocked", "subagent_unblocked"):
            if (
                data.get("subagent_type")
                and "status" in data
                and "coordinator_id" in data
            ):
                saw_structured_subagent_event = True
            if data.get("blocker_kind") or data.get("blocker_text") or msg_type in ("subagent_blocked", "subagent_unblocked"):
                saw_blocker_signal = True
            if msg_type == "subagent_unblocked" and data.get("blocker_kind", "") in ("", "permission"):
                saw_permission_unblocked = True
            continue
        if msg_type == "coordinator_status":
            saw_coordinator_status = True
            coordinator = data.get("coordinator")
            if isinstance(coordinator, dict):
                note_staged_progress_from_coordinator(coordinator)
            if (
                isinstance(coordinator, dict)
                and coordinator.get("coordinator_id")
                and isinstance(coordinator.get("agents"), list)
                and "status" in coordinator
            ):
                saw_structured_coordinator_snapshot = True
                agents = coordinator.get("agents")
                if isinstance(agents, list):
                    for agent in agents:
                        child = agent.get("child_session") if isinstance(agent, dict) else None
                        if (
                            isinstance(child, dict)
                            and isinstance(child.get("commits"), list)
                            and isinstance(child.get("pending_queue"), dict)
                            and "summary" in child
                        ):
                            saw_child_session_snapshot = True
                        if isinstance(child, dict) and isinstance(child.get("frames"), list):
                            saw_child_session_frames = True
                        if isinstance(child, dict) and isinstance(child.get("history"), list) and child.get("history"):
                            saw_child_session_history = True
            if isinstance(coordinator, dict) and (coordinator.get("blocker_kind") or coordinator.get("blocker_text")):
                saw_blocker_signal = True
            content = json.dumps(data, ensure_ascii=False)
            if "<｜｜DSML｜｜" in content:
                dsml_leak = True
            if any(token in content for token in ("FILE:", "SEARCH:", "<bash>", "<fileio>", "<tool>", "<read-file", "```bash", "```json", "```shell")):
                transcript_leak = True
            continue
        if msg_type == "coordinator_output":
            saw_coordinator_output = True
            coordinator = data.get("coordinator")
            if isinstance(coordinator, dict):
                note_staged_progress_from_coordinator(coordinator)
            if (
                isinstance(coordinator, dict)
                and coordinator.get("coordinator_id")
                and isinstance(coordinator.get("agents"), list)
                and "status" in coordinator
            ):
                saw_structured_coordinator_snapshot = True
                agents = coordinator.get("agents")
                if isinstance(agents, list):
                    for agent in agents:
                        child = agent.get("child_session") if isinstance(agent, dict) else None
                        if (
                            isinstance(child, dict)
                            and isinstance(child.get("commits"), list)
                            and isinstance(child.get("pending_queue"), dict)
                            and "summary" in child
                        ):
                            saw_child_session_snapshot = True
                        if isinstance(child, dict) and isinstance(child.get("frames"), list):
                            saw_child_session_frames = True
                        if isinstance(child, dict) and isinstance(child.get("history"), list) and child.get("history"):
                            saw_child_session_history = True
            if isinstance(coordinator, dict) and (coordinator.get("blocker_kind") or coordinator.get("blocker_text")):
                saw_blocker_signal = True
            content = json.dumps(data, ensure_ascii=False)
            if "<｜｜DSML｜｜" in content:
                dsml_leak = True
            if any(token in content for token in ("FILE:", "SEARCH:", "<bash>", "<fileio>", "<tool>", "<read-file", "```bash", "```json", "```shell")):
                transcript_leak = True
            continue
        if msg_type == "coordinator_done":
            saw_completion = True
            saw_completion_at = time.time()
            if args.snapshot_http_base and args.require_http_completion_snapshot:
                ok, _, error = wait_for_http_snapshot_condition(
                    args.snapshot_http_base,
                    args.chat_id,
                    5,
                    snapshot_has_child_session_frames,
                )
                saw_http_completion_snapshot = ok
                if error:
                    last_http_snapshot_error = error
            if args.snapshot_http_base and args.require_http_history_snapshot:
                ok, _, error = wait_for_http_snapshot_condition(
                    args.snapshot_http_base,
                    args.chat_id,
                    5,
                    snapshot_has_child_session_history,
                )
                saw_http_history_snapshot = ok
                if error:
                    last_http_snapshot_error = error
            content = json.dumps(data, ensure_ascii=False)
            if "<｜｜DSML｜｜" in content:
                dsml_leak = True
            if any(token in content for token in ("FILE:", "SEARCH:", "<bash>", "<fileio>", "<tool>", "<read-file", "```bash", "```json", "```shell")):
                transcript_leak = True
            if not args.wait_for_final_response:
                break
            continue

    send_ws_close(sock)
    sock.close()

    for frame in frames:
        print(frame)

    if not saw_start_event:
        print("probe failed: no initial websocket start event observed", file=sys.stderr)
        return 3
    if args.wait_for_coordinator:
        if not saw_completion:
            print("probe failed: coordinator completion was not observed", file=sys.stderr)
            return 4
        if args.wait_for_final_response and not saw_parent_final_response:
            when = f" after {int(time.time() - saw_completion_at)}s" if saw_completion_at else ""
            print(f"probe failed: final parent response was not observed{when}", file=sys.stderr)
            return 10
    elif not saw_completion and not saw_sync_final_response:
        print("probe failed: neither sync final response nor coordinator completion observed", file=sys.stderr)
        return 4
    if dsml_leak:
        print("probe failed: websocket output still contains DSML", file=sys.stderr)
        return 5
    if transcript_leak:
        print("probe failed: websocket output still contains raw transcript markers", file=sys.stderr)
        return 6
    if args.wait_for_coordinator and not saw_structured_subagent_event:
        print("probe failed: no structured subagent events observed", file=sys.stderr)
        return 7
    if args.wait_for_coordinator and not saw_coordinator_status and not saw_coordinator_output:
        print("probe failed: no coordinator progress events observed before completion", file=sys.stderr)
        return 8
    if args.wait_for_coordinator and not saw_structured_coordinator_snapshot:
        print("probe failed: coordinator progress events did not include structured coordinator snapshots", file=sys.stderr)
        return 9
    if args.wait_for_coordinator and not saw_child_session_snapshot:
        print("probe failed: coordinator snapshots did not include child_session summaries/commits/queues", file=sys.stderr)
        return 14
    if args.require_child_session_frames and not saw_child_session_frames:
        print("probe failed: coordinator snapshots did not include child_session.frames", file=sys.stderr)
        return 15
    if args.require_child_session_history and not saw_child_session_history:
        print("probe failed: coordinator snapshots did not include child_session.history", file=sys.stderr)
        return 24
    if args.require_sudo_request_with_context and not saw_sudo_request_with_task_context:
        print("probe failed: required delegated sudo request with task/session/coordinator context was not observed", file=sys.stderr)
        return 18
    if args.require_blocker_signal and not saw_blocker_signal:
        print("probe failed: required blocker signal was not observed", file=sys.stderr)
        return 19
    if args.require_mixed_parent_child_blockers and not (saw_parent_question_request and saw_sudo_request_with_task_context):
        print("probe failed: required mixed parent-question and delegated child-blocker flow was not observed", file=sys.stderr)
        return 20
    if args.require_staged_progress and not saw_staged_progress:
        print("probe failed: required staged coordinator progress with queued dependencies was not observed", file=sys.stderr)
        return 23
    if saw_sudo_request and not saw_sudo_request_with_task_context:
        print("probe failed: sudo_request observed without delegated task/session/coordinator context", file=sys.stderr)
        return 11
    if saw_question_request and not (saw_question_request_with_task_context or saw_parent_question_request):
        print("probe failed: question_text interactive_request observed without recognized parent/delegated context", file=sys.stderr)
        return 12
    if (args.auto_interactive_answer or args.auto_interactive_cancel) and saw_question_request and not saw_interactive_reply_sent:
        print("probe failed: question_text interactive_request observed but no interactive_reply was sent", file=sys.stderr)
        return 13
    if args.require_parent_question_request and not saw_parent_question_request:
        print("probe failed: required parent-level question_text interactive_request was not observed", file=sys.stderr)
        return 16
    if args.require_interview_reply and not saw_interactive_reply_sent:
        print("probe failed: required question_text interactive_reply was not sent", file=sys.stderr)
        return 17
    if args.require_http_question_snapshot and not saw_http_question_snapshot:
        detail = f" ({last_http_snapshot_error})" if last_http_snapshot_error else ""
        print(f"probe failed: /api/subagent_state did not expose pending parent question_text request before reply{detail}", file=sys.stderr)
        return 21
    if args.require_http_completion_snapshot and not saw_http_completion_snapshot:
        detail = f" ({last_http_snapshot_error})" if last_http_snapshot_error else ""
        print(f"probe failed: /api/subagent_state did not expose child_session.frames after completion{detail}", file=sys.stderr)
        return 22
    if args.require_http_history_snapshot and not saw_http_history_snapshot:
        detail = f" ({last_http_snapshot_error})" if last_http_snapshot_error else ""
        print(f"probe failed: /api/subagent_state did not expose child_session.history after completion{detail}", file=sys.stderr)
        return 25
    if args.print_summary:
        print(json.dumps(
            {
                "chat_id": args.chat_id,
                "saw_start_response": saw_start_response,
                "saw_sync_final_response": saw_sync_final_response,
                "saw_completion": saw_completion,
                "saw_parent_final_response": saw_parent_final_response,
                "saw_coordinator_status": saw_coordinator_status,
                "saw_coordinator_output": saw_coordinator_output,
                "saw_structured_subagent_event": saw_structured_subagent_event,
                "saw_structured_coordinator_snapshot": saw_structured_coordinator_snapshot,
                "saw_child_session_snapshot": saw_child_session_snapshot,
                "saw_child_session_frames": saw_child_session_frames,
                "saw_child_session_history": saw_child_session_history,
                "saw_parent_question_request": saw_parent_question_request,
                "saw_question_request_with_task_context": saw_question_request_with_task_context,
                "saw_sudo_request_with_task_context": saw_sudo_request_with_task_context,
                "saw_interactive_reply_sent": saw_interactive_reply_sent,
                "saw_blocker_signal": saw_blocker_signal,
                "saw_permission_unblocked": saw_permission_unblocked,
                "saw_http_question_snapshot": saw_http_question_snapshot,
                "saw_http_completion_snapshot": saw_http_completion_snapshot,
                "saw_http_history_snapshot": saw_http_history_snapshot,
                "saw_staged_progress": saw_staged_progress,
                "saw_staged_dependency_child": saw_staged_dependency_child,
                "last_http_snapshot_error": last_http_snapshot_error,
                "dsml_leak": dsml_leak,
                "transcript_leak": transcript_leak,
            },
            ensure_ascii=False,
        ))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
