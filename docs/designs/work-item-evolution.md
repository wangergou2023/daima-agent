# Work Item 收集系统设计

> 状态：设计目标与实现规格。适用读者：设计/实现 work item 收集、复盘、后续自演进流程的开发者。相关代码：`main/tools/tool_work_item.c`、`main/work_items/`、`main/agent/`

第一阶段目标不是“自动修 bug”，而是建立一个可靠的工作项收集系统：把真实问题、用户诉求、工具失败、模型协议异常、日志错误和测试失败稳定沉淀为可追踪、可去重、可复盘、可进入后续处理流程的 work item。

自动修复、跑完整回归、创建 PR 属于后续阶段，不进入第一阶段核心。

## 1. 设计目标

Work Item 收集系统要解决一个核心问题：

> 只要 Daima 遇到值得后续处理的事实，就不应该只停留在日志里，也不应该依赖模型“想起来”主动记录。

第一阶段重点做到：

- 从用户对话、工具失败、LLM/API 失败、日志异常、测试失败、heartbeat、人工 review 中收集事项。
- 把事项标准化为固定 schema。
- 为每条事项保存足够证据，包括会话、工具名、输入、输出、错误码、日志片段、相关文件或命令。
- 对重复失败做去重和限流，避免刷屏。
- 支持人工 review、更新状态、补充证据、关闭或转入实现计划。
- 保持本地 JSONL 存储简单可查，后续可迁移到 SQLite 或 GitHub issue。

第一阶段不做：

- 不自动修代码。
- 不自动提交 PR。
- 不把未确认事项直接进入修复流程。
- 不要求所有事项一次性具备完整复现步骤。
- 不把所有 WARN 都当成 defect；必须有收集规则和限流。

## 2. 可靠性的定义

“可靠”不是收集越多越好，而是满足以下条件：

| 维度 | 要求 |
| :-- | :-- |
| 不漏关键问题 | 工具协议错误、未知工具、验证失败、LLM 超时、崩溃类日志必须能进入 work item |
| 不刷屏 | 同一轮、同一错误签名只收集一次；短时间重复只增加 occurrence 或证据 |
| 可追溯 | 每条事项能追到会话、日志、工具调用或命令 |
| 可判断 | 标题、类型、状态、优先级、期望/实际或缺口要清楚 |
| 可复盘 | 支持按状态、优先级、类型、来源查看和摘要 |
| 可演进 | 后续能接入 repair flow、GitHub issue、统计和自演进经验 |

## 3. Work Item 范围

work item 不只代表 bug，也代表任何值得跟踪、确认或后续处理的事项。

| 类型 | 说明 |
| :-- | :-- |
| `defect` | 已有功能行为错误、崩溃、异常、回归 |
| `missing` | 明确缺失的功能、入口、能力或集成 |
| `improvement` | 体验、性能、稳定性、可维护性等改进诉求 |
| `tech_debt` | 架构债、重复逻辑、临时实现、难以维护的代码 |
| `docs` | 文档缺失、过期、不清晰或与实现不一致 |
| `test_gap` | 缺少测试、测试覆盖不足、复现用例未沉淀 |

来源固定为：

| 来源 | 说明 |
| :-- | :-- |
| `user` | 用户明确反馈、需求、抱怨、建议 |
| `log` | 运行日志、崩溃、后台异常、协议异常 |
| `test` | 测试命令、构建命令、验证命令失败 |
| `github_issue` | 后续接入 GitHub issue |
| `heartbeat` | 周期性健康检查发现的问题 |
| `review` | 人工或 AI 复盘记录 |

## 4. 收集入口

### 4.1 用户反馈入口

当用户表达问题、需求或体验不满时，Agent 应优先判断是否需要创建 work item。

触发语义包括：

- “这里有问题”
- “这个失败了”
- “为什么没收集”
- “想加一个功能”
- “体验不好”
- “应该支持”
- “经常崩”
- “日志里有异常”

行为要求：

- 信息足够时直接创建 work item。
- 信息不足时也可以创建，但状态必须是 `needs_info`。
- 不要因为缺少完整复现步骤而丢失用户反馈。
- 如果用户明确要求“马上修”，可以修，但仍要在明显缺陷场景记录 work item。

