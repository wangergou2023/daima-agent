# Agent 内核开发参考

> 基于 15 个开源 Vibe Coding 工具 + **Claude Code 源码泄漏（2026.3.31）**的架构调研，提炼对 Agent 内核开发的设计决策、模式参考和避坑指南。
>
> **Claude Code 泄漏补充**：2026 年 3 月 31 日，npm 包 `@anthropic-ai/claude-code` v2.1.88 因 `.npmignore` 漏了 `*.map`，导致 ~51 万行 TypeScript 源码公开。这是业界唯一一次能看清闭源顶级 Agent 全貌的机会。下文标注 🔍 的发现均来自此次泄漏。

---

## 一、核心洞察

### 行业共识已经形成

调研完 15 个工具后，最大的感受是：**核心架构已经收敛**。2025 年初各家还在探索 Agent 循环怎么写，到 2026 年中，以下模式已成为事实标准：

```
User Input → Agent Loop (Plan → Think → Act → Observe) → Output
                  │
                  ├── LLM Router (多模型分发)
                  ├── Tool Registry (工具注册/调用)
                  ├── Context Manager (上下文管理/压缩)
                  ├── Sandbox (安全隔离)
                  └── Memory (会话记忆/持久化)
```

**这意味着**：如果你 2026 年下半年开始做 Agent 内核，不需要在基本模式上重新发明轮子。竞争点在**架构质感**和**差异化能力**上。

---

## 二、语言选择：决策树

调研中 15 个工具的选型数据：

| 语言 | 工具数 | 代表 | 推荐场景 |
|------|:-----:|------|----------|
| TypeScript | 8 | Pi, OpenCode, Cline, OMO | 快速迭代、社区驱动、IDE 集成 |
| Python | 3 | Hermes, Aider, OpenHands | AI/ML 团队、学术背景、快速原型 |
| Rust | 3 | Codex CLI, Open Interpreter, Tabby | 极致性能、安全性、单二进制分发 |
| Go | 1 | Plandex | 并发处理、大上下文 |
| Lua | 1 | Avante.nvim | 编辑器插件生态绑定 |

### 决策建议

```
你是社区/个人开发者 → TypeScript
  - npm 生态、JSON 原生、VS Code 集成无摩擦
  - 参考 Pi 的极简架构起步

你是 AI/ML 团队 → Python
  - 直接复用 transformers/datasets/litellm
  - 参考 Aider 的 diff 编辑算法

你要做高性能/嵌入式场景 → Rust
  - 启动 < 5ms，内存无 GC
  - 参考 Codex CLI 的 tokio 架构

你要处理超大上下文 → Go
  - goroutine 天然适配并发 I/O
  - 参考 Plandex 的 2M token 上下文管理
```

### TypeScript 为什么占一半

不是因为 TS 最好，而是因为：

1. **JSON 原生** — Agent 完全活在 JSON 里（LLM tool schema、MCP 协议、观测序列化都是 JSON）。Python 需要 `json.loads/dumps` 来回转，TS 直接是类型。
2. **分发零摩擦** — `npx` 比 `pip install` + 虚拟环境友好一个数量级
3. **VS Code 生态外溢** — Cline/Continue 的开发者把 Agent 方法论带到了终端工具
4. **异步模型匹配** — Agent 循环是 `流式LLM + 并发工具 + WebSocket`，Node.js event loop 天然适配

---

## 三、Agent 循环设计 — 不要从头造轮子

### 标准五步循环（所有工具的共同模式）

```
1. Context Condense   ← 上下文快超 token 上限时压缩历史
2. LLM Query          ← 发当前上下文 + 工具列表给模型
3. Security Check     ← 评估模型返回动作的风险
4. Execute            ← 在沙箱中执行 → 拿到 Observation
5. Verify             ← 验证执行结果，失败回到步骤 2
```

### 各工具的变体

| 工具 | 循环特点 | 值得借鉴 |
|------|----------|----------|
| **Pi** | 最简：4 工具 + < 300 词 prompt，无多余步骤 | 极简起点 |
| **OpenHands** | 事件溯源，每步不可变日志，安全分析夹在 execute 前 | 安全审计 |
| **OMO** | 多 Agent 并行循环，Lead 编排 + Worker 执行 | 多 Agent 编排 |
| **Claude Code** 🔍 | 3 层 AsyncGenerator + 7 继续点，StreamingToolExecutor 并行工具执行 + 读写锁 | 生产级循环设计 |
| **Codex CLI** | Rust 原生 tokio async，零开销上下文切换 | 高性能实现 |
| **Cline** | Plan/Act 分离，plan 只读不执行 | 人机协作 |

