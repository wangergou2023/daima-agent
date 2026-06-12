# Skills 系统

**模块**: `main/skills/` + `spiffs_data/skills/`
**职责**: Skill 加载、元数据管理、运行时注入

## STRUCTURE

```
main/skills/
├── skill_loader.c/h            # Skill 加载器（C 侧）
└── skill_meta.c/h              # Skill 元数据管理

spiffs_data/skills/             # Skill 定义（运行时）
├── pdf/SKILL.md                # PDF 处理 skill
├── xlsx/SKILL.md               # Excel 处理 skill
├── docx/SKILL.md               # Word 处理 skill
├── pptx/SKILL.md               # PPT 处理 skill
├── weather/SKILL.md            # 天气查询 skill
├── codebase-analysis/SKILL.md  # 代码分析 skill
├── code-review/SKILL.md        # 代码审查 skill
└── ...                         # 共 20 个 skill
```

## WHERE TO LOOK

| Task | Location | Notes |
|------|----------|-------|
| Skill 加载 | `main/skills/skill_loader.c` | 读取 spiffs_data/skills/ 下 SKILL.md |
| Skill 元数据 | `main/skills/skill_meta.c` | 解析 name/description |
| Skill 定义 | `spiffs_data/skills/*/SKILL.md` | YAML frontmatter + Markdown 内容 |
| Skill 调用 | `main/tools/tool_skills.c` | 通过工具接口调用 skill |

## CONVENTIONS

- Skill 文件格式：YAML frontmatter（name, description, license）+ Markdown 内容
- Skill 在 agent turn 的 context building 阶段注入 system prompt
- C 加载器扫描 `spiffs_data/skills/` 目录，动态加载所有 SKILL.md

## ANTI-PATTERNS

- 不要硬编码 skill 列表（使用扫描加载）
- 不要修改 skill 格式而不更新加载器
- Skill 内容不要包含敏感信息
