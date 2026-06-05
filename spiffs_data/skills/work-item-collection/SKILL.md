---
name: Work Item 收集
description: 当用户表达 bug、功能缺失、体验改进、技术债、测试缺口或文档缺口时，收集为结构化 work item 而非直接修复。
---

# Work Item 收集

将用户反馈、工具失败、系统异常收集为结构化 work item，用于后续追踪和修复。**第一阶段不直接修复**，只收集、分类、补充证据。

## 何时使用

- 用户说"这里有问题""想加个功能""体验不好""能不能改一下"
- 工具执行失败（webfetch 超时、terminal 异常等）
- 发现技术债、缺少测试、文档不清晰
- 任何值得跟踪但不应立即修复的问题

## 分类 (type)

| 类型 | 说明 |
|------|------|
| `defect` | 已有功能行为错误、崩溃、异常 |
| `missing` | 明确缺失的功能或能力 |
| `improvement` | 体验、性能、稳定性改进 |
| `tech_debt` | 架构债、重复逻辑、临时实现 |
| `docs` | 文档缺失、过期、不清晰 |
| `test_gap` | 缺少测试、覆盖不足 |

## 来源 (source)

`user` / `log` / `test` / `github_issue` / `heartbeat` / `review`

## 操作步骤

1. **补齐信息**：现象/诉求、期望结果、证据/复现方式
2. **信息足够** → `work_item add`，状态默认 `new`
3. **信息不足但值得跟踪** → `work_item add`，状态设为 `needs_info`，在 description 中说明缺口
4. **不直接修复**：只有 `accepted` 或 `planned` 且验收标准清楚的事项，后续才进入实现

## 常用命令

- 收集：`work_item add` — 创建 work item
- 查看：`work_item list` — 列出事项，支持按 status/type/priority 过滤
- 审核：`work_item review` — 列出待审项（new/triaged/needs_info），支持批量 accept/reject
- 摘要：`work_item summary` — 生成今日摘要（新增/高优先级/缺信息/可进入实现）
- 更新：`work_item update` — 修改单个 work item 字段

## 审核状态流转

```
new → triaged → needs_info → accepted → planned → fixing → done
任意状态可 → rejected
```

审核时：
- `accepted`：确认有效，可进入后续实现
- `rejected`：无效、重复或不处理
- `needs_info`：信息不足，需要补充
- `planned`：已准备进入实现

## 去重

同标题 + 同类型自动检测疑似重复，description 会标记 `⚠疑似与 WI-xxx 重复`。

## 示例

用户说"Web 聊天页刷新后历史消息有时候丢了"：

```
work_item add
  type=defect
  source=user
  title="Web 聊天页刷新后历史消息偶发丢失"
  description="用户反馈刷新后消息偶发丢失，缺少稳定复现步骤"
  expected="刷新后仍能看到历史消息"
  actual="刷新后历史消息有时不可见"
  status=needs_info
```