### 推荐起步架构

参考 **Pi** 的最小可用设计（TypeScript）：

```typescript
// 核心类型
interface Tool {
  name: string;
  description: string;
  parameters: JSONSchema;
  execute(args: any): Promise<Observation>;
}

interface Observation {
  output: string;
  error?: string;
}

interface AgentConfig {
  model: string;
  tools: Tool[];
  systemPrompt: string;
  maxSteps: number;
}

// Agent 循环
class Agent {
  async run(task: string): Promise<Result> {
    let context = [this.systemPrompt, `Task: ${task}`];

    for (let step = 0; step < this.maxSteps; step++) {
      // 1. 上下文压缩
      context = await this.condense(context);

      // 2. LLM 调用
      const response = await this.llm.chat(context, this.tools);

      // 3. 安全检查
      await this.security.check(response.action);

      // 4. 执行
      const observation = await this.execute(response.action);

      // 5. 验证 & 循环
      context.push(response, observation);
      if (response.action.type === 'finish') break;
    }

    return this.extractResult(context);
  }
}
```

### 🔍 Claude Code 的循环设计（源码验证）

```typescript
// 不是简单的 while 循环，而是 3 层 AsyncGenerator
// 主文件 print.ts：5594 行，单个函数 3167 行，12 层嵌套深度
// 这说明生产级 Agent 循环的复杂度远超直觉

// 核心组件
class StreamingToolExecutor {
  // 并行工具执行 + 读写锁并发控制
  // 不是所有工具都要串行 — 读取类工具可以并发
}

// 3 层 AbortController 级联取消
// 父任务取消 → 子任务 → 孙任务全部取消
// 避免了"僵尸工具调用"继续消耗 API 额度
```

**关键启示**：
- **并行工具执行**：`read_file` 和 `grep` 可以同时发，不需要等一个完成再下一个
- **级联取消**：你的 Bus 模型天然支持 — 父事件取消 → 级联取消所有子事件
- **7 个继续点**：Agent 循环不是简单的 "回到步骤 2"，而是根据执行结果跳到 7 个不同的重入点

---

**为什么从 Pi 起步而不是直接用 OpenCode 的代码**：
- Pi 核心 ~2000 行，48 小时内能完全读懂
- OpenCode 是产品级代码，10 万+ 行，适合参考具体子系统（LSP、MCP）但不适合理解整体
- 先理解最小闭环，再用更复杂的设计替换部件

---

## 四、上下文管理 — 最难的问题

### Token 窗口是硬通货

一个 Agent 的质量本质上取决于**有效上下文**里塞了多少有意义的信息：

```
Token 预算分配（以 128K 窗口为例）：

System Prompt:     -2K  (Agent 角色 + 工具定义)
Task:              -1K  (用户输入)
Codebase Context:  -50K (相关文件内容，越大越好)
Conversation:      -30K (历史交互)
Safety Margin:     -45K (留给模型推理)

可用给代码的空间：~50K
```

### 各工具的做法

| 策略 | 工具 | 实现 |
|------|------|------|
| **极简 System Prompt** | Pi | < 300 词，把 Token 留给代码 |
| **Repo Map** | Aider | 用 tree-sitter 构建代码库索引，按需注入相关文件 |
| **LLM 摘要压缩** | OpenHands | 用单独的 LLM 调用把历史对话压缩成摘要 |
| **6 策略 + 断路器** 🔍 | Claude Code | 93% 压力触发，轻量→标准→激进 3 级递进 + 断路器保护 |
| **最大 2M Token** | Plandex | 不在单次调用的窗口，而是文件级分片加载 |
| **渐进式加载** | Pi Extensions | 技能/指令按需注入，不用就不占 Token |

### 设计建议

```
第 1 层：最小核心 Prompt（< 500 词）
第 2 层：tree-sitter 代码地图（按需加载相关文件）
第 3 层：LLM 摘要（历史压缩）
第 4 层：RAG 注入（外部文档/API 文档）
```

