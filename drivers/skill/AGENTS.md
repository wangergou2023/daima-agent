# SKILL CONTAINER MODEL

## OVERVIEW

技能容器模型，三层架构（容器→设备→驱动），区别于扁平 `struct tool_driver` 模型。`skill_loader_init()` 在 `subsys_initcall(4)` 被调用，加载 `spiffs_data/skills/` 下 19 个技能目录并构建系统提示摘要。

## STRUCTURE

```
drivers/skill/
├── skill_loader.{h,c}   # 入口：init + 构建系统提示摘要 + 通道过滤
├── skill_meta.{h,c}     # 元数据：名称校验/路径解析/YAML front matter 提取
├── skill_module.{h,c}   # 容器层：probe/load/unload 生命周期，依赖检查
├── skill_tools.{h,c}    # 设备层：每技能最多 8 个工具注册到 tool_bus
└── Makefile             # obj-y := skill_loader.o skill_meta.o skill_tools.o skill_module.o
```

## WHERE TO LOOK

| Task | Location | Notes |
|------|----------|-------|
| 技能加载入口 | `skill_loader.c:257` | `skill_loader_init()` → 安装 4 内置 + 遍历目录构建摘要 |
| 摘要构建（通道感知） | `skill_loader.c:306-353` | `append_skill_summary_for_entry()` — 子目录优先（SKILL.md） |
| 元数据解析 | `skill_meta.c:52` | `parse_yaml_value()` 提取 `name`/`description` |
| 容器生命周期 | `skill_module.c:7` | `skill_module_load()` → `probe → load → loaded=1` |
| 工具注册 | `skill_tools.c:91` | `activate_loaded_bundle()` → `tool_registry_register_dynamic()` |
| 技能创建约束 | `skill_loader.c:100-150` | BUILTIN_SKILL_CREATOR：create/eval/improve/benchmark 流程 |

## CONVENTIONS

- **三层架构**：`struct skill_module`（容器）→ `struct tool_device`（设备）→ `struct tool_driver`（驱动），不同于 `tool_driver` 的单层模型。
- **存储位置**：运行时技能在 `spiffs_data/skills/<name>/SKILL.md`；通道专属在 `skills/channels/<channel>/<name>/SKILL.md`。
- **文件结构**：每个技能目录含 `SKILL.md`（YAML front matter + markdown 正文）和可选的 `scripts/` 目录。
- **元数据**：YAML front matter 必须有 `name` 和 `description`；标题取 YAML name 或首个 `#` 标题。
- **依赖检查**：`skill_module_check_tool_exists()` 查询 `tool_bus` 确认所需工具存在。
- **内置技能**：4 个（weather/daily-briefing/skill-creator/menu-xianren），首次运行自动写入 `spiffs_data/skills/`。
- **名称规范**：小写英文，可用连字符，例如 `code-review`；路径必须通过 `skill_meta_validate_name()` 安全检查。

## ANTI-PATTERNS

- **Skill ≠ Driver**：技能是容器（module），不是驱动。`struct skill_module` 管理一组设备的生命周期，不要将技能直接当作 `struct tool_driver` 使用。
- **不可递归委托**：skill 内创建的 sub-agent 不可再调用 `delegate_task`（与 tool_delegate 约束一致）。
- **工具名必须真实存在**：技能步骤引用的工具名必须在 `tool_bus` 上可查询，不能用占位名或 Claude Code 专属命令。
- **不写泛化能力宣传**：技能正文写操作规则，不写 "robust solution"、"seamless integration" 等空话。
