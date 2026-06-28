#!/usr/bin/env python3
import importlib.util
import io
import json
import pathlib
import sys
import unittest
from contextlib import redirect_stdout
from unittest import mock


MODULE_PATH = pathlib.Path(__file__).with_name("probe-multi-subagent-runtime.py")


def load_probe_module():
    spec = importlib.util.spec_from_file_location("probe_multi_subagent_runtime", MODULE_PATH)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


class ProbeMultiSubagentRuntimeTests(unittest.TestCase):
    def setUp(self):
        self.module = load_probe_module()

    def test_send_ws_message_closes_socket_without_waiting_full_keepalive(self):
        sock = mock.Mock()
        with mock.patch.object(self.module, "open_ws", return_value=sock), \
             mock.patch.object(self.module, "ws_send_json") as send_json, \
             mock.patch.object(self.module, "ws_read_text", return_value=None) as read_text:
            self.module.send_ws_message("127.0.0.1", 1234, "probe_chat", "hello", 120.0)

        send_json.assert_called_once()
        read_text.assert_not_called()
        sock.close.assert_called_once()

    def test_main_prints_staged_and_final_reconnect_evidence(self):
        reconnect_payloads = [
            json.dumps({
                "type": "coordinator_status",
                "chat_id": "probe_chat",
                "coordinator": {
                    "coordinator_id": "dc_1",
                    "dispatch_mode": "staged",
                    "status": "running",
                    "agent_count": 5,
                },
            }),
            json.dumps({
                "type": "subagent_progress",
                "chat_id": "probe_chat",
                "task_id": "dt_2",
                "task": "分析 CLI 入口",
            }),
            json.dumps({
                "type": "coordinator_done",
                "chat_id": "probe_chat",
                "coordinator": {
                    "coordinator_id": "dc_1",
                    "dispatch_mode": "staged",
                    "status": "done",
                    "agent_count": 5,
                },
            }),
            json.dumps({
                "type": "response",
                "chat_id": "probe_chat",
                "content": "final synthesized answer",
            }),
        ]
        hits = [
            "22:00:00 [I] kernel: execute patched input tool=delegate_task input={\"dispatch_mode\":\"staged\"}",
            "22:00:01 [I] kernel: delegate_store plan: task_id=dt_10 slot=4 coordinator=dc_1 session_id=delegate_sync_11 subagent=oracle scope=/repo",
            "22:00:02 [I] kernel: delegate_bg worker start: task_id=dt_10 subagent=oracle parent_chat=probe_chat",
            "22:00:03 [I] kernel: Queue final response to websocket:probe_chat (4100 bytes)",
            "22:00:04 [I] kernel: Delivered pending response to probe_chat",
        ]

        argv = [
            "probe-multi-subagent-runtime.py",
            "--chat-id", "probe_chat",
            "--log", "/tmp/fake-agent.log",
        ]

        with mock.patch.object(sys, "argv", argv), \
             mock.patch.object(self.module, "send_ws_message"), \
             mock.patch.object(self.module, "wait_for_runtime_evidence", return_value=hits), \
             mock.patch.object(self.module, "open_ws", return_value=mock.Mock()), \
             mock.patch.object(self.module, "ws_send_json"), \
             mock.patch.object(self.module, "ws_collect_texts", return_value=reconnect_payloads), \
             mock.patch.object(self.module.time, "sleep"):
            output = io.StringIO()
            with redirect_stdout(output):
                rc = self.module.main()

        rendered = output.getvalue()
        self.assertEqual(rc, 0)
        self.assertIn("dispatch_mode\":\"staged", rendered)
        self.assertIn("subagent=oracle", rendered)
        self.assertIn("reconnect_payload[4]", rendered)
        self.assertIn("final synthesized answer", rendered)
        self.assertIn("Delivered pending response to probe_chat", rendered)


if __name__ == "__main__":
    unittest.main()