不要一开始就把 128K 塞满。**空出来的 Token 给代码 = 更高的代码理解质量**。

### 🔍 Prompt 缓存架构（Claude Code 源码验证）

Claude Code 的 System Prompt 不是一块铁板，而是**模块化的缓存感知组合**：

```
静态部分（7 段）→ 全局缓存，92% 缓存命中率
  ├── Agent 角色定义
  ├── 工具 schema
  └── 通用行为规则

动态部分（13 段）→ 每次重建，标记 DANGEROUS_uncachedSystemPromptSection
  ├── CLAUDE.md 项目规则
  ├── MCP 配置
  └── 用户偏好

边界线：SYSTEM_PROMPT_DYNAMIC_BOUNDARY
```

**关键教训**：
- 改了 CLAUDE.md 就**永远炸缓存** — 14 种缓存破坏向量带"粘性锁"，破了就不再恢复
- **会话开始前**配好所有项目规则和 MCP，中途修改 = 每个后续请求都多花缓存 Token 成本
- 缓存的不是"内容相似"，而是**字节完全相同**

**对 agent-kernel 的意义**：你的 LLM Driver 应该明确区分静态 prompt（Agent 角色 + 工具定义）和动态 prompt（项目上下文 + 用户偏好），并在 Driver 层实现缓存感知。

### 🔍 压缩策略体系（Claude Code 源码验证）

```
6 种自动压缩策略，跨 93% 上下文压力阈值触发：

策略 1: 丢弃旧工具调用结果（最轻量）
策略 2: 摘要化中间推理步骤
策略 3: 9 段标准格式总结（fork 子进程执行）
策略 4-6: 更激进的压缩（按需启用）

断路器: MAX_CONSECUTIVE_AUTOCOMPACT_FAILURES = 3
```

**为什么有断路器**：源码注释记录了一次事故 — 1279 个 session 出现 50+ 连续自动压缩失败（单个 session 最多 3272 次），每天浪费 ~25 万次 API 调用。没有断路器，一个 bug session 能烧掉几千美元。

**对 agent-kernel 的意义**：
- 上下文管理不是调一个 API，是一套**策略体系 + 断路器保护**
- 框架级必须有限流/断路器 — 不信任 Agent 的自我约束
- 你的 Context Manager Driver 应该把这些策略实现为可插拔的中间件链

---

## 五、工具系统 — 少即是多

### 行业演化路径

```
2024: 每个工具 = 独立 JSON schema（20+ 工具，维护成本高）
2025: 统一动作空间（CodeAct：所有操作用 Python 代码表达）
2026: 最小核心 + 按需扩展（Pi：4 个核心工具 + 插件系统）
```

### 核心工具集（15 个工具的交集）

不管做什么 Agent，这 4 个工具是必需的：

| 工具 | 功能 | 为什么必须 |
|------|------|-----------|
| `read_file` | 读文件 | 代码理解的基础 |
| `write_file` | 写文件 | 代码生成的输出 |
| `edit_file` | 精确编辑 | 避免全量重写，节省 Token |
| `bash` | 执行命令 | 运行代码/测试/git 操作 |

**超过这 4 个的，用插件/扩展系统按需加载**，不要写死在核心里。

### 插件/扩展系统设计（参考 Pi Packages）

```typescript
interface Extension {
  name: string;
  tools?: Tool[];           // 新增工具
  commands?: Command[];     // 新增命令
  hooks?: Hook[];           // 事件钩子
  contextProvider?: () => Promise<string>;  // 动态上下文注入
}

// 安装
agent.install('npm:@scope/my-tools');
agent.install('git:github.com/user/pi-web-search');
```

这样核心永远瘦，能力按需长。

---

## 六、模型路由 — 不是所有任务都要 Opus

### 问题

```
用户说 "写个 README" → 调 Claude Opus（浪费）
用户说 "重构整个认证模块" → 不调 Opus（不够用）
```

### 解法：Category 路由（参考 OMO）

