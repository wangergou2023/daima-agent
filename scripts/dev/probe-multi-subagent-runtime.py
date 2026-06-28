#!/usr/bin/env python3
import argparse
import base64
import json
import os
import socket
import struct
import sys
import time
from typing import Optional
from pathlib import Path


DEFAULT_PROMPT = (
    "请同时安排多个subagent处理这个复杂任务。\n"
    "1. 先梳理当前仓库的启动入口和主执行链路。\n"
    "2. 再单独检查多 subagent 调度和恢复链路的关键模块。\n"
    "3. 另外检查前端 websocket 刷新与会话恢复相关代码。\n"
    "最后汇总成统一结论，并指出风险和下一步建议。"
)

DEFAULT_LOG = Path.home() / ".agent-data" / "spiffs_data" / "memory" / "agent.log"


def open_ws(host: str, port: int, chat_id: str) -> socket.socket:
    key = base64.b64encode(os.urandom(16)).decode()
    sock = socket.create_connection((host, port), timeout=5)
    request = (
        f"GET /ws?chat_id={chat_id} HTTP/1.1\r\n"
        f"Host: {host}:{port}\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        f"Sec-WebSocket-Key: {key}\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n"
    )
    sock.sendall(request.encode())
    _ = sock.recv(4096)
    sock.settimeout(1.0)
    return sock


def ws_send_json(sock: socket.socket, payload_obj: dict) -> None:
    payload = json.dumps(payload_obj).encode()
    mask = os.urandom(4)
    frame = bytearray([0x81])
    payload_len = len(payload)
    if payload_len < 126:
        frame.append(0x80 | payload_len)
    elif payload_len < 65536:
        frame.append(0x80 | 126)
        frame.extend(struct.pack("!H", payload_len))
    else:
        frame.append(0x80 | 127)
        frame.extend(struct.pack("!Q", payload_len))
    frame.extend(mask)
    frame.extend(bytes(b ^ mask[i % 4] for i, b in enumerate(payload)))
    sock.sendall(frame)


def ws_read_text(sock: socket.socket, timeout_s: float) -> Optional[str]:
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        try:
            header = sock.recv(2)
        except socket.timeout:
            continue
        if not header or len(header) < 2:
            return None
        opcode = header[0] & 0x0F
        payload_len = header[1] & 0x7F
        if payload_len == 126:
            ext = sock.recv(2)
            payload_len = struct.unpack("!H", ext)[0]
        elif payload_len == 127:
            ext = sock.recv(8)
            payload_len = struct.unpack("!Q", ext)[0]
        payload = b""
        while len(payload) < payload_len:
            chunk = sock.recv(payload_len - len(payload))
            if not chunk:
                return None
            payload += chunk
        if opcode == 0x1:
            return payload.decode(errors="replace")
        if opcode == 0x9:
            ws_send_control(sock, 0xA, payload)
            continue
        if opcode == 0xA:
            continue
    return None


def ws_send_control(sock: socket.socket, opcode: int, payload: bytes = b"") -> None:
    frame = bytearray()
    frame.append(0x80 | (opcode & 0x0F))
    payload_len = len(payload)
    if payload_len < 126:
        frame.append(0x80 | payload_len)
    elif payload_len < 65536:
        frame.append(0x80 | 126)
        frame.extend(struct.pack("!H", payload_len))
    else:
        frame.append(0x80 | 127)
        frame.extend(struct.pack("!Q", payload_len))
    mask = os.urandom(4)
    frame.extend(mask)
    frame.extend(bytes(b ^ mask[i % 4] for i, b in enumerate(payload)))
    sock.sendall(frame)


def ws_collect_texts(sock: socket.socket, timeout_s: float, max_messages: int = 8) -> list[str]:
    messages: list[str] = []
    deadline = time.time() + timeout_s
    while time.time() < deadline and len(messages) < max_messages:
        remaining = max(0.1, deadline - time.time())
        text = ws_read_text(sock, remaining)
        if text is None:
            continue
        messages.append(text)
    return messages


def send_ws_message(host: str, port: int, chat_id: str, content: str, keepalive_s: float) -> None:
    del keepalive_s
    sock = open_ws(host, port, chat_id)
    try:
        ws_send_json(sock, {
            "type": "message",
            "chat_id": chat_id,
            "content": content,
        })
    finally:
        sock.close()


def collect_log_hits(log_path: Path, chat_id: str) -> list[str]:
    if not log_path.exists():
        return []
    patterns = (
        chat_id,
        "scoped delegate batch build",
        "Patched delegate_task batch to scoped user-prompt batch",
        "delegate_store plan",
        "delegate_bg worker start",
        "Queue final response to websocket",
        "Delivered pending response to",
    )
    hits: list[str] = []
    with log_path.open("r", encoding="utf-8", errors="replace") as handle:
        for line in handle:
            if chat_id not in line and not any(token in line for token in patterns[1:]):
                continue
            hits.append(line.rstrip("\n"))
    return hits


