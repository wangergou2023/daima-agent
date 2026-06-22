/* 内置技能定义与安装。 */

#include "drivers/skill/skill_builtin.h"
#include "fs.h"
#include "paths.h"

#include <stdio.h>
#include "linux/printk.h"

#define BUILTIN_WEATHER \
    "# 天气\n" \
    "\n" \
    "通过 weather 工具获取当前天气与预报（基于 wttr.in，无需 API Key）。\n" \
    "\n" \
    "## 何时使用\n" \
    "当用户询问天气、温度、降雨、预报等。\n" \
    "\n" \
    "## 使用步骤\n" \
    "1. 使用 get_current_time 获取当前日期\n" \
    "2. 调用 weather 工具：{\"location\":\"城市\",\"type\":\"current\"}\n" \
    "3. 若用户要预报，可用 {\"location\":\"城市\",\"type\":\"forecast\",\"days\":3}\n" \
    "4. 用简洁友好的格式回答\n" \
    "\n" \
    "## 示例\n" \
    "用户：\"东京今天天气怎么样？\"\n" \
    "→ get_current_time\n" \
    "→ weather {\"location\":\"东京\",\"type\":\"current\"}\n" \
    "→ \"东京：8°C，局部多云。体感 6°C，湿度 40%，风速 10 km/h。\"\n"

#define BUILTIN_DAILY_BRIEFING \
    "# 每日简报\n" \
    "\n" \
    "为用户生成个性化的每日简报。\n" \
    "\n" \
    "## 何时使用\n" \
    "当用户要求每日简报、早报或“今天有什么新消息”。\n" \
    "也适合作为 heartbeat/cron 任务。\n" \
    "\n" \
    "## 使用步骤\n" \
    "1. 用 get_current_time 获取今天日期\n" \
    "2. 读取数据目录中的 MEMORY.md 获取用户偏好与上下文\n" \
    "3. 读取今日笔记（若存在）\n" \
    "4. 若 USER.md 中有地点，用 weather 获取天气\n" \
    "5. 汇总记忆中的待办事项与今日计划\n" \
    "6. 输出简洁简报，包含：\n" \
    "   - 日期与时间\n" \
    "   - 天气（若 USER.md 有地点）\n" \
    "   - 记忆中的待办事项\n" \
    "   - 已安排的 cron 任务\n" \
    "\n" \
    "## 格式\n" \
    "保持简短——最多 5-10 条要点，使用用户偏好语言。\n"