```typescript
enum TaskCategory {
  QUICK,           // 简单任务 → GPT-4o-mini / Haiku
  DEEP,            // 复杂推理 → Claude Opus / GPT-5
  FRONTEND,        // 前端/视觉 → Gemini
  WRITING,         // 文档 → 擅长写作的模型
  EXPLORATION,     // 搜索/探索 → 快速廉价模型
}

function routeModel(category: TaskCategory): ModelConfig {
  switch (category) {
    case TaskCategory.QUICK:
      return { provider: 'openai', model: 'gpt-4o-mini', maxTokens: 2000 };
    case TaskCategory.DEEP:
      return { provider: 'anthropic', model: 'claude-opus-4-6', maxTokens: 32000 };
    // ...
  }
}
```

### 关键洞察

OpenAI 文档里提到的 Codex 相关信息，都是我上几轮通过 `librarian` agent 实时搜索回来的，不是系统内置的。

我现有的、写死的规则和行为（System Prompt 里的内容）全来自 OMO：

- **Sisyphus 身份** + 主编排角色 → OMO Agent 定义
- **Oracle / Librarian / Explore 等 Agent** → OMO 的 11 个 Agent
- **`task(category="...")` 委派系统** → OMO Category 路由
- **Phase 0-3 行为流程** → OMO Agent 编排协议
- **`.omo/` 目录结构** → OMO 插件约定
- **Claude Code 兼容层** → OMO hooks/skills/commands

Codex 相关内容都是在对话中根据你的问题实时查的，不是写死在系统里的。**我的"行为准则"不包含任何 Codex 特定的东西。**

> **不需要感知任务类别** — 直接从用户指令推断。问 "这个变量叫啥好" 就是 quick，"重构认证系统" 就是 deep。OCR 文本特征判断：短 + 简单动词 → quick，长 + 多文件 → deep。

---

## 七、多 Agent 编排 — 差异化战场

### 从单 Agent 到 Agent 团队

```
单 Agent 模式（2024-2025）
  User → Agent → 循环执行 → 输出
  问题：复杂任务单 Agent 上下文爆炸、推理深度不够

多 Agent 模式（2025-2026）
  User → Orchestrator → Agent A (探索) ─┐
                       → Agent B (编码) ──┼→ 结果聚合 → 输出
                       → Agent C (审查) ─┘
  优势：并发、专业化、上下文隔离
```

### 三种编排范式

| 范式 | 代表 | 机制 | 适用场景 |
|------|------|------|----------|
| **Lead/Worker** | OMO (Team Mode) | Lead 分配任务，Workers 并行执行，Lead 聚合 | 复杂多文件工程 |
| **Pipeline** | OpenHands (Plan→Code) | 规划 Agent 产出计划，编码 Agent 逐步执行 | 明确需求的任务 |
| **Coordinator-as-Prompt** 🔍 | Claude Code | Orchestrator 是纯 prompt 指令，子 Agent 就是带不同 prompt 的模型实例。IPC 用文件系统轮询 | 任何需要分治的任务 |
| **Peer-to-Peer** | Hermes (Sub-agents) | 主 Agent 动态 spawn 子 Agent，子 Agent 间可通信 | 探索性任务 |

### 编排协议设计

```
关键不是 Agent 数量，而是通信协议：

✓ Worker 只读当前子任务上下文（隔离，不会爆炸）
✓ Lead 只读 Worker 返回的摘要（聚合，不会爆炸）
✗ 所有 Agent 共享全量上下文（必然爆炸）
```

### 🔍 Coordinator-as-Prompt 模式（Claude Code 源码验证）

Claude Code 的多 Agent 编排**没有写一行编排代码**。Orchestrator 的整套行为逻辑是 System Prompt：

```
Coordinator Prompt 的硬性规定：
"绝不写 'based on your findings' — 
 把理解工作委派给 worker 是不可接受的。
 Coordinator 必须综合，不能传话。"
```

**架构事实**：
- 子 Agent = 带不同 prompt 的模型实例
- IPC：文件系统（`~/.claude/work/ipc/`），500ms 轮询
- fork Agent 的上下文是**字节完全相同的拷贝** — 缓存友好，spawn 5 个子 Agent 的成本 ≈ 1 个
- **已知缺陷**：文件系统 IPC 存在竞态条件，恶意任务可能劫持权限桥

**对 agent-kernel 的意义**：
- 多 Agent 不一定需要复杂协议。你的 Bus 用 Event Emitter 替代文件轮询，已经比 Claude Code 的 IPC 方案好一个档次
- Coordinator 的核心价值在**综合能力**，不在编排代码
- 关键代码路径：Coordinator prompt 的质量直接决定多 Agent 协作的质量