### 4.2 工具失败入口

工具失败是第一阶段必须自动收集的重点。不能只记录日志。

必须收集的工具失败：

| 条件 | 类型 | 来源 | 优先级 | 示例 |
| :-- | :-- | :-- | :-- | :-- |
| 未知工具 `DAIMA_ERR_NOT_FOUND` | `defect` | `log` | `P1` | `tool_name`、`$TOOL_NAME` |
| 工具调用参数为空 `{}` 且工具需要必填字段 | `defect` | `log` | `P1` | `write_file input={}` |
| 同一轮同一工具连续失败 3 次以上 | `defect` | `log` | `P1` | 连续 `write_file` 缺 `path` |
| 工具迭代预算耗尽 | `defect` | `log` | `P1` | `Tool iteration budget exhausted` |
| `terminal` 验证命令失败或超时 | `defect` | `test` | `P1/P2` | `make test` 失败 |
| `webfetch` 网络失败、超时、HTTP 错误 | `defect` | `test` | `P2` | fetch 文档失败 |
| 写文件、patch、edit 因路径策略失败 | `defect` 或 `improvement` | `log` | `P2` | 模型使用 `/path/to/file` 占位路径 |

不应默认收集的工具失败：

- 用户明确尝试一个可能不存在的文件，且 Agent 正常恢复。
- 单次低风险查询失败，后续同轮成功。
- 受安全策略阻止但结果符合预期的危险命令，例如真实危险命令被拦截。

但如果安全策略反复阻止正常工作流，例如 `node -e` 被阻止后模型无法改用脚本文件，也应收集为 `improvement` 或 `defect`。

### 4.3 LLM/API 失败入口

必须收集：

- LLM HTTP 超时。
- LLM 响应无法解析。
- LLM 返回 tool_use 但工具名为空或不存在。
- LLM 多次返回空参数工具调用。
- 单轮因 LLM 失败导致最终回复失败。

示例：

```text
HTTP request failed: Timeout was reached
agent_run: LLM call failed: DAIMA_FAIL
agent_finish: Agent turn failed: DAIMA_FAIL
```

应创建 work item：

- 类型：`defect`
- 来源：`log`
- 标题：`LLM 请求超时导致 Agent 回合失败`
- 优先级：`P1` 或 `P2`
- 证据：chat_id、provider、model、body size、错误日志片段

### 4.4 日志异常入口

日志异常不应无限扫描全量日志，第一阶段采用事件驱动和轻量 tail 扫描。

必须收集的日志签名：

- `free(): invalid pointer`
- `已中止 (核心已转储)`
- `Segmentation fault`
- `Agent turn failed`
- `Tool iteration budget exhausted`
- `Unknown tool`
- `LLM call failed`
- `HTTP request failed`
- `Session saved` 前后的崩溃或异常收尾

heartbeat 可以周期性扫描最近 N 行日志，按签名收集缺失的事项。

### 4.5 测试与构建入口

任何由 Agent 触发的验证命令，如果失败，必须收集。

包括：

- `make test`
- `cmake --build build-host`
- `npm test`
- `pytest`
- `node script.js` 生成或验证失败
- 自动构建验证失败

证据必须保存：

- 命令
- workdir
- exit_code
- timed_out
- output 摘要
- 相关文件

### 4.6 人工 review 入口

用户明确提出“这个应该记录”“这算问题”“这个以后要修”时，Agent 应调用 `work_item` 或内部收集函数记录，不要只口头同意。

## 5. 核心模块

### 5.1 `intake_collector`

负责从不同来源捕获原始事项。

输入来源：

- 用户对话
- 工具执行结果
- LLM/API 调用结果
- 日志事件
- 测试命令结果
- heartbeat 扫描
- 人工 review

输出是候选事项 `work_item_candidate`。

### 5.2 `tool_failure_observer`

负责观察每次工具调用结果。

职责：

