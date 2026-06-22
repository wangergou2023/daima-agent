# Daima Agent

嵌入式 AI Agent，基于 Linux 内核风格架构。`C11 + Kbuild`，单二进制。

> 架构文档：[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)

## 快速开始

```bash
make
./build-kbuild/agent
```

## 构建命令

| 命令 | 说明 |
|------|------|
| `make` | Kbuild 编译，输出 `build-kbuild/agent` |
| `make V=1` | 详细输出 |
| `make V=2` | 详细输出 + 重编译原因 |
| `make clean` | 清理编译产物 |
| `make mrproper` | 清理并删除 `.config` |
| `make mips` | MIPS 交叉编译 |
| `make arm` | ARM 交叉编译 |

## 当前架构要点

| 模块 | 当前定位 |
|------|----------|
| `kernel/` | 回合主链、上下文、路由、收尾 |
| `drivers/` | tool / llm / channel / memory / skill 驱动 |
| `ipc/` | bus、消息队列、core_task |
| `extensions/` | 预留目录，默认主链不使用 |
| `spiffs_data/` | 配置、skills、运行时数据 |

## 当前能力

- OpenAI / Anthropic 协议接入
- 多 Agent 调度（Planner / Executor / Reviewer）
- 意图分类、角色选择、计划评审
- 飞书 / Vector / WebSocket 多通道接入
- 工具系统与动态 skill tools
- 会话存储、恢复、上下文压缩
- Prometheus 澄清回合
- Todo 进度约束与 Ralph Loop 警告
- 模型路由与回退
- `!test` 内建自检命令

## 当前实现上的几个约定

- 默认主链在 `kernel/loop.c` → `kernel/turn_prepare.c` → `kernel/turn_pipeline.c` → `kernel/turn_finish.c`
- `extensions_init()` 默认不装配主链行为
- `skill_summary_build_for_channel()` 只构建摘要，不注册工具
- skill 专属工具需要显式激活，turn 结束统一回收
- `subagent` 只走 `delegate_task + kernel/sched` 主路径
