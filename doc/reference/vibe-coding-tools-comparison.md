# 开源 Vibe Coding 工具全景对比

> 调研日期：2026-06-30 | 注：GitHub Stars 数据为近似值，时效性约 ±1 周

---

## 一、市场总览

Vibe Coding（氛围编程）指通过自然语言描述需求，由 AI Agent 自主完成阅读代码、编辑文件、运行命令、调试修复等全流程的开发范式。2025-2026 年是该领域的爆发期，开源社区涌现了大量优秀工具，与闭源产品（Cursor、GitHub Copilot、Claude Code）形成了有力竞争。

### 核心玩家图谱

```
                    IDE 集成型                    终端原生型
              ┌──────────────────┐       ┌──────────────────┐
              │  Cline (63K ⭐)   │       │  Codex CLI (94K⭐)│
              │  Continue (34K ⭐)│       │  Aider (46K ⭐)   │
              │  Avante.nvim(18K)│       │  OpenCode (180K⭐)│
              │  Kilo Code (20K) │       │  Qwen Code (25K⭐)│
              └──────────────────┘       │  Pi (66K ⭐)      │
                       │                 │  Plandex (15K ⭐) │
                       │                 │  OpenInterp(64K⭐)│
                       │                 └──────────────────┘
                       │                          │
        ┌──────────────┴──────────────────────────┴──────────────┐
        │                    通用平台型                            │
        │  ┌────────────┬─────────────┬──────────────────────┐   │
        │  │  Hermes    │  OpenClaw   │   OpenHands          │   │
        │  │  (206K ⭐)  │  (381K ⭐)   │   (78K ⭐)            │   │
        │  │  自主Agent │  全能助手    │   自主修Issue         │   │
        │  └────────────┴─────────────┴──────────────────────┘   │
        └─────────────────────────────────────────────────────────┘
```

### Star 热度排行

| 排名 | 工具 | Stars | 许可证 | 语言 | 定位 |
|------|------|-------|--------|------|------|
| 1 | **OpenClaw** | ~381K | MIT | TypeScript | 通用 AI 个人助手 |
| 2 | **Hermes Agent** | ~206K | MIT | Python | 自主学习 Agent 框架 |
| 3 | **OpenCode** | ~180K | MIT | TypeScript | 开源 AI 编码 Agent 平台 |
| 4 | **Codex CLI** | ~94K | Apache 2.0 | Rust | OpenAI 官方终端编码 Agent |
| 5 | **OpenHands** | ~78K | MIT | Python | 自主 Issue 修复 Agent |
| 6 | **Pi** | ~66K | MIT | TypeScript | 极简终端编码 Agent |
| 7 | **OMO** | ~64K | 自定义 | TypeScript | OpenCode 多Agent 编排插件 |
| 8 | **Open Interpreter** | ~64K | Apache 2.0 | Rust | Codex fork，专注低成本模型 |
| 9 | **Cline** | ~63K | Apache 2.0 | TypeScript | VS Code 自主编码 Agent |
| 10 | **Aider** | ~46K | Apache 2.0 | Python | 终端 AI 结对编程 |
| 11 | **Continue.dev** | ~34K | Apache 2.0 | TypeScript | 可定制编码 Agent |
| 12 | **Tabby** | ~33K | Apache 2.0 | Rust | 自托管补全服务 |
| 13 | **Qwen Code** | ~25K | Apache 2.0 | TypeScript | 全开源 Claude Code 替代 |
| 14 | **Avante.nvim** | ~18K | Apache 2.0 | Lua | Neovim Cursor 替代 |
| 15 | **Plandex** | ~15K | MIT | Go | 大型项目终端 Agent |

---

## 二、重点工具详解

### 2.1 OpenCode + OMO — 多 Agent 编排平台