- 记录当前回合内每个工具的失败次数。
- 计算错误签名。
- 判断是否应收集 work item。
- 对同一回合重复错误做限流。
- 在工具预算耗尽时汇总本轮失败链路。

示例签名：

```text
tool:write_file|err:DAIMA_ERR_INVALID_ARG|output:missing_path
tool:tool_name|err:DAIMA_ERR_NOT_FOUND|output:unknown_tool
tool:terminal|err:DAIMA_ERR_INVALID_STATE|output:dangerous_command_blocked
```

### 5.3 `triage_engine`

负责把候选事项归类、补齐字段并判断信息是否足够。

职责：

- 分类为固定 `type`。
- 设置 `source`。
- 生成稳定标题。
- 设置 `priority`。
- 判断 `status` 是 `new`、`triaged` 还是 `needs_info`。
- 生成 `expected` 和 `actual`。
- 提取或生成 `error_signature`。
- 与已有事项做去重。

### 5.4 `work_item_store`

负责本地持久化。

MVP 使用：

```text
<DAIMA_HOME>/spiffs_data/memory/work_items.jsonl
```

设计要求：

- 每行一条完整 JSON。
- 新建事项追加写。
- 更新事项可重写文件。
- 重复事项不新增时，应更新 `occurrences`、`last_seen_at` 和 `evidence`。
- 文件损坏时不能阻塞新事项写入；应统计 invalid lines 并生成 `defect` 或日志告警。

### 5.5 `evidence_collector`

负责把证据结构化。

证据必须尽量机器可读，不只是自然语言描述。

证据来源：

- 会话 ID
- 用户原文
- 工具名
- 工具输入
- 工具输出
- 错误码
- 命令和命令结果
- 日志片段
- 相关文件
- provider / model

### 5.6 `review_queue`

负责让人工确认、补充、关闭或转入修复。

人工 review 可以执行：

- 标记为 `accepted`
- 补充验收标准
- 补充复现步骤
- 标记为 `needs_info`
- 标记为 `rejected`
- 转入 `planned`

## 6. Work Item Schema

每条事项至少记录：

```json
{
  "id": "WI-20260608-001",
  "type": "defect",
  "source": "log",
  "title": "write_file 连续收到空参数导致回合耗尽",
  "description": "模型连续调用 write_file，但 input 为 {}，工具返回缺少 path 字段，最终触发工具迭代预算耗尽。",
  "expected": "模型调用 write_file 时必须提供 path 和 content；连续空参失败应被限流并记录。",
  "actual": "write_file 多次收到 input={}，Agent 继续循环直到预算耗尽。",
  "evidence": {
    "session_id": "web_is1z9i",
    "issue_url": "",
    "logs": [
      "12:23:23.672 [W] agent_run: Tool write_file failed: DAIMA_ERR_INVALID_ARG input={} output=错误：缺少 'path' 字段"
    ],
    "files": [
      "main/agent/agent_turn_exec_helpers.c",
      "main/tools/tool_file_write.c"
    ],
    "commands": [],
    "tool_calls": [
      {
        "tool": "write_file",
        "input": "{}",
        "error": "DAIMA_ERR_INVALID_ARG",
        "output": "错误：缺少 'path' 字段（也支持 file_path/filename）"
      }
    ]
  },
  "error_signature": "tool:write_file|err:DAIMA_ERR_INVALID_ARG|output:missing_path",
  "occurrences": 1,
  "first_seen_at": "2026-06-08T04:23:23Z",
  "last_seen_at": "2026-06-08T04:23:23Z",
  "status": "triaged",
  "priority": "P1",
  "created_at": "2026-06-08T04:23:23Z",
  "updated_at": "2026-06-08T04:23:23Z"
}
```

字段约束：