def summarize_reconnect_payloads(payloads: list[str]) -> dict:
    summary = {
        "has_response": False,
        "has_progress": False,
        "has_done": False,
        "dispatch_modes": set(),
        "response_texts": [],
    }
    for payload in payloads:
        if not payload:
            continue
        try:
            data = json.loads(payload)
        except json.JSONDecodeError:
            continue
        payload_type = str(data.get("type") or "").strip()
        if payload_type == "response":
            summary["has_response"] = True
            text = str(data.get("content") or "").strip()
            if text:
                summary["response_texts"].append(text)
        if payload_type in (
            "coordinator_status",
            "coordinator_output",
            "coordinator_done",
            "subagent_session",
            "subagent_progress",
        ):
            summary["has_progress"] = True
        if payload_type == "coordinator_done":
            summary["has_done"] = True
        coordinator = data.get("coordinator")
        if isinstance(coordinator, dict):
            dispatch_mode = str(coordinator.get("dispatch_mode") or "").strip()
            if dispatch_mode:
                summary["dispatch_modes"].add(dispatch_mode)
    return summary


def wait_for_runtime_evidence(log_path: Path, chat_id: str, timeout_s: float) -> list[str]:
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        hits = collect_log_hits(log_path, chat_id)
        has_delegate = any(f"chat={chat_id} tool=delegate_task" in line for line in hits)
        has_plan = any("delegate_store plan" in line and f"chat={chat_id}" not in line for line in hits)
        has_worker = any(f"parent_chat={chat_id}" in line and "delegate_bg worker start" in line
                         for line in hits)
        if has_delegate and has_plan and has_worker:
            return hits
        time.sleep(1)
    return collect_log_hits(log_path, chat_id)


def main() -> int:
    parser = argparse.ArgumentParser(description="Probe real multi-subagent orchestration via websocket.")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=1234)
    parser.add_argument("--chat-id", default=f"probe_multi_{int(time.time())}")
    parser.add_argument("--prompt", default=DEFAULT_PROMPT)
    parser.add_argument("--keepalive", type=float, default=20.0)
    parser.add_argument("--timeout", type=float, default=45.0)
    parser.add_argument("--reconnect-wait", type=float, default=12.0)
    parser.add_argument("--reconnect-read-timeout", type=float, default=8.0)
    parser.add_argument("--log", default=str(DEFAULT_LOG))
    args = parser.parse_args()

    log_path = Path(args.log).expanduser()
    print(f"chat_id={args.chat_id}")
    send_ws_message(args.host, args.port, args.chat_id, args.prompt, args.keepalive)
    hits = wait_for_runtime_evidence(log_path, args.chat_id, args.timeout)
    time.sleep(args.reconnect_wait)

    reconnect_payloads: list[str] = []
    reconnect_sock = open_ws(args.host, args.port, args.chat_id)
    try:
        ws_send_json(reconnect_sock, {
            "type": "session_sync",
            "chat_id": args.chat_id,
            "last_seq": 0,
        })
        reconnect_payloads = ws_collect_texts(reconnect_sock, args.reconnect_read_timeout)
    finally:
        reconnect_sock.close()

    if not hits:
        print("no log evidence found", file=sys.stderr)
        return 2

    for line in hits[-80:]:
        print(line)
    if reconnect_payloads:
        for idx, payload in enumerate(reconnect_payloads, start=1):
            print(f"reconnect_payload[{idx}]={payload}")
    else:
        print("reconnect_payload=<none>")

    reconnect_summary = summarize_reconnect_payloads(reconnect_payloads)
    dispatch_modes = ",".join(sorted(reconnect_summary["dispatch_modes"])) or "-"
    print(
        "reconnect_summary="
        f"has_response={int(reconnect_summary['has_response'])} "
        f"has_progress={int(reconnect_summary['has_progress'])} "
        f"has_done={int(reconnect_summary['has_done'])} "
        f"dispatch_modes={dispatch_modes}"
    )
    for idx, text in enumerate(reconnect_summary["response_texts"], start=1):
        print(f"reconnect_response[{idx}]={text}")

    has_delegate = any(
        (
            f"chat={args.chat_id} tool=delegate_task" in line or
            "execute patched tool tool=delegate_task" in line or
            "execute patched input tool=delegate_task" in line
        )
        for line in hits
    )
    has_worker = any(f"parent_chat={args.chat_id}" in line and "delegate_bg worker start" in line
                     for line in hits)
    has_reconnect_response = reconnect_summary["has_response"]
    has_reconnect_progress = reconnect_summary["has_progress"]
    has_staged_dispatch = "staged" in reconnect_summary["dispatch_modes"] or any(
        "\"dispatch_mode\":\"staged\"" in line or "\"dispatch_mode\": \"staged\"" in line
        for line in hits
    )
    has_oracle_worker = any("subagent=oracle" in line for line in hits)
    has_final_delivery_evidence = any(
        f"Queue final response to websocket:{args.chat_id}" in line or
        f"Delivered pending response to {args.chat_id}" in line
        for line in hits
    )
    return 0 if (
        has_delegate and
        has_worker and
        has_reconnect_response and
        has_reconnect_progress and
        has_staged_dispatch and
        has_oracle_worker and
        has_final_delivery_evidence
    ) else 1


if __name__ == "__main__":
    raise SystemExit(main())