---

## 八、安全沙箱 — 不隔离就是裸奔

### 业界方案对比

| 方案 | 工具 | 隔离级别 | 开销 | 适用 |
|------|------|:------:|------|------|
| Docker 容器 | OpenHands | 进程级 | 中 | 服务端/CI |
| 目录限制 + 网络禁用 | Codex CLI, Pi | 文件级 | 低 | 本地开发 |
| macOS Seatbelt 沙箱 🔍 | Claude Code | OS 级 | 低 | macOS 本地 |
| Docker 内 Agent Server | OpenHands V1 | 强隔离 | 高 | 生产环境 |
| 消息平台间接访问 | OpenClaw, Hermes | 应用级 | 低 | 个人使用 |

### 🔍 Claude Code 的安全架构（五层防御，源码验证）

这是目前已知的**最完整的 Agent 安全体系**，每一层都值得深入理解：

```
Layer 1: 权限规则（项目 → 全局 → 托管 级联）
  项目 .claude/settings.json → ~/.claude/settings.json → 托管策略

Layer 2: 权限模式（6 种模式）
  default / acceptEdits / dontAsk / bypassPermissions / auto
  用户选择 Agent 的自主程度

Layer 3: YOLO 分类器（yoloClassifier.ts, 1495 行）
  ┌─ Stage 1: 64 token 快速扫描
  ├─ Stage 2: 4096 token 深度推理（temperature=0）如果 Stage 1 可疑
  └─ 判定: allow / deny / ask user
  
  🔑 关键：这是一个独立的 Sonnet 4.6 模型调用，不是嵌在主 prompt 里的安全提示
  每次工具调用 = 额外一次 API 调用，但换来了真正的安全隔离

Layer 4: Bash 安全（bashSecurity.ts, 2592 行）
  - 23 个编号安全检查（每个都对应一次真实的漏洞利用事件）
  - Zsh 专项防御（=cmd 展开、zmodload 网关、heredoc 注入、零宽空格、ztcp 泄露）
  - 手写递归下降 bash AST 解析器，标记 15 种危险节点类型
  - 44 条 gitleaks 密钥扫描规则
  
  🔑 每个编号检查背后都是一次安全事故。从攻击经验构建防御，不是从理论。

Layer 5: macOS Seatbelt 沙箱
  OS 级隔离，最后一层防线

断路器：auto 模式下 3 次连续 或 20 次累计阻止 → 强制切手动
（硬限制，不是软建议）
```

### 对 agent-kernel 的安全设计建议

```
┌─ Bus ──────────────────────────────────────────┐
│                                                  │
│  before:tool.call → Security Hook Chain          │
│     │                                            │
│     ├── Hook 1: 命令白名单检查（O(1)）           │
│     ├── Hook 2: 模式匹配（危险正则可配置）       │
│     ├── Hook 3: YOLO 分类器（独立模型调用）      │
│     └── Hook 4: 断路器计数检查                   │
│                                                  │
└──────────────────────────────────────────────────┘
```

**关键原则**：
1. **安全不是 prompt 工程** — YOLO 分类器是独立模型调用，不污染主 prompt
2. **用温度 0 做安全判断** — 安全判定需要确定性，不是创造性
3. **从攻击经验建防御** — 每一条规则背后应该有真实漏洞。没出过事的不用防
4. **断路器是最后防线** — 不信任 Agent 的自我约束，框架级硬限制

### 生产级 Bash 安全检查（可直接参考实现）

```typescript
interface BashSecurityCheck {
  id: number;            // 编号（方便追溯）
  name: string;          // 检查名
  incident: string;      // 对应的真实漏洞描述
  pattern: RegExp;       // 匹配模式
  severity: 'block' | 'warn' | 'log';
}

const bashSecurityChecks: BashSecurityCheck[] = [
  {
    id: 1,
    name: 'rm-rf-recursive-delete',
    incident: 'Agent 误删整个项目目录',
    pattern: /rm\s+-rf\s+[\/*~]/,
    severity: 'block',
  },
  {
    id: 2,
    name: 'curl-pipe-bash',
    incident: '远程代码执行攻击',
    pattern: /curl\s+\S+\s*\|\s*(ba)?sh/,
    severity: 'block',
  },
  {
    id: 3,
    name: 'sudo-escalation',
    incident: '权限提升绕过工作目录限制',
    pattern: /\bsudo\b/,
    severity: 'block',
  },
  // ... 从 Claude Code 的 23 条规则中按需移植
];
```

