# Daima Agent

嵌入式 AI Agent，基于 Linux 内核风格架构。**C11 + Kbuild，单二进制**。

> 架构文档：[ARCHITECTURE.md](docs/ARCHITECTURE.md)

## 快速开始

```bash
make               # 编译
./build-kbuild/agent  # 运行
```

## 构建命令

| 命令 | 说明 |
|------|------|
| `make` | Kbuild 编译 → `build-kbuild/agent` |
| `make V=1` | 详细输出 |
| `make V=2` | 详细输出 + 重编译原因诊断 |
| `make clean` | 清理编译产物 |
| `make mrproper` | 清理 + 删除 `.config` |
| `make mips` | MIPS 交叉编译 |
| `make arm` | ARM 交叉编译 |

## 内核风格特性

| 特性 | 实现 |
|------|------|
| 构建系统 | Kbuild 递归 + `obj-y` (零 cmake) |
| 驱动模型 | `struct driver` + `probe()/remove()` (3 条总线) |
| 模块系统 | `extensions/` + `ext_init.c` 显式初始化链 |
| 初始化链 | `do_basic_setup()` 4 级手动链 |
| 平台抽象 | `arch/{host,mips,arm}/` |
| 代码风格 | `.clang-format` (主) + `checkpatch.pl` (备选) |
| 多核调度 | PLANNER + EXECUTOR + REVIEWER |
| 总线模型 | `bus_type` + `device` + `driver` + `probe()` |

## Agent 功能

- LLM 调用 (OpenAI / Anthropic 协议)
- 多 Agent 并行调度 (PLANNER + EXECUTOR + REVIEWER)
- 意图分类 + 角色路由 + 计划评审
- 飞书 / Vector / WebSocket 多通道接入
- 工具系统 (34 个: 文件 / 终端 / Web 抓取 / cron / skill)
- 会话存储 / 压缩 / 恢复
- Hashline 安全编辑
- Prometheus 访谈模式
- Todo 强制执行 + Ralph Loop
- 模型回退 + 分类路由
- `!test` 自检 (10 集成测试)
