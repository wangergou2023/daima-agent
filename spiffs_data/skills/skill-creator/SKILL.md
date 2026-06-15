---
name: 技能创建器
description: 当用户要求创建、改写、评估、优化或测试 Agent 技能，或提到 Claude Skill Creator 时使用。
---

# 技能创建器

这是 Claude Skill Creator 插件工作流在 Agent 中的本地适配版。Agent 不能直接执行 Claude Code 的 `/skill-creator` 插件命令；遇到创建或维护技能的请求时，按 Create、Eval、Improve、Benchmark 四种模式工作。

## 何时使用

- 用户要求创建新技能、改写现有技能、评估技能质量或扩展能力。
- 用户要求沉淀任务套路为 skill。
- 用户提到 Claude Skill Creator、`/skill-creator`、Create、Eval、Improve、Benchmark。

## 使用步骤

1. 选择模式：默认 Create；检查/评估用 Eval；优化/修复用 Improve；测试/验证用 Benchmark。
2. Create：明确目标、触发条件、输入输出、可用工具和保存位置。
3. Eval：用 `skills action=view` 读取目标技能，检查触发条件、front matter、真实工具和路径。
4. Improve：先评估问题，再只改导致失败的触发条件、步骤、工具名、路径或示例。
5. Benchmark：生成 3-5 条测试提示，覆盖正常触发、边界表达和不应触发场景。
6. 用 `apply_patch` 保存技能到 `/spiffs/skills/<name>/SKILL.md`。

## 工具与路径

- 常用工具：`skills action=list`、`skills action=view`、`files action=read`、`apply_patch`、`terminal`。
- 技能路径：`/spiffs/skills/<name>/SKILL.md`。
- `SKILL.md` 必须包含 front matter：`name` 和 `description`。

## 输出要求

- 技能正文优先包含：`# 标题`、`## 何时使用`、`## 使用步骤`、`## 工具与路径`、`## 注意事项`。
- 创建或修改后给出改动摘要和建议的 Benchmark 提示。

## 注意事项

- `description` 只写触发条件，不总结完整流程。
- 工具名和路径必须真实可用。
- 不要把 Claude Code 专属命令写成 Agent 可执行命令；`/skill-creator` 只作为参考工作流名称。
- 技能要短而具体，避免把完整项目计划塞进单个技能。