#define BUILTIN_SKILL_CREATOR \
    "# 技能创建器\n" \
    "\n" \
    "这是 Claude Skill Creator 插件工作流在 Agent 中的本地适配版。Agent 不能直接执行 Claude Code 的 `/skill-creator` 插件命令；遇到创建或维护技能的请求时，按下面四种模式工作，并输出适合 Agent 的技能文件。\n" \
    "\n" \
    "## 何时使用\n" \
    "\n" \
    "当用户要求创建新技能、改写现有技能、评估技能质量、扩展能力、沉淀任务套路，或提到 Claude Skill Creator、`/skill-creator`、Create、Eval、Improve、Benchmark 时使用。\n" \
    "\n" \
    "## 模式选择\n" \
    "\n" \
    "- Create：从用户目标设计并写入新技能。\n" \
    "- Eval：评估现有技能是否会在正确场景触发、是否引用真实工具、是否足够具体。\n" \
    "- Improve：根据失败案例、反馈或新约束改进现有技能。\n" \
    "- Benchmark：生成可重复的测试提示和预期行为，用来验证技能是否生效。\n" \
    "\n" \
    "如果用户没有指定模式，默认使用 Create。若用户说“检查”“评估”“好不好”，使用 Eval；说“优化”“改进”“修复”，使用 Improve；说“测试”“验证”“benchmark”，使用 Benchmark。\n" \
    "\n" \
    "## Create\n" \
    "\n" \
    "1. 明确技能目标、触发条件、输入输出、可用工具和保存位置。\n" \
    "2. 选择简短、清晰的目录名：小写英文，可用连字符，例如 `code-review`。\n" \
    "3. 写出 Agent 技能文件，路径固定为 `spiffs_data/skills/<name>/SKILL.md`；运行时绝对路径可用当前 Agent skills 目录。\n" \
    "4. `SKILL.md` 必须包含 YAML front matter：\n" \
    "   - `---`\n" \
    "   - `name: <技能名>`\n" \
    "   - `description: <一句话描述>`\n" \
    "   - `---`\n" \
    "5. 正文保持简洁，优先包含：\n" \
    "   - `# 标题` —— 清晰的名称\n" \
    "   - `## 何时使用`\n" \
    "   - `## 使用步骤`\n" \
    "   - `## 工具与路径`\n" \
    "   - `## 示例`（可选）\n" \
    "6. 使用 `apply_patch` 保存技能，例如 `*** Add File: spiffs_data/skills/<name>/SKILL.md`；不要调用不可用工具或使用占位路径。\n" \
    "7. 告诉用户下一次对话开始后技能会自动生效；如需立即检查，可用 `skills action=list` 和 `skills action=view` 查看。\n" \
    "\n" \
    "## Eval\n" \
    "\n" \
    "1. 使用 `skills action=view` 读取目标技能；必要时用 `files action=read` 查看关联文件。\n" \
    "2. 检查 front matter 是否包含可解析的 `name` 和 `description`。\n" \
    "3. 检查触发条件是否具体：既不能宽到抢占无关任务，也不能窄到常见表达无法触发。\n" \
    "4. 检查步骤是否引用 Agent 中真实可用的工具，例如 `files`、`apply_patch`、`terminal`、`skills`。\n" \
    "5. 检查路径是否符合当前环境，技能应保存到 `spiffs_data/skills/<name>/SKILL.md` 或运行时 skills 目录。\n" \
    "6. 输出结论：通过、需要改进，或不建议使用；列出具体问题和修改建议。\n" \
    "\n" \
    "## Improve\n" \
    "\n" \
    "1. 先用 Eval 找出问题，不要盲目重写。\n" \
    "2. 保留技能原本目标，只改触发条件、步骤、工具名、路径或示例中会导致失败的部分。\n" \
    "3. 删除过度泛化、重复、和 Agent 环境不匹配的内容。\n" \
    "4. 用 `apply_patch` 写回原路径；或在用户要求时另存为新技能。\n" \
    "5. 给出改动摘要和建议的 Benchmark 提示。\n" \
    "\n" \
    "## Benchmark\n" \
    "\n" \
    "1. 为技能设计 3-5 条测试提示，覆盖正常触发、边界表达和不应触发的场景。\n" \
    "2. 每条测试写清楚预期行为：是否应使用该技能、应调用哪些工具、应生成或修改哪些文件。\n" \
    "3. 对创建类技能，至少包含一条检查 `spiffs_data/skills/<name>/SKILL.md` 是否存在且 front matter 可解析的测试。\n" \
    "4. 如果测试失败，切换到 Improve 模式修正。\n" \
    "\n" \
    "## 质量标准\n" \
    "\n" \
    "- 技能描述必须能帮助系统判断“何时使用”。\n" \
    "- 技能正文写操作规则，不写泛泛的能力宣传。\n" \
    "- 工具名和路径必须真实可用；优先使用 `skills`、`files`、`apply_patch`、`terminal`。\n" \
    "- 需要临时执行代码时，先用 `apply_patch` 新建脚本文件，再用 `terminal` 执行脚本；默认工作目录是 Agent workspace。只有明确操作某个项目时才传项目 `workdir`。不要使用 `node -e`、`python -c` 或 `cd ... && ...`。\n" \
    "- 不要把 Claude Code 专属命令写成 Agent 可执行命令；`/skill-creator` 只作为参考工作流名称。\n" \
    "- 技能要短而具体，避免把完整项目计划塞进单个技能。\n"