| 维度 | OpenCode (基础) | OMO (编排插件) |
|------|----------------|----------------|
| **仓库** | [anomalyco/opencode](https://github.com/anomalyco/opencode) | [code-yeongyu/oh-my-openagent](https://github.com/code-yeongyu/oh-my-openagent) |
| **Stars** | ~180K | ~64K |
| **许可证** | MIT | 自定义 (NOASSERTION) |
| **语言** | TypeScript (70%) | TypeScript (87%) |
| **贡献者** | 460+ | 290+ |
| **安装** | `curl -fsSL https://opencode.ai/install \| bash` | `bunx oh-my-openagent install` |

**核心架构（四层）：**

```
客户端层 (TUI / Desktop / IDE Extension / Web)
        ↕ SDK / ACP Protocol
服务端核心 (Session管理 / Agent系统 / Tool系统)
        ↕
Provider层 (75+ LLM提供商 + LSP + MCP)
        ↕
OMO 编排层 (11 Agents / 54+ Hooks / Team Mode / Category系统)
```

**OpenCode 关键特性：**
- 75+ LLM 提供商，免费 Zen 模型
- 订阅代理：支持 GitHub Copilot、ChatGPT Plus/Pro 订阅
- 原生 LSP 集成（20+ 语言）
- 多会话并行、会话分享 (`/share`)
- 撤销/重做 (`/undo` `/redo`)

**OMO 关键特性：**
- **11 个专业 Agent**：Sisyphus（主编排）、Hephaestus（深度执行）、Oracle（架构咨询）、Librarian（文档搜索）等
- **Category 系统**：按任务类别自动路由最优模型，用户不指定模型名
- **Team Mode**：Lead + 8 Members 并行协作
- **ultrawork / Ralph Loop**：自我引用循环，完成前不停止
- **Hash-Anchored Editing**：内容哈希验证编辑准确性
- **双平台**：OpenCode (Ultimate) + Codex CLI (Light/LazyCodex)

**差异化优势：**
1. **模型自由 + 订阅代理**：唯一同时支持 Copilot/ChatGPT 订阅 + 75+ API + 免费模型的工具
2. **Category 路由**：不手动选模型，由系统按任务自动分配最优模型
3. **多 Agent 并行**：真正的团队协作，非简单子任务委派

**局限性：**
- 终端优先，无 IDE 内联补全（非 Cursor 风格）
- OMO 学习曲线陡峭（54+ hooks / 11 agents）
- Anthropic 主动封锁标准 API Key 在 OpenCode 中使用
- OMO 为非标准许可证

---

### 2.2 Hermes Agent — 自主学习 Agent 框架

| 维度 | 详情 |
|------|------|
| **仓库** | [NousResearch/hermes-agent](https://github.com/NousResearch/hermes-agent) |
| **Stars** | ~206K |
| **许可证** | MIT |
| **语言** | Python (82%) + TypeScript (14%) |
| **贡献者** | 390 |
| **安装** | `curl -fsSL https://hermes-agent.nousresearch.com/install.sh \| bash` |

**核心定位：** 不只编码工具，而是一个**自主持续学习**的 AI Agent 框架。随运行时间增长越来越聪明。

**关键特性：**
- **闭环学习系统**：从任务中自动创建技能，复用中自我改进
- **模型无关**：300+ 模型（Nous Portal / OpenRouter / OpenAI / Anthropic / Ollama 等）
- **20+ 通讯平台**：CLI、Telegram、Discord、Slack、WhatsApp、微信、钉钉、飞书、QQ 等
- **子 Agent 委派**：隔离子代理并行工作流
- **6 种终端后端**：本地、Docker、SSH、Singularity、Modal、Daytona
- **内置 Cron**：无人值守定时任务
- **60+ 内置工具**：文件、终端、网页搜索、浏览器、代码执行等
- **Blank Slate 模式**：最小工具集启动，按需开启

**差异化优势：**
1. **闭环学习** — 唯一能从使用中自动学习并改进技能的工具
2. **服务器常驻** — 可在 $5 VPS 上运行，通过 Telegram 远程交互
3. **插件生态** — `hermes-code-bridge` 可控制 Codex、Claude Code、OpenCode 等作为子代理

---

### 2.3 OpenClaw — 空间龙虾全能助手 🦞

| 维度 | 详情 |
|------|------|
| **仓库** | [openclaw/openclaw](https://github.com/openclaw/openclaw) |
| **Stars** | ~381K |
| **许可证** | MIT |
| **语言** | TypeScript (91%) |
| **贡献者** | 370+ |
| **创建者** | Peter Steinberger（2026年加入 OpenAI）|

**核心定位：** 通用个人 AI 助手，编码是其众多能力之一。通过消息应用随时随地交互。

**关键特性：**
- **23+ 通讯平台**：WhatsApp、Telegram、Slack、Discord、Signal、微信、iMessage 等
- **语音交互**：Wake Word + Talk Mode (macOS/iOS/Android)
- **Live Canvas**：Agent 驱动的可视化工作区
- **ClawHub 技能市场**：565-700+ 社区技能
- **记忆系统**：Markdown 文件（SOUL.md、MEMORY.md、AGENTS.md 等）
- **心跳/Cron**：主动式定时自动化
- **模型无关**：支持任意 LLM 提供商
- **浏览器 + 代码调试一体化**：一个循环完成所有操作

**差异化优势：**
1. **手机编码** — 通过 Telegram/WhatsApp 发消息即写代码
2. **自我改进** — 可修改自身配置、安装工具、创建技能
3. **跨工具编排** — 读取 Linear 任务 → 写代码 → 跑测试 → push 分支 → 更新 Linear

**⚠️ 安全考量：**
- 默认权限宽松（读写文件、执行命令、修改自身）
- 存在供应链安全事件（CVE-2026-25253）
- 建议：Docker 沙箱运行、使用一次性通讯账号

---

### 2.4 Pi — 极简主义终端 Agent

| 维度 | 详情 |
|------|------|
| **仓库** | [earendil-works/pi](https://github.com/earendil-works/pi) |
| **Stars** | ~66K |
| **许可证** | MIT |
| **语言** | TypeScript (93%) |
| **贡献者** | 220 |
| **创建者** | Mario Zechner (libGDX 作者) |
| **安装** | `npm install -g @earendil-works/pi-coding-agent` |

**核心定位：** "AI 编码 Agent 界的 Arch Linux" — 刻意极简，按需扩展。

**关键特性：**
- **仅 4 个内置工具**：`read` / `write` / `edit` / `bash`
- **< 300 词 System Prompt**：上下文窗口绝大部分留给实际代码
- **自我扩展**：Agent 内嵌自身源码知识，可直接编写扩展
- **Pi Packages**：扩展/技能/提示/主题通过 npm/git 包安装
- **20+ 提供商**：Anthropic、OpenAI、Gemini、Copilot、Grok、Ollama 等
- **4 种运行模式**：交互式 TUI、脚本输出、RPC JSON 协议、SDK 嵌入
- **被 OpenClaw 使用**：OpenClaw 内部使用 Pi 的 Agent 运行时

**差异化优势：**
1. **极致简洁** — 最小核心，最大灵活性
2. **可审计** — 纯本地运行，零 SaaS 后端
3. **SDK 化** — 可嵌入其他应用（OpenClaw 已采用）

---

### 2.5 OpenHands — 自主 Issue 修复引擎

| 维度 | 详情 |
|------|------|
| **仓库** | [All-Hands-AI/OpenHands](https://github.com/All-Hands-AI/OpenHands) |
| **Stars** | ~78K |
| **许可证** | MIT |
| **语言** | Python |

**核心定位：** 最强大的开源自主软件工程师。给 Issue → 读代码 → 写代码 → 调试 → 开 PR。

**关键特性：**
- **72% SWE-bench Verified**（Claude Opus 4.6）— 开源最强自主修复得分
- Docker 沙箱隔离运行
- Web UI + CLI + REST API
- Planning Mode（规划模式）
- Kubernetes 支持
- 2026 年 6 月获 $18.8M A 轮融资

**最佳场景：** CI/CD 流水线中的自主 Issue 修复和 PR 生成。

---

### 2.6 Cline — VS Code 最强自主 Agent

| 维度 | 详情 |
|------|------|
| **仓库** | [cline/cline](https://github.com/cline/cline) |
| **Stars** | ~63K |
| **许可证** | Apache 2.0 |
| **语言** | TypeScript |

**核心定位：** VS Code 中最流行的自主编码 Agent 扩展（500 万+ 安装）。

**关键特性：**
- Plan/Act 完整循环
- 终端命令执行、浏览器访问、自动错误恢复
- 30+ 提供商（含本地 Ollama）
- **SDK** 模式：可构建自定义 Agent
- **Headless CLI**：CI/CD 集成
- **多 Agent Kanban 板**：并行任务管理

**最佳场景：** VS Code 用户需要零订阅成本（BYO API Key）的完整 Agent 体验。

---

### 2.7 Aider — 终端 Git 原生结对编程

| 维度 | 详情 |
|------|------|
| **仓库** | [Aider-AI/aider](https://github.com/Aider-AI/aider) |
| **Stars** | ~46K |
| **许可证** | Apache 2.0 |
| **语言** | Python |

**核心定位：** 最老牌的开源 AI 搭档（2023年5月创建），Git 原生。

**关键特性：**
- **Diff/Patch 编辑**：给模型发最小 diff，极致 Token 效率
- **Repo Map**：大项目代码库感知地图
- 每次改动自动 Git 提交
- 语音编码支持
- Lint/Test 自动修复
- 100+ 语言

**最佳场景：** 终端优先开发者（vim/tmux），需要 Git 原生、架构感知的结对编程。

---

### 2.8 Codex CLI — OpenAI 官方终端 Agent

| 维度 | 详情 |
|------|------|
| **仓库** | [openai/codex](https://github.com/openai/codex) |
| **Stars** | ~94K |
| **许可证** | Apache 2.0 |
| **语言** | Rust (96%) |
| **贡献者** | 480（几乎全部 OpenAI 员工） |
| **安装** | `npm install -g @openai/codex` 或 `brew install --cask codex` |

**核心定位：** OpenAI 官方出品的终端原生编码 Agent。客户端全开源，模型和后端服务闭源（需 ChatGPT 订阅或 API Key）。

**关键特性：**
- **Rust 编写** — 唯一用 Rust 实现核心的顶级编码 Agent，性能极致
- **沙箱执行**：操作限定当前目录，网络默认禁用
- **MCP 协议**：完整支持，并行工具调用
- **多 Agent**：子 Agent 委派
- **Skills 系统**：可复用提示/脚本目录
- **审批工作流**：应用 diff 前预览审查
- **IDE 扩展**：VS Code / Cursor / Windsurf 均可用
- **SDK 双语言**：Python (`pip install openai-codex`) + TypeScript

**Fork 生态：**
| Fork | Stars | 特点 |
|------|-------|------|
| [Open Interpreter](https://github.com/openinterpreter/openinterpreter) | ~64K | 专注低成本/开源模型，可切换 Agent harness |
| [StellarLink Codex](https://github.com/stellarlinkco/codex) | 473 | 已归档，Web UI + Anthropic API |

**差异化优势：**
1. **OpenAI 官方** — 与 GPT-5-codex 模型深度协同优化
2. **Rust 性能** — 内存和启动速度全面领先 JS/Python 实现的 Agent
3. **双 SDK** — Python 和 TypeScript 均可嵌入

**⚠️ 注意：**
- 客户端开源（Apache 2.0），模型和后端闭源
- 默认需 ChatGPT 订阅或 API Key
- 这与 VS Code（MIT 开源）+ GitHub Copilot（闭源）模式相同

---

### 2.9 Qwen Code — 全开源 Claude Code 替代

| 维度 | 详情 |
|------|------|
| **仓库** | [QwenLM/qwen-code](https://github.com/QwenLM/qwen-code) |
| **Stars** | ~25K |
| **许可证** | Apache 2.0 |
| **语言** | TypeScript |

**核心定位：** 阿里巴巴全开源 Claude Code 替代，框架 + 模型双开源。

**关键特性：**
- Claude Code 功能完全对标
- **唯一框架+模型双开源**的工具
- 终端 TUI + VS Code + JetBrains + Zed + Desktop + 守护进程
- **IM Bot 支持**：Telegram/钉钉/微信/飞书
- Agent Arena：多模型对比擂台
- 420+ 贡献者，505 releases

**最佳场景：** 需要全开源 Claude Code 替代 + 中国生态集成的开发者。

---

## 三、核心维度横向对比

### 3.1 基本指标

| 工具 | Stars | 许可 | 语言 | 创建 | 模型支持 |
|------|-------|------|------|------|----------|
| OpenClaw | 381K | MIT | TypeScript | 2025.11 | 任意 |
| Hermes | 206K | MIT | Python + TS | 2025.07 | 300+ |
| OpenCode | 180K | MIT | TypeScript | 2025.04 | 75+ |
| Codex CLI | 94K | Apache 2.0 | Rust | 2025.04 | ChatGPT订阅/API Key |
| OpenHands | 78K | MIT | Python | 2024 | 多模型 |
| Pi | 66K | MIT | TypeScript | 2025.08 | 20+ |
| OMO | 64K | 自定义 | TypeScript | 2025.12 | 多模型路由 |
| Open Interpreter | 64K | Apache 2.0 | Rust | 2025(Fork) | 低成本/开源模型 |
| Cline | 63K | Apache 2.0 | TypeScript | 2024 | 30+ |
| Aider | 46K | Apache 2.0 | Python | 2023.05 | 多模型 |
| Continue | 34K | Apache 2.0 | TypeScript | 2023 | 多模型 |
| Tabby | 33K | Apache 2.0 | Rust | 2023 | 自托管GGUF |
| Qwen Code | 25K | Apache 2.0 | TypeScript | 2025 | 多模型 |
| Avante.nvim | 18K | Apache 2.0 | Lua | 2025 | 多模型 |
| Plandex | 15K | MIT | Go | 2024 | 多模型 |

### 3.2 能力矩阵

| 工具 | 终端 | IDE扩展 | 桌面 | 多Agent | LSP | MCP | 本地模型 | 自主修复 | 多平台聊天 |
|------|:----:|:------:|:----:|:------:|:---:|:---:|:------:|:------:|:--------:|
| **OpenCode+OMO** | ✅ | ✅ | ✅ | ✅(11) | ✅ | ✅ | ✅ | ✅ | ❌ |
| **Hermes** | ✅ | ❌ | ✅ | ✅ | ❌ | ✅ | ✅ | ✅ | ✅(20+) |
| **OpenClaw** | ✅ | ❌ | ✅ | ✅ | ❌ | ✅ | ✅ | ✅ | ✅(23+) |
| **Codex CLI** | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ❌ | ✅ | ❌ |
| **Open Interpreter** | ✅ | ❌ | ❌ | ✅ | ❌ | ✅ | ✅ | ✅ | ❌ |
| **Pi** | ✅ | ❌ | ❌ | ⚠️扩展 | ❌ | ⚠️扩展 | ✅ | ✅ | ❌ |
| **OpenHands** | ✅ | ❌ | ❌ | ❌ | ❌ | ❌ | ✅ | ✅ | ❌ |
| **Cline** | ✅ | ✅ | ❌ | ✅ | ✅ | ✅ | ✅ | ✅ | ❌ |
| **Aider** | ✅ | ❌ | ❌ | ❌ | ❌ | ❌ | ✅ | ✅ | ❌ |
| **Continue** | ⚠️ | ✅ | ❌ | ❌ | ✅ | ✅ | ✅ | ⚠️ | ❌ |
| **Tabby** | ❌ | ✅ | ❌ | ❌ | ❌ | ❌ | ✅ | ❌ | ❌ |
| **Qwen Code** | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| **Avante.nvim** | ❌ | ✅ | ❌ | ⚠️ | ✅ | ✅ | ✅ | ✅ | ❌ |
| **Plandex** | ✅ | ❌ | ❌ | ❌ | ❌ | ❌ | ✅ | ✅ | ❌ |

### 3.3 差异化定位

| 工具 | 一句话定位 | 最适合 |
|------|-----------|--------|
| **OpenCode+OMO** | 多 Agent 编排编码平台 | 复杂工程 + 预算敏感 + 模型灵活性 |
| **Hermes** | 自主学习 AI Agent 框架 | 长期使用、需要持续学习优化的开发者 |
| **OpenClaw** | 通用 AI 个人助手 | 手机编码 + 跨工具自动化 |
| **Codex CLI** | OpenAI 官方终端 Agent | 已订阅 ChatGPT + 要官方品质的终端体验 |
| **Open Interpreter** | Codex 的低成本 fork | 要 Codex 体验但用开源/便宜模型 |
| **Pi** | 极简可扩展终端 Agent | 终端控、需要完全掌控上下文的高级开发者 |
| **OpenHands** | 自主 Issue 修复引擎 | CI/CD 流水线自动修 Bug |
| **Cline** | VS Code 最强自主 Agent | VS Code 用户需要零成本完整 Agent |
| **Aider** | 终端 Git 原生结对编程 | vim/tmux 用户、Git 重度使用者 |
| **Continue** | 最可定制编码 Agent | 团队按任务分配不同模型 |
| **Tabby** | 自托管 Copilot 替代 | 企业隐私合规、内网部署 |
| **Qwen Code** | 全开源 Claude Code 替代 | 中国生态、全开源需求 |
| **Avante.nvim** | Neovim 的 Cursor | Neovim 用户不离开编辑器 |
| **Plandex** | 大项目终端 Agent | 超大代码库（2M token 上下文）|

### 3.4 SWE-bench 得分（参考）

| 工具 | SWE-bench Verified | 使用模型 |
|------|:-----------------:|----------|
| OpenHands | **72.0%** | Claude Opus 4.6 |
| Claude Code (闭源) | 88.6% | Claude Opus 4.5 |
| Codex CLI | 模型相关 | GPT-5-codex 协同优化 |
| OpenCode | 未公开标准分 | 模型相关 |
| Cline | 模型相关 | 取决于所选模型 |
| Aider | 模型相关 | 取决于所选模型 |

> 注：开源工具的 SWE-bench 分数高度依赖所用模型，并非工具自身的固定指标。OpenHands 作为自主修复引擎做了专门的 Prompt 优化。

---

## 四、选型决策树

```
你要什么体验？
│
├─ 我想在手机上发消息就能写代码
│  → OpenClaw / Hermes（多平台聊天接入）
│
├─ 我要在终端里用，配合 vim/tmux
│  ├─ 我已订阅 ChatGPT，要官方品质 → Codex CLI
│  ├─ 我要极简、自己掌控一切 → Pi
│  ├─ 我要 Git 原生、Token 高效 → Aider
│  ├─ 我要 Claude Code 的全开源替代 → Qwen Code
│  ├─ 我要 Codex 体验但用便宜模型 → Open Interpreter
│  └─ 我要超大代码库支持 → Plandex
│
├─ 我主要用 VS Code
│  ├─ 我要自主 Agent 完成多文件任务 → Cline
│  ├─ 我要按任务分配不同模型 → Continue
│  └─ 我要完整的 AI IDE 体验 → 闭源 (Cursor/Windsurf)
│
├─ 我用 Neovim，不想离开编辑器
│  → Avante.nvim
│
├─ 我需要多 Agent 并行协作处理复杂工程
│  → OpenCode + OMO（Team Mode）
│
├─ 我要 CI/CD 自动修 Bug 开 PR
│  → OpenHands
│
├─ 我是企业、代码不能离开内网
│  → Tabby（自托管补全）/ OpenCode + 本地模型
│
└─ 我要一个 AI 助手长期学习、越用越聪明
   → Hermes（闭环学习系统）
```

---

## 五、组合推荐

### 场景 1：个人全栈开发者
```
Codex CLI (主力编码，有ChatGPT订阅) 或 OpenCode + OMO (无订阅)
+ Pi (快速修补)
理由：官方品质或模型自由 + 多 Agent 编排 + 成本最低
```

### 场景 2：手机党 / 随时随地写代码
```
OpenClaw (手机端) + Aider (终端端)
理由：手机秒回 + 终端深度集成
```

### 场景 3：VS Code 党
```
Cline (自主 Agent) + Continue (日常补全)
理由：VS Code 内最佳开源组合
```

### 场景 4：企业团队
```
Tabby (自托管补全) + OpenHands (CI/CD自动修Bug)
理由：隐私合规 + 自动化效率
```

### 场景 5：长期学习型助手
```
Hermes (持续学习) + Qwen Code (编码任务)
理由：越用越聪明 + 全栈编码能力
```

---

## 六、关键趋势（2026 H1）

1. **模型无关化**：所有主流工具均已支持多种模型，模型锁定已过时
2. **本地模型成熟**：Ollama + 本地模型成为标配，隐私场景不再妥协
3. **多 Agent 协作**：从单 Agent 到 Agent 团队（OMO / Hermes / OpenClaw）
4. **跨平台聊天接入**：从终端走向多端（IM 即时通讯成为新界面）
5. **自主修复标准化**：SWE-bench 分数成为事实标准，OpenHands 引领开源
6. **开源追赶闭源**：核心功能差距已极小，差异主要在 UX 抛光
7. **Rust 入场**：Codex CLI 以 Rust 重写核心，性能远超 JS/Python 实现，可能带起终端 Agent 的 Rust 化趋势

---

## 附录：参考资源

| 工具 | 官网 | 文档 |
|------|------|------|
| OpenCode | [opencode.ai](https://opencode.ai) | [docs.opencode.ai](https://docs.opencode.ai) |
| OMO | [omo.dev](https://omo.dev) | [ohmyopenagent.com/docs](https://ohmyopenagent.com/docs) |
| Codex CLI | [github.com/openai/codex](https://github.com/openai/codex) | [platform.openai.com/docs](https://platform.openai.com/docs) |
| Open Interpreter | [github.com/openinterpreter](https://github.com/openinterpreter/openinterpreter) | - |
| Hermes | [hermes-agent.nousresearch.com](https://hermes-agent.nousresearch.com) | - |
| OpenClaw | [openclaw.ai](https://openclaw.ai) | [docs.openclaw.ai](https://docs.openclaw.ai) |
| Pi | [pi.dev](https://pi.dev) | [pi.dev/docs](https://pi.dev/docs) |
| OpenHands | [all-hands.dev](https://all-hands.dev) | [docs.all-hands.dev](https://docs.all-hands.dev) |
| Cline | [cline.bot](https://cline.bot) | [docs.cline.bot](https://docs.cline.bot) |
| Aider | [aider.chat](https://aider.chat) | [aider.chat/docs](https://aider.chat/docs) |
| Continue | [continue.dev](https://continue.dev) | [docs.continue.dev](https://docs.continue.dev) |
| Qwen Code | [github.com/QwenLM/qwen-code](https://github.com/QwenLM/qwen-code) | - |
| Plandex | [plandex.ai](https://plandex.ai) | [docs.plandex.ai](https://docs.plandex.ai) |
