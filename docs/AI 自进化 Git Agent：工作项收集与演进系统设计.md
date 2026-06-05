# AI 自进化 Git Agent：工作项收集与演进系统设计

> 第一阶段目标：先把真实问题、需求和改进机会稳定收集成结构化 work item。
> 自动修复、跑测试、创建 PR 都是后续阶段能力，不进入 MVP 核心。

## 1. 设计目标

AI 自进化 Git Agent 的第一阶段不是“自动修 bug”，而是建立一个可靠的工作项收集系统。

系统需要从用户对话、运行日志、测试失败、GitHub issue、heartbeat、人工 review 等来源收集事项，并把它们标准化为可追踪、可去重、可补充证据、可进入后续实现流程的 work item。

第一阶段重点解决：

- 收集真实问题和需求
- 标准化为 work item
- 分类、去重、补充证据
- 持久化到本地存储
- 支持人工或 AI 后续处理

暂不解决：

- 自动修改代码
- 自动创建 PR
- 未确认事项直接进入修复流程
- GitHub issue 双向同步

## 2. Work Item 范围

work item 不只代表 bug，也代表任何值得跟踪、确认或后续处理的事项。

| 类型 | 说明 |
| :-- | :-- |
| `defect` | 已有功能行为错误、崩溃、异常、回归 |
| `missing` | 明确缺失的功能、入口、能力或集成 |
| `improvement` | 体验、性能、稳定性、可维护性等改进诉求 |
| `tech_debt` | 架构债、重复逻辑、临时实现、难以维护的代码 |
| `docs` | 文档缺失、过期、不清晰或与实现不一致 |
| `test_gap` | 缺少测试、测试覆盖不足、复现用例未沉淀 |

## 3. 核心模块

### 3.1 `intake_collector`

负责从不同来源捕获原始事项。

输入来源包括：

- 用户对话：例如“这里有问题”“想加个功能”“这个体验不好”
- 日志：运行错误、异常堆栈、关键告警
- 测试失败：失败用例、命令、输出、相关文件
- GitHub issue：后续接入，第一版可保留接口边界
- heartbeat：周期性健康检查发现的问题
- review：人工复盘时记录的新事项

输出是尚未完全结构化的候选事项。

### 3.2 `triage_engine`

负责把候选事项归类、补齐字段并判断信息是否足够。

主要职责：

- 分类为 `defect` / `missing` / `improvement` / `tech_debt` / `docs` / `test_gap`
- 识别来源 `source`
- 生成简洁标题和描述
- 判断是否具备现象或诉求、期望结果、证据或复现方式
- 与已有 work item 做基础去重
- 设定初始状态和优先级

信息足够时，状态进入 `new` 或 `triaged`；信息不足时，状态进入 `needs_info`。

### 3.3 `work_item_store`

负责本地持久化结构化事项。

MVP 使用 JSONL 文件：

```text
work_items.jsonl
```

设计要求：

- 每行是一条完整 JSON 记录
- 追加写优先，便于调试和恢复
- 更新事项时可采用重写文件或追加事件日志，第一版优先选择实现简单的重写文件
- 后续可替换为 SQLite 或同步到 GitHub issue

### 3.4 `evidence_collector`

负责把 work item 与证据关联起来。

证据包括：

- 会话 ID
- GitHub issue URL
- 日志片段或日志文件路径
- 相关源码文件
- 相关命令和命令输出
- 测试失败输出

证据不要求一次性完整，但必须可追加。

### 3.5 `review_queue`

负责让人工确认、补充、关闭或转入修复。

人工 review 可以执行：

- 确认事项有效，状态改为 `accepted`
- 补充验收标准、复现步骤或证据
- 标记为 `needs_info`
- 标记为 `rejected`
- 转入 `planned`，等待 Phase 2 repair flow 处理

## 4. Work Item Schema

每条事项至少记录以下字段：

```json
{
  "id": "WI-20260605-001",
  "type": "defect | missing | improvement | tech_debt | docs | test_gap",
  "source": "user | log | test | github_issue | heartbeat | review",
  "title": "",
  "description": "",
  "expected": "",
  "actual": "",
  "evidence": {
    "session_id": "",
    "issue_url": "",
    "logs": [],
    "files": [],
    "commands": []
  },
  "status": "new | triaged | needs_info | accepted | planned | fixing | done | rejected",
  "priority": "P0 | P1 | P2 | P3",
  "created_at": "",
  "updated_at": ""
}
```

字段约束：

