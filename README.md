# Daima Agent

嵌入式 AI Agent，基于 Linux 内核风格架构。`C11 + Kbuild`，单二进制。

> 架构文档：[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)

常用入口：

- `./run.sh`：本地单实例编译并启动
- `./run.sh --background`：后台启动并输出 PID/日志路径
- `./run.sh --no-build`：跳过编译直接重启
- `./install.sh`：安装到 `~/.agent-data`
- `scripts/dev/probe-subagent-websocket.py`：websocket subagent/protocol 调试探针