### 最小安全方案（本地 Agent）

```typescript
const SafetyRule = {
  // 最小权限原则
  workspaceRoot: '/home/user/project',         // 限制工作目录
  allowedCommands: ['ls', 'git', 'npm', 'node'], // 命令白名单
  forbiddenPatterns: ['rm -rf', 'sudo', 'curl | bash'], // 危险模式
  networkAccess: false,                         // 默认无网络
  fileSizeLimit: 1_000_000,                    // 写入文件大小上限
  circuitBreaker: {
    maxConsecutiveBlocks: 3,    // 连续阻止上限
    maxTotalBlocksPerSession: 20, // 会话累计阻止上限
  },
};
```

---

## 九、持久化与记忆 — 越用越聪明的基础

### 🔍 Claude Code 的三层记忆（源码验证，直接可抄）

这是目前已知的**最成熟的 Agent 记忆架构**：

```
Layer 1: Index（始终加载在上下文中）
  只有指针，每行 ~150 字符 → 极便宜，几乎不占 Token
  例："auth.ts: JWT token 生成在 validateToken() 中"
  例："数据库迁移: 使用 drizzle-kit, 配置在 drizzle.config.ts"

Layer 2: Topic 文件（按需加载）
  实际知识内容 → 中等 Token 成本
  例："认证流程: 用户通过 POST /api/auth/login 提交凭证，
        validateToken() 验证 JWT，中间件 authMiddleware 拦截未认证请求..."

Layer 3: Transcripts（永不加载到上下文）
  完整对话记录 → 零 Token 成本
  只被 grep 搜索，不会注入到 LLM 上下文
```

### 写纪律（比存储结构更重要）

```
1. 先写 Topic 文件，再更新 Index
   永远不要反过来 — index 必须先有 topic 作为来源

2. 代码库里能重新推导的，不存
   如果事实可以从代码中提取，让 grep/tree-sitter 去找，不是存到记忆里

3. Index = 指针，不是内容
   Index 条目最多 150 字符 — 只是告诉你"有什么"和"在哪"
   详细内容在 Topic 文件里，按需加载
```

### 🔍 autoDream — 夜间记忆整理（KAIROS 后台 Agent）

Claude Code 里有一个 **24/7 后台 Agent**（feature flag `KAIROS` / `PROACTIVE`），负责：

```
autoDream 流程：
  - 每夜定时运行
  - fork 子 Agent（受限工具集，防止污染主上下文）
  - 去重：合并重复的记忆条目
  - 去矛盾：检测和解决冲突
  - 重组：按主题重新组织 Topic 文件
  - 只追加日志（不能擦除自己的历史）
```

**对 agent-kernel 的意义**：
- 记忆不是一个静态存储，是一个**活的、需要维护的系统**
- 记忆整理 Agent 必须隔离运行（受限工具集 + 隔离上下文）
- 你的 Memory Driver 应该有一个 `consolidate()` 方法，由定时任务触发

### 最低实现

```typescript
interface Memory {
  // Layer 1: Index（始终在上下文中）
  getIndex(): Promise<MemoryIndex>;  // [{ key, pointer, topic }]
  addToIndex(entry: MemoryIndexEntry): Promise<void>;

  // Layer 2: Topic（按需加载）
  getTopic(topicId: string): Promise<string>;
  writeTopic(topicId: string, content: string): Promise<void>;

  // Layer 3: Transcript（只 grep，不注入上下文）
  appendTranscript(event: TranscriptEntry): Promise<void>;
  searchTranscripts(query: string): Promise<TranscriptEntry[]>;

  // 维护
  consolidate(): Promise<void>;  // 去重 + 去矛盾 + 重组
}
```

---

## 十、技术栈建议（针对你的 Bus/Driver/Device 模型）

基于你的 `agent-kernel: Bus/Driver/Device 总线模型` 定位，建议：

