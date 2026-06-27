#!/usr/bin/env python3
import argparse
import base64
import json
import os
import socket
import sys
import time
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


def connect_ws(host: str, port: int) -> socket.socket:
    sock = socket.create_connection((host, port), timeout=10)
    key = base64.b64encode(os.urandom(16)).decode()
    req = (
        f"GET / HTTP/1.1\r\n"
        f"Host: {host}:{port}\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        f"Sec-WebSocket-Key: {key}\r\n"
        "Sec-WebSocket-Version: 13\r\n\r\n"
    )
    sock.sendall(req.encode())
    resp = sock.recv(4096)
    if b"101 Switching Protocols" not in resp:
        raise RuntimeError("websocket handshake failed")
    return sock


def fetch_json(url: str, timeout: int):
    req = urllib.request.Request(url, headers={"Cache-Control": "no-store"})
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        body = resp.read().decode("utf-8", errors="replace")
    return json.loads(body)


def fetch_subagent_state(base_url: str, chat_id: str, timeout: int):
    url = f"{base_url.rstrip('/')}/api/subagent_state?chat_id={urllib.parse.quote(chat_id)}"
    return fetch_json(url, timeout)


def fetch_session_history(base_url: str, chat_id: str, timeout: int):
    url = f"{base_url.rstrip('/')}/api/session_history?chat_id={urllib.parse.quote(chat_id)}"
    return fetch_json(url, timeout)


def wait_for(predicate, timeout: int, interval: float = 0.2):
    deadline = time.time() + timeout
    while time.time() < deadline:
        value = predicate()
        if value:
            return value
        time.sleep(interval)
    return None


def snapshot_has_pending_question(snapshot: dict) -> bool:
    pending = snapshot.get("pending_request")
    return (
        isinstance(pending, dict)
        and str(pending.get("request_type", "")).strip() == "question_text"
        and str(pending.get("request_id", "")).strip() != ""
    )


