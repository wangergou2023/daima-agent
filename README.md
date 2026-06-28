# Daima Agent

嵌入式 AI Agent，基于 Linux 内核风格架构。`C11 + Kbuild`，单二进制。

> 架构文档：[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)
>
> 发布状态：`0.1.0-alpha`
>
> Changelog：[CHANGELOG.md](CHANGELOG.md)

当前 alpha 发布范围：

- 真实多 subagent 并发编排
- Web 端多 subagent 基础展示与详情恢复
- `session_events` 双游标回放：
  - `after_seq`
  - `after_visible_revision`
- 基于真实 `opencode` 仓库的自检准备与日志探测

当前未承诺：

- 还不是 `opencode sessions.events(after)` 那种单一 durable event stream
- 还不是最终完成版的多 subagent 架构
- `parent wake` 语义仍在继续向 `oh-my-openagent` 收敛

常用入口：

- `./run.sh`：本地开发实例编译并启动
- `./run.sh --background`：后台启动并输出 PID/日志路径
- `./run.sh --no-build`：跳过编译直接重启
- `./install.sh`：安装到 `~/.agent-data`，并重启为安装版运行时
- `scripts/dev/probe-subagent-websocket.py`：websocket subagent/protocol 调试探针

运行边界：

- `./run.sh` 面向仓库内开发调试，启动的是 `build-kbuild/agent`
- `./install.sh` 面向安装态运行，启动的是 `~/.agent-data/bin/agent`
- 两者默认共用 `web_port`，不要混跑；安装后如果要验证安装版，请直接访问安装版端口，不要保留旧的开发实例

macOS 兼容说明：

- `run.sh` / `install.sh` 已兼容无 `setsid` 的环境
- 安装脚本不再依赖 GNU `install -D`
- host 运行时不再硬编码依赖 Linux 的 `/proc/self/exe` 与 `/proc/meminfo`
- 当前仓库内验证已在 Linux 上完成；如在 macOS 首次运行，优先使用 `./run.sh --background` 与 `./install.sh` 验证启动链路

alpha 发布前最小验收命令：

- `make -j4`
- `node scripts/dev/check-subagent-web-ui.js`
- `./build-kbuild/agent --self-test`
