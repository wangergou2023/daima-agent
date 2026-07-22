# daima-agent

用 C11 构建的嵌入式 AI Agent，采用 Linux 内核风格的 **Bus / Driver / Device** 架构和 Kbuild 构建系统。

## 核心机制

```
用户消息 → Bus → Boss 分析意图
  ├─ Registry 匹配到 Specialist → 注入其 system_prompt，限制工具集 → 执行
  └─ 无匹配 → Boss 亲自执行 → 写入 Transcript
                                      ↓
                                 HR Agent（手动/自动触发）
                                      ↓
                          scan → cluster → distill → register
                                      ↓
                       下次类似任务 → 自动路由到新 Specialist
```

## 快速开始

```bash
./install.sh          # 编译 + 安装 + 前台启动
./install.sh --background  # 后台启动
```

打开 `http://127.0.0.1:1234`

## 目录

```
kernel/         核心引擎（loop, router, registry, turn管道, HR蒸馏）
drivers/        驱动层（llm, tool, channel/feishu, channel/gateway, memory, skill）
ipc/            消息总线（Bus）— 入站/出站队列
arch/           平台适配（host/mips/arm）
init/           启动引导
spiffs_data/    运行时数据
  config/       配置（config.json, IDENTITY.md, SOUL.md）
  agents/       预定义 Agent 角色（hr/ 等）
  skills/       Skill 模块
  web/          Web UI
```

## 文档

- [ARCHITECTURE.md](ARCHITECTURE.md) — 系统架构
- [doc/boss-hr-dynamic-agent-framework-prd.md](doc/boss-hr-dynamic-agent-framework-prd.md) — Boss-HR 框架 PRD
- [doc/adr/](doc/adr/) — 架构决策记录
- [doc/reference/](doc/reference/) — 外部参考