def snapshot_has_child_frames(snapshot: dict) -> bool:
    coordinators = snapshot.get("coordinators")
    if not isinstance(coordinators, list):
        return False
    for coordinator in coordinators:
        if not isinstance(coordinator, dict):
            continue
        agents = coordinator.get("agents")
        if not isinstance(agents, list):
            continue
        for agent in agents:
            child = agent.get("child_session") if isinstance(agent, dict) else None
            if isinstance(child, dict) and isinstance(child.get("frames"), list) and child.get("frames"):
                return True
    return False


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=1234)
    parser.add_argument("--chat-id", required=True)
    parser.add_argument("--content", required=True)
    parser.add_argument("--answer", required=True)
    parser.add_argument("--http-base", default="http://127.0.0.1:1234")
    parser.add_argument("--timeout", type=int, default=120)
    args = parser.parse_args()

    phase1_frames = []
    phase2_frames = []
    request_id = ""
    prompt_text = ""
    saw_completion = False
    saw_parent_final_response = False
    saw_coordinator_status = False
    saw_coordinator_output = False
    saw_child_session_frames = False
    dsml_leak = False
    transcript_leak = False

    sock = connect_ws(args.host, args.port)
    send_ws_text(
        sock,
        json.dumps(
            {"type": "message", "chat_id": args.chat_id, "content": args.content},
            ensure_ascii=False,
        ),
    )

    deadline = time.time() + args.timeout
    while time.time() < deadline:
        frame = recv_ws_frame(sock, 5)
        if frame is None:
            break
        if frame[0] == "timeout":
            continue
        opcode, payload = frame
        if opcode == 0x9:
            send_ws_pong(sock, payload)
            continue
        if opcode in (0x8, 0xA):
            continue
        text = payload.decode("utf-8", errors="ignore")
        phase1_frames.append(text)
        try:
            data = json.loads(text)
        except Exception:
            continue
        if data.get("type") == "interactive_request" and data.get("request_type") == "question_text":
            request_id = str(data.get("request_id", "")).strip()
            prompt_text = str(data.get("prompt", "")).strip()
            if request_id:
                break

    send_ws_close(sock)
    sock.close()

    if not request_id:
        print("reconnect-probe failed: did not observe parent question_text request", file=sys.stderr)
        return 2

    snapshot = wait_for(
        lambda: (
            fetch_subagent_state(args.http_base, args.chat_id, 5)
            if snapshot_has_pending_question(fetch_subagent_state(args.http_base, args.chat_id, 5))
            else None
        ),
        timeout=10,
    )
    if not snapshot:
        print("reconnect-probe failed: HTTP subagent snapshot did not retain pending parent question", file=sys.stderr)
        return 3

    history = fetch_session_history(args.http_base, args.chat_id, 5)
    messages = history.get("messages") if isinstance(history, dict) else None
    history_count = len(messages) if isinstance(messages, list) else 0

    sock = connect_ws(args.host, args.port)
    send_ws_text(
        sock,
        json.dumps(
            {
                "type": "interactive_reply",
                "chat_id": args.chat_id,
                "session_id": "",
                "task_id": "",
                "coordinator_id": "",
                "request_type": "question_text",
                "request_id": request_id,
                "value": args.answer,
                "cancelled": False,
            },
            ensure_ascii=False,
        ),
    )

    while time.time() < deadline:
        frame = recv_ws_frame(sock, 5)
        if frame is None:
            break
        if frame[0] == "timeout":
            continue
        opcode, payload = frame
        if opcode == 0x9:
            send_ws_pong(sock, payload)
            continue
        if opcode in (0x8, 0xA):
            continue
        text = payload.decode("utf-8", errors="ignore")
        phase2_frames.append(text)
        try:
            data = json.loads(text)
        except Exception:
            continue

        if "<｜｜DSML｜｜" in text:
            dsml_leak = True
        if any(token in text for token in ("FILE:", "SEARCH:", "<bash>", "<fileio>", "<tool>", "```bash", "```json", "```shell")):
            transcript_leak = True

        msg_type = data.get("type")
        if msg_type == "coordinator_status":
            saw_coordinator_status = True
            coordinator = data.get("coordinator")
            if isinstance(coordinator, dict):
                agents = coordinator.get("agents")
                if isinstance(agents, list):
                    for agent in agents:
                        child = agent.get("child_session") if isinstance(agent, dict) else None
                        if isinstance(child, dict) and isinstance(child.get("frames"), list) and child.get("frames"):
                            saw_child_session_frames = True
        elif msg_type == "coordinator_output":
            saw_coordinator_output = True
            coordinator = data.get("coordinator")
            if isinstance(coordinator, dict):
                agents = coordinator.get("agents")
                if isinstance(agents, list):
                    for agent in agents:
                        child = agent.get("child_session") if isinstance(agent, dict) else None
                        if isinstance(child, dict) and isinstance(child.get("frames"), list) and child.get("frames"):
                            saw_child_session_frames = True
        elif msg_type == "coordinator_done":
            saw_completion = True
        elif msg_type == "response" and saw_completion:
            saw_parent_final_response = True
            break

    send_ws_close(sock)
    sock.close()

    final_snapshot = wait_for(
        lambda: (
            fetch_subagent_state(args.http_base, args.chat_id, 5)
            if snapshot_has_child_frames(fetch_subagent_state(args.http_base, args.chat_id, 5))
            else None
        ),
        timeout=10,
    )
    if not final_snapshot:
        print("reconnect-probe failed: HTTP subagent snapshot did not expose child_session.frames after reconnect completion", file=sys.stderr)
        return 4

    final_history = fetch_session_history(args.http_base, args.chat_id, 5)
    final_messages = final_history.get("messages") if isinstance(final_history, dict) else None
    final_history_count = len(final_messages) if isinstance(final_messages, list) else 0

    if not saw_completion:
        print("reconnect-probe failed: reconnect phase did not observe coordinator completion", file=sys.stderr)
        return 5
    if not saw_parent_final_response:
        print("reconnect-probe failed: reconnect phase did not observe final parent response", file=sys.stderr)
        return 6
    if not (saw_coordinator_status or saw_coordinator_output):
        print("reconnect-probe failed: reconnect phase did not observe coordinator progress", file=sys.stderr)
        return 7
    if not saw_child_session_frames:
        print("reconnect-probe failed: reconnect phase did not observe child_session.frames", file=sys.stderr)
        return 8
    if dsml_leak:
        print("reconnect-probe failed: DSML leaked into websocket output", file=sys.stderr)
        return 9
    if transcript_leak:
        print("reconnect-probe failed: raw transcript markers leaked into websocket output", file=sys.stderr)
        return 10

    print(json.dumps({
        "chat_id": args.chat_id,
        "request_id": request_id,
        "prompt_excerpt": prompt_text[:120],
        "history_count_before_reconnect": history_count,
        "history_count_after_reconnect": final_history_count,
        "saw_http_pending_question_snapshot": True,
        "saw_http_completion_snapshot": True,
        "saw_coordinator_status": saw_coordinator_status,
        "saw_coordinator_output": saw_coordinator_output,
        "saw_child_session_frames": saw_child_session_frames,
        "saw_completion": saw_completion,
        "saw_parent_final_response": saw_parent_final_response,
        "dsml_leak": dsml_leak,
        "transcript_leak": transcript_leak,
    }, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