```
                    ┌─────────────────────┐
                    │        Bus          │  ← 消息总线 / 调度中心
                    │  (Event Emitter /   │
                    │   Message Queue)    │
                    └──────┬──────┬───────┘
                           │      │
              ┌────────────┤      ├────────────┐
              ▼            ▼      ▼            ▼
        ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐
        │ LLM      │ │ LSP      │ │ Sandbox  │ │ Memory   │
        │ Driver   │ │ Driver   │ │ Driver   │ │ Driver   │
        └──────────┘ └──────────┘ └──────────┘ └──────────┘
              │            │          │            │
              ▼            ▼          ▼            ▼
        ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐
        │ GPT/Claude│ │typescript│ │ Docker   │ │ SQLite   │
        │ Device   │ │ Device   │ │ Device   │ │ Device   │
        └──────────┘ └──────────┘ └──────────┘ └──────────┘
```

### Driver 参考实现来源

| Driver | 参考项目 | 可复用模块 |
|--------|----------|-----------|
| LLM Driver | Pi `pi-ai/` | 20+ 提供商归一化接口 |
| LSP Driver | OpenCode LSP 集成 | 语言服务器自动发现 |
| Sandbox Driver | OpenHands sandbox/ | Docker 沙箱生命周期 |
| Memory Driver | Hermes FTS5 索引 | 全文搜索 + LLM 摘要 |
| Tool Driver | Pi 扩展系统 | 工具热插拔 |

### Bus 消息协议建议

```typescript
// Driver 挂载
bus.register('llm', new OpenAIDevice(config));

// Device 通过 Bus 通信
bus.emit('llm.request', {
  id: 'req-001',
  messages: [...],
  tools: [...],
  stream: true,
});

bus.on('llm.response', (response) => {
  // 流式 token → 拼接 → 解析 tool call
});

// Hook 拦截（参考 OMO）
bus.hook('before:tool.execute', async (call) => {
  return security.check(call); // 安全审计
});
```

**关键设计原则**：
1. Driver 不知道 Device 的存在（解耦）
2. Device 不直接通信，全部经 Bus（可观测/可拦截）
3. Hook 机制在 Bus 层实现（统一安全/日志/审计）

---

## 十一、避坑清单

调研过程中发现的共同问题和教训：

### 1. 不要一开始就做大而全的 Agent

```
✗ 前 3 个月做了 11 个 Agent + 54 个 Hook + Team Mode
✓ 先用 Pi 的 4 工具起步，跑通最小闭环，然后按需加
```

OMO 的复杂度是 290+ 贡献者一年堆出来的，不是一个人能设计的。

### 2. System Prompt 不在长，在精确

```
✗ 5000 词的 System Prompt（吃掉 10%+ token 预算）
✓ < 500 词，每个词都经过验证有效
```

Pi 的 < 300 词 Prompt 和 OpenCode 的长 Prompt 在 SWE-bench 上差距不大，但 Pi 空出了更多的 token 给代码。

### 3. 安全不能事后补

```
✗ "先做功能，安全问题后面再说"
✓ 从 Day 1 就要有命令白名单 + 工作目录限制
```

OpenClaw 因为默认权限太宽松，已经出了 CVE。

### 4. 模型锁定 = 技术债

```
✗ 所有 prompt 为 Claude 优化
✓ 支持多模型，prompt 保持通用
```

Anthropic 已经主动封锁 OpenCode 使用 Claude API。单一模型依赖是定时炸弹。

### 5. 不要重新发明 MCP

```
✗ 自己定 Agent-工具通信协议
✓ 直接实现 MCP 标准
```

15 个工具里支持 MCP 的占大多数。自定义协议 = 生态孤岛。

### 6. 上下文管理是核心竞争力

```
✗ 把 128K token 窗口当成无限大来用
✓ 精打细算每一段注入的上下文
```

SWE-bench 表现差异的主要来源不是模型，是上下文管理质量。

### 7. `.npmignore` 漏 `*.map` = 源码公开 🔍

```
✗ CI 不检查 source map 泄漏
✓ CI 必须 block *.map 文件出现在发布包中
```

Claude Code 因为这个漏了 51 万行源码。一条 `!*.map` 规则就能阻止。

### 8. 没有断路器 = 一个 bug session 能烧掉几千刀 🔍

```
✗ 信任 Agent 的自我约束
✓ 框架级硬限制：连续失败 N 次 → 停止
```

Claude Code 源码记录了一次事故：单个 session 连续 3272 次压缩失败，每天 25 万次废 API 调用。之后才加的断路器。

