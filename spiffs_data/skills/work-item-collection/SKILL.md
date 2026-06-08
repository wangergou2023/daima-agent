---
name: Work Item 收集
description: 当用户表达 bug、功能缺失、体验改进、技术债、测试缺口或文档缺口且需要跟踪时使用。
---

# Work Item 收集

将用户反馈、工具失败、系统异常收集为结构化 work item，用于后续追踪和修复。第一阶段只收集、分类、补充证据，不直接修复。

## 何时使用

- 用户说“这里有问题”“想加个功能”“体验不好”“能不能改一下”。
- 工具执行失败，如 `webfetch` 超时、`terminal` 异常。
- 发现技术债、缺少测试、文档不清晰。
- 问题值得跟踪，但当前不应立即修复。

## 使用步骤

1. 补齐现象/诉求、期望结果、证据或复现方式。
2. 信息足够时调用 `work_item add`，状态默认 `new`。
3. 信息不足但值得跟踪时调用 `work_item add`，状态设为 `needs_info`。
4. 只有 `accepted` 或 `planned` 且验收标准清楚的事项，后续才进入实现。
5. 疑似重复时在 description 中标记重复对象。

## 工具与路径

- 常用工具：`work_item add`、`work_item list`、`work_item review`、`work_item summary`、`work_item update`。
- 类型：`defect`、`missing`、`improvement`、`tech_debt`、`docs`、`test_gap`。
- 来源：`user`、`log`、`test`、`github_issue`、`heartbeat`、`review`。

## 输出要求

- 告诉用户已记录的标题、类型、状态和缺失信息。
- 不直接承诺修复，除非用户明确进入实现并且事项已满足条件。

## 注意事项

- “收集”不是“修复”；不要在第一阶段直接改代码。
- 信息不足时用 `needs_info`，不要硬编复现步骤。
- 同标题同类型要检查疑似重复。