- `id` 使用日期和当天序号生成，例如 `WI-20260608-001`。
- `type` 必须来自固定枚举。
- `source` 必须来自固定枚举。
- `title` 应简洁描述事项，不包含大段日志。
- `description` 描述背景、上下文和影响。
- `expected` 描述期望结果。
- `actual` 描述当前现象；非 defect 类型可描述当前缺口。
- `evidence.logs` 保存短日志片段，不保存整份日志。
- `evidence.tool_calls` 保存工具调用证据。
- `error_signature` 用于去重和 occurrence 累计。
- `occurrences` 表示同一签名被观察到的次数。
- `first_seen_at` 和 `last_seen_at` 表示观察窗口。
- `status` 初始值通常是 `new`、`triaged` 或 `needs_info`。
- `priority` 默认为 `P2`，严重故障可升为 `P0` 或 `P1`。
- `created_at` 和 `updated_at` 使用 ISO 8601 UTC 时间。

## 7. 状态流转

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
| `triaged` | 已分类，具备基本描述和证据 |
| `needs_info` | 信息不足，不能可靠处理 |
| `accepted` | 人工确认有效 |
| `planned` | 已准备进入实现 |
| `fixing` | Phase 2 中正在处理 |
| `done` | 已完成并通过验收 |
| `rejected` | 无效、重复或不处理 |

自动收集的工具失败通常进入：

- `triaged`：有明确工具、错误码、输入、输出。
- `needs_info`：只有用户抱怨但缺少证据。
- `new`：只有初步日志签名，尚未归类。

## 8. 优先级规则

| 优先级 | 条件 |
| :-- | :-- |
| `P0` | 进程崩溃、数据损坏、核心功能完全不可用 |
| `P1` | Agent 回合失败、工具预算耗尽、未知工具、连续空参工具调用、关键验证失败 |
| `P2` | 单次工具失败但可恢复、功能缺失、体验问题、普通 webfetch 失败 |
| `P3` | 文档、低风险改进、暂不影响主流程的问题 |

## 9. 去重与限流

MVP 采用签名去重。

### 9.1 错误签名

工具失败签名：

```text
tool:<name>|err:<daima_err>|output:<normalized_output>
```

LLM 失败签名：

```text
llm:<provider>|model:<model>|err:<error_kind>
```

日志异常签名：

```text
log:<normalized_error_line>
```

测试失败签名：

```text
test:<command>|exit:<exit_code>|summary:<first_error_line>
```

### 9.2 单轮限流

同一 Agent 回合内：

- 同一 `error_signature` 只创建一次 work item。
- 后续重复只增加本轮计数。
- 回合结束时如触发预算耗尽，应把本轮失败摘要追加到同一事项证据。

### 9.3 跨轮去重

跨回合：

- 若存在相同 `error_signature` 且状态不是 `done` / `rejected`，不新增事项。
- 更新 `occurrences += 1`。
- 更新 `last_seen_at`。
- 追加最新证据，但每类证据保留上限，避免 JSONL 膨胀。

建议上限：

- `evidence.logs` 最多 10 条。
- `evidence.tool_calls` 最多 10 条。
- `evidence.commands` 最多 10 条。

无法确定重复时，允许创建新事项，并通过 `duplicate_of` 标记疑似关联。

## 10. MVP 行为示例

### 10.1 `write_file` 空参数

日志：

```text
Tool write_file failed: DAIMA_ERR_INVALID_ARG input={} output=错误：缺少 'path' 字段
```

应收集：

- `type=defect`
- `source=log`
- `priority=P1`
- `status=triaged`
- `error_signature=tool:write_file|err:DAIMA_ERR_INVALID_ARG|output:missing_path`

标题：

```text
write_file 连续收到空参数导致工具调用失败
```

### 10.2 未知工具

日志：

```text
Tool $TOOL_NAME failed: DAIMA_ERR_NOT_FOUND input={} output=错误：未知工具 '$TOOL_NAME'
```

应收集：

- `type=defect`
- `source=log`
- `priority=P1`
- `status=triaged`

标题：

```text
模型调用未知工具 $TOOL_NAME
```

### 10.3 LLM 超时

日志：

```text
HTTP request failed: Timeout was reached
LLM call failed: DAIMA_FAIL
Agent turn failed: DAIMA_FAIL
```

应收集：

- `type=defect`
- `source=log`
- `priority=P1`
- `status=triaged`

标题：

```text
LLM 请求超时导致 Agent 回合失败
```

### 10.4 用户反馈但缺少复现

用户：