### 9. 安全不能是 prompt 工程 🔍

```
✗ "请在安全范围内操作" 写在 system prompt 里
✓ 独立的模型调用（YOLO 分类器）+ 命令 AST 解析 + OS 级沙箱
```

prompt 里的安全提示是建议，模型不一定会听。独立的安全模块才是保障。

---

## 十二、开发路线图建议

### Phase 1：最小可用 Agent（1-2 周）

```
- [ ] 单 Agent 循环（Plan → Think → Act → Observe）
- [ ] 4 个核心工具（read, write, edit, bash）
- [ ] 单 LLM 提供商（OpenAI 或 Anthropic）
- [ ] 基础安全（工作目录限制 + 命令白名单）
- [ ] 会话持久化（SQLite）
```

### Phase 2：多模型 + 上下文（2-4 周）

```
- [ ] 多提供商 LLM Driver
- [ ] Category 路由（quick/deep/exploration）
- [ ] tree-sitter Repo Map
- [ ] LLM 历史摘要压缩
- [ ] 文件级安全审计
```

### Phase 3：扩展与编排（4-8 周）

```
- [ ] 插件/扩展系统
- [ ] MCP 客户端
- [ ] 多 Agent 编排（Lead/Worker）
- [ ] Hook 中间件系统
- [ ] Docker 沙箱
```

### Phase 4：生态与生产化（8-16 周）

```
- [ ] LSP Driver
- [ ] Web UI / TUI
- [ ] CI/CD 集成
- [ ] 技能市场 / 社区贡献机制
- [ ] 性能优化 / 基准测试
```

---

## 附录：工具速查表

| 要参考什么 | 看哪个项目 |
|-----------|-----------|
| 最小 Agent 架构 | **Pi** (`earendil-works/pi`) |
| 生产级 Agent 循环 🔍 | **Claude Code** (print.ts, StreamingToolExecutor) |
| LLM 统一接口 | Pi (`pi-ai/`) |
| Prompt 缓存架构 🔍 | **Claude Code** (stable/dynamic boundary) |
| LSP 集成 | **OpenCode** (`anomalyco/opencode`) |
| MCP 客户端 | OpenCode |
| Docker 沙箱 | **OpenHands** (`All-Hands-AI/OpenHands`) |
| 安全体系 | **Claude Code** (5层防御 + YOLO分类器) / OpenHands (SecurityAnalyzer) |
| Bash 安全检查 🔍 | **Claude Code** (bashSecurity.ts, 23 条编号规则) |
| 多 Agent 编排 | **OMO** (`code-yeongyu/oh-my-openagent`) |
| Coordinator-as-Prompt 🔍 | **Claude Code** (纯 prompt 编排 + fork Agent) |
| Agent Prompt 设计 | OMO (`agents/` 目录) / Claude Code (110 分段 prompt 组合) |
| 工具扩展系统 | Pi Packages |
| 记忆系统 🔍 | **Claude Code** (3层: index→topic→transcript + autoDream) |
| 上下文管理 🔍 | **Claude Code** (6 压缩策略 + 断路器) / **Aider** (Repo Map) |
| 终端 TUI | Pi (`pi-tui`) / OpenCode / **Claude Code** (Ink + Yoga, 7743 行) |
| Rust 高性能实现 | **Codex CLI** (`openai/codex`) |
| Web UI | OpenHands / OpenClaw |
| 企业部署 | **Tabby** (`TabbyML/tabby`) |

---

> **最后一句话**：2026 年做 Agent 开发，不需要在基础模式上创新。找一个最接近你目标的参考实现（大概率是 Pi），先跑通最小闭环，然后用 Bus/Driver/Device 模型增量替换部件。
>
> Claude Code 源码泄漏证明了：顶级 Agent 的核心竞争力不在代码量（51 万行 TS 可以被一个人用 Rust 一夜间重写并追平 9/15 测试），而在 **prompt 设计、安全策略、记忆架构、上下文管理的工程细节**。这些是 Bus/Driver/Device 模型可以系统化解决的 — 每层 Driver 专注一个维度，Bus 负责编排和拦截。
>
> **你的优势**：Claude Code 用文件轮询做 IPC，用单文件 3167 行函数做循环。你用 Bus 事件驱动做 IPC，用 Driver 抽象做分层 — 架构上你已经走在更干净的方向上。