#define BUILTIN_MENU_XIANREN \
    "# 菜单仙人\n" \
    "\n" \
    "你是“菜单仙人”：懂食堂、会搭配、说话有一点俏皮，但必须实用。\n" \
    "\n" \
    "## 何时使用\n" \
    "\n" \
    "当用户发送公司食堂周菜单、菜单图片，或询问“吃啥”“一会儿吃啥”“中午吃什么”“晚上吃什么”“帮我搭配一下”时使用。\n" \
    "\n" \
    "## 菜单保存\n" \
    "\n" \
    "- 最新菜单：`/spiffs/memory/weekly-menu/current-menu.md`\n" \
    "- 饮食偏好：`/spiffs/memory/weekly-menu/preferences.md`\n" \
    "- 菜单是图片时，先转写成 Markdown 表格再保存；看不清写 `待确认`，不要编菜名。\n" \
    "- 保存时写元信息：保存日期、来源、覆盖周次（能判断再写）。\n" \
    "- 直接写固定文件，不要先列目录，不要去 `workspace` 找菜单。\n" \
    "- 优先用 `apply_patch` 更新文件；不要为了确认存在性先做 `files action=list`。\n" \
    "\n" \
    "## 推荐流程\n" \
    "\n" \
    "1. 直接读取 `/spiffs/memory/weekly-menu/current-menu.md`；不要列目录，不要搜索 `workspace`，不要猜别的路径。\n" \
    "2. 如果读取失败，直接请用户先发本周菜单，不要自己创建目录，不要改用别的路径找文件。\n" \
    "3. 用 `get_current_time` 获取当前日期时间。\n" \
    "3. 推断餐段：05:00-10:30 早餐，10:30-14:00 午餐，16:30-20:30 晚餐；不确定就问一句。\n" \
    "4. 按当天和餐段查菜单。\n" \
    "5. 再直接读取 `/spiffs/memory/weekly-menu/preferences.md`；这个文件不存在也没关系，按“暂无长期偏好”处理。\n" \
    "6. 输出 1 个首选组合和 1 个备选组合。\n" \
    "\n" \
    "## 工具约束\n" \
    "\n" \
    "- 查看菜单或偏好时，只用 `files action=read` 读取固定文件。\n" \
    "- 不要对 `/spiffs/memory/weekly-menu` 先做 `files action=list` 来试探存在性。\n" \
    "- 不要去 `/spiffs/workspace`、`/workspace` 或其他项目目录找菜单。\n" \
    "- 不要为了补目录而先用 `terminal mkdir -p`；只有确实需要写文件且 `apply_patch` 无法完成时才考虑。\n" \
    "- 用户只是问“吃啥”时，不要改文件，只做读取和推荐。\n" \
    "\n" \
    "## 搭配原则\n" \
    "\n" \
    "- 优先凑齐：蛋白质 + 蔬菜 + 主食；有汤/水果/酸奶可顺手加。\n" \
    "- 减脂：少油少炸，主食半份，蛋白质和蔬菜优先。\n" \
    "- 避辣或忌口：避开明显冲突项，给非辣/替代备选。\n" \
    "\n" \
    "## 输出格式\n" \
    "\n" \
    "菜单仙人掐指一算：今天<餐段>吃这套。\n" \
    "首选：<主菜> + <蔬菜> + <主食>（一句理由）\n" \
    "备选：<组合>（卖完/不想吃时用）\n" \
    "小提醒：<少油/加汤/少主食/避辣等一条>\n"

typedef struct {
    const char *filename;
    const char *content;
} builtin_skill_t;

static const builtin_skill_t s_builtins[] = {
    { "weather",        BUILTIN_WEATHER        },
    { "daily-briefing", BUILTIN_DAILY_BRIEFING },
    { "skill-creator",  BUILTIN_SKILL_CREATOR  },
    { "menu-xianren",   BUILTIN_MENU_XIANREN   },
};

static void install_builtin(const builtin_skill_t *skill)
{
    char dir_path[128];
    char file_path[160];
    char legacy_path[160];

    snprintf(dir_path, sizeof(dir_path), "%s/%s", path_skills_dir(), skill->filename);
    snprintf(file_path, sizeof(file_path), "%s/SKILL.md", dir_path);
    snprintf(legacy_path, sizeof(legacy_path), "%s/%s.md", path_skills_dir(), skill->filename);

    FILE *f = fopen(file_path, "r");
    if (f) {
        fclose(f);
        pr_debug("Skill exists: %s", file_path);
        return;
    }

    f = fopen(legacy_path, "r");
    if (f) {
        fclose(f);
        pr_debug("Legacy skill exists: %s", legacy_path);
        return;
    }

    fs_ensure_dir(path_skills_dir());
    fs_ensure_dir(dir_path);

    f = fopen(file_path, "w");
    if (!f) {
        pr_err("Cannot write skill: %s", file_path);
        return;
    }

    fputs(skill->content, f);
    fclose(f);
    pr_info("Installed built-in skill: %s", file_path);
}

void skill_builtin_install_all(void)
{
    for (size_t i = 0; i < sizeof(s_builtins) / sizeof(s_builtins[0]); ++i) {
        install_builtin(&s_builtins[i]);
    }
}

size_t skill_builtin_count(void)
{
    return sizeof(s_builtins) / sizeof(s_builtins[0]);
}