```text
左边历史会话有时候显示不对。
```

应收集：

- `type=defect`
- `source=user`
- `priority=P2`
- `status=needs_info`

标题：

```text
历史会话列表偶发显示不正确
```

## 11. Review 与摘要

`work_item` 工具至少支持：

- `add`
- `update`
- `list`
- `summary`
- `review`

摘要应包含：

- 新增事项。
- 高优先级事项。
- 缺信息事项。
- 重复高频事项。
- 可进入实现的事项。

摘要示例：

```text
今日新增 5 条 work item：
- P1 defect：write_file 连续收到空参数导致工具调用失败
- P1 defect：模型调用未知工具 $TOOL_NAME
- P2 improvement：terminal 安全策略阻止 node -e 后缺少替代引导

重复高频事项：
- WI-20260608-001 出现 12 次，最近一次 12:35:05

缺信息事项：
- WI-20260608-004 缺少复现步骤
```

## 12. 实施阶段

### 12.1 Phase 1A：补齐收集规则

目标：

- 所有工具失败进入统一观察器。
- 自动收集关键工具失败。
- 单轮限流。
- 增加 `error_signature` 和 `occurrences`。
- 保留现有 JSONL 存储。

验收：

- `write_file input={}` 连续失败时，只创建一条 work item。
- 未知工具调用创建一条 work item。
- 工具预算耗尽创建或更新同一条 work item。
- `make test` 失败仍按验证失败收集。

### 12.2 Phase 1B：跨轮去重和证据追加

目标：

- 相同签名跨轮更新已有事项。
- 追加有限证据。
- 更新 `last_seen_at`。
- 不重复刷 JSONL。

验收：

- 两轮相同 `write_file input={}` 失败只保留一条活跃事项，`occurrences` 增加。
- `done` 或 `rejected` 的旧事项不阻止新事项创建。

### 12.3 Phase 1C：日志和 heartbeat 收集

目标：

- heartbeat 扫描最近日志异常。
- 崩溃、LLM 失败、Agent turn failed 进入 work item。
- 避免扫描全量日志造成性能问题。

验收：

- 日志出现 `free(): invalid pointer` 后可生成 `P0 defect`。
- LLM 超时生成 `P1 defect`。

### 12.4 Phase 1D：Review 队列体验

目标：

- `work_item summary` 能展示高优先级、重复高频、缺信息事项。
- 支持批量标记 `accepted`、`rejected`、`planned`。
- 支持按 `type/source/status/priority` 筛选。

## 13. Phase 2：处理系统

自动修复链路进入 Phase 2。

只有满足以下条件的 work item 才能进入 repair flow：

- 状态是 `accepted` 或 `planned`。
- 具备清楚的期望结果。
- 具备足够证据或复现方式。
- 能写出可验证的验收标准。

Phase 2 能力包括：

- 从 `accepted` / `planned` 事项生成修复计划。
- 修改代码。
- 跑测试。
- 记录修复证据。
- 创建 PR。

## 14. Phase 3：自演进系统

Phase 3 关注长期演进能力：

- 统计事项处理成功率。
- 识别长期技术债和高频故障区域。
- 形成 repo-specific 修复经验。
- 自动建议重构和测试补强。
- 让 heartbeat、review、测试失败和历史修复经验互相反馈。

## 15. 当前实现差距

当前已有：

- `work_item_store` 基础 JSONL 存储。
- `work_item` 工具。
- 基础枚举校验。
- 标题级重复标记。
- 验证命令失败和部分 `webfetch` 失败收集。

当前缺口：

- 普通工具失败没有统一收集。
- `write_file input={}` 不会自动生成 work item。
- 未知工具不会自动生成 work item。
- 工具预算耗尽不会自动生成 work item。
- LLM/API 超时不会自动生成 work item。
- 缺少 `error_signature`、`occurrences`、`first_seen_at`、`last_seen_at`。
- 去重只按标题和类型，无法可靠合并重复错误。
- 证据结构缺少 `tool_calls`。
- 没有单轮限流。

这些缺口应作为 Phase 1A/1B 的直接实现目标。
