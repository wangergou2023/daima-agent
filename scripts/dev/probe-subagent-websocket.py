#!/usr/bin/env python3
import argparse
import base64
import json
import os
import socket
import sys
import time


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


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=1234)
    parser.add_argument("--chat-id", default=f"web_probe_{int(time.time())}")
    parser.add_argument("--content", required=True)
    parser.add_argument("--timeout", type=int, default=90)
    args = parser.parse_args()

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
            {"type": "message", "chat_id": args.chat_id, "content": args.content},
            ensure_ascii=False,
        ),
    )

    frames = []
    saw_start_response = False
    saw_completion = False
    dsml_leak = False
    transcript_leak = False
    deadline = time.time() + args.timeout

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
            saw_start_response = True
        if msg_type == "coordinator_done":
            saw_completion = True
            content = json.dumps(data, ensure_ascii=False)
            if "<｜｜DSML｜｜" in content:
                dsml_leak = True
            if any(token in content for token in ("FILE:", "SEARCH:", "<bash>", "<fileio>", "<tool>", "<read-file", "```bash", "```json", "```shell")):
                transcript_leak = True
            break

    send_ws_close(sock)
    sock.close()

    for frame in frames:
        print(frame)

    if not saw_start_response:
        print("probe failed: no initial response observed", file=sys.stderr)
        return 3
    if not saw_completion:
        print("probe failed: no coordinator completion observed", file=sys.stderr)
        return 4
    if dsml_leak:
        print("probe failed: websocket output still contains DSML", file=sys.stderr)
        return 5
    if transcript_leak:
        print("probe failed: websocket output still contains raw transcript markers", file=sys.stderr)
        return 6
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