- `id` 使用日期和当天序号生成，例如 `WI-20260605-001`
- `type` 必须来自固定枚举
- `source` 必须来自固定枚举
- `title` 应简洁描述事项
- `description` 描述背景、上下文和影响
- `expected` 描述期望结果
- `actual` 描述当前现象；非 defect 类型可为空或描述当前缺口
- `evidence` 内数组字段默认为空数组
- `status` 初始值通常是 `new`、`triaged` 或 `needs_info`
- `priority` 默认为 `P2`，严重故障可升为 `P0` 或 `P1`
- `created_at` 和 `updated_at` 使用 ISO 8601 时间

## 5. MVP 行为

当用户说“这里有问题”“想加个功能”“这个体验不好”时，Agent 不立即进入修复。

Agent 应先尝试补齐三类信息：

- 现象或诉求：发生了什么，或希望新增什么
- 期望结果：用户认为正确状态应该是什么
- 证据或复现方式：日志、步骤、命令、截图、相关文件、测试输出

如果信息足够，Agent 写入 `work_items.jsonl`。

如果信息不足，Agent 仍可创建 work item，但状态必须标记为 `needs_info`，并在 `description` 或证据字段里说明缺口。

### 5.1 对话收集示例

用户输入：

```text
这个 Web 聊天页刷新后历史消息有时候丢了。
```

Agent 应追问或整理：

- 现象：刷新后历史消息偶发丢失
- 期望：刷新后仍能看到该会话历史
- 证据：浏览器操作步骤、会话 ID、日志或相关文件

如果用户暂时没有更多信息，创建：

```json
{
  "id": "WI-20260605-001",
  "type": "defect",
  "source": "user",
  "title": "Web 聊天页刷新后历史消息偶发丢失",
  "description": "用户反馈 Web 聊天页刷新后历史消息有时丢失，当前缺少稳定复现步骤和日志。",
  "expected": "刷新页面后仍能看到当前会话历史消息。",
  "actual": "刷新后历史消息有时不可见。",
  "evidence": {
    "session_id": "",
    "issue_url": "",
    "logs": [],
    "files": [],
    "commands": []
  },
  "status": "needs_info",
  "priority": "P2",
  "created_at": "2026-06-05T00:00:00Z",
  "updated_at": "2026-06-05T00:00:00Z"
}
```

### 5.2 摘要生成

每天或手动触发时，Agent 生成 work item 摘要。

摘要至少包含：

- 新增事项
- 高优先级事项
- 缺信息事项
- 可进入实现的事项

摘要示例：

```text
今日新增 5 条 work item：
- P1 defect：WebSocket 断线后未自动恢复
- P2 missing：缺少 work item review 队列入口
- P2 test_gap：session_store_file 缺少 JSONL 损坏用例

缺信息事项 2 条：
- WI-20260605-003 缺少复现步骤
- WI-20260605-004 缺少期望结果

可进入实现事项 1 条：
- WI-20260605-002 已 accepted，具备验收标准
```

## 6. 状态流转

```text
new
  -> triaged
  -> needs_info
  -> accepted
  -> planned
  -> fixing
  -> done

new / triaged / needs_info
  -> rejected
```

状态含义：

| 状态 | 说明 |
| :-- | :-- |
| `new` | 新收集，尚未充分整理 |
| `triaged` | 已分类，具备基本描述 |
| `needs_info` | 信息不足，不能可靠处理 |
| `accepted` | 人工确认有效 |
| `planned` | 已准备进入实现 |
| `fixing` | Phase 2 中正在处理 |
| `done` | 已完成并通过验收 |
| `rejected` | 无效、重复或不处理 |

## 7. 去重策略

MVP 采用保守去重：

- 同标题、同类型、同来源且近似描述相同，视为疑似重复
- 同一测试命令同一失败摘要，视为疑似重复
- 同一日志错误签名重复出现，合并为同一事项的证据

去重不应直接删除用户反馈。无法确定时，创建新 work item 并在描述中标记“可能与某事项相关”。

## 8. Phase 2：处理系统

自动修复链路降级到 Phase 2。

只有满足以下条件的 work item 才能进入 repair flow：

- 状态是 `accepted` 或 `planned`
- 具备清楚的期望结果
- 具备足够证据或复现方式
- 能写出可验证的验收标准

Phase 2 能力包括：

- 从 `accepted` / `planned` 的事项生成修复计划
- 修改代码
- 跑测试
- 记录修复证据
- 创建 PR

## 9. Phase 3：自进化系统

Phase 3 关注长期演进能力：

- 统计事项处理成功率
- 识别长期技术债和高频故障区域
- 形成 repo-specific 修复经验
- 自动建议重构和测试补强
- 让 heartbeat、review、测试失败和历史修复经验互相反馈

## 10. 实施假设

- 第一版不自动创建 PR
- 第一版不自动修改代码
- 收集质量优先于自动化程度
- Daima 先把 work item 做成本地持久化能力
- 后续再接 GitHub issue、SQLite 或外部项目管理系统

