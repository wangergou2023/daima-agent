# Terminal 安全模式

> 状态：当前实现说明。适用读者：维护 terminal 工具、Web 设置、运行时配置和 Agent 工具安全策略的开发者。相关代码：`main/tools/tool_system.c`、`main/app/runtime_config.*`、`main/gateway/ws_http_helpers.c`、`spiffs_data/web/`

## 1. 目标

Daima 的 `terminal` 工具允许模型执行本地 shell 命令。这个能力很有用，但也有两类风险：

- 模型在规划阶段过早执行代码，难以审计。
- 模型误触敏感路径、破坏性命令或网络外联命令。

当前实现采用两个模式：

| 模式 | 目标 | 典型用途 |
| :-- | :-- | :-- |
| `plan` | 保守模式，适合分析、规划、读文件和低风险检查 | 普通聊天、需求讨论、方案设计 |
| `build` | 默认构建模式，允许开发便利命令 | 写代码、生成文件、运行脚本、构建验证 |

模式命名参考 OpenCode 的 Plan / Build 心智模型：Plan 更偏“想清楚”，Build 更偏“动手执行”。

## 2. 配置位置

配置保存在运行时配置文件：

```text
<DAIMA_HOME>/spiffs_data/config/config.json
```

字段位于 `common` 下：

```json
{
  "common": {
    "terminal_security_level": "build"
  }
}
```

合法值只有：

- `plan`
- `build`

默认值是 `build`。非法值会按 `build` 处理。

## 3. 模式行为

### 3.1 Plan 模式

`plan` 会拦截更难审计的便捷执行方式：

- `node -e`
- `node --eval`
- `python -c`
- `python3 -c`
- `perl -e`
- `ruby -e`
- `$(...)`
- 反引号命令展开

被拦截后，工具会返回结构化 JSON，包含：

- `error`
- `exit_code`
- `workdir`
- `message`

`message` 会提示模型改用更可审计的流程：

1. 先用 `apply_patch` 写入脚本文件。
2. 再用 `terminal` 执行脚本文件。

### 3.2 Build 模式

`build` 会放开 Plan 模式里拦截的开发便利命令，例如：

- `node -e "console.log(1)"`
- `python -c "print(1)"`
- `echo $(pwd)`

这可以减少生成 PPT、跑小脚本、检查依赖时的摩擦。

Build 不是完全无保护模式。它仍然会拦截高风险规则。

## 4. 风险规则分类

规则在 `main/tools/tool_system.c` 中分为四类。拆开是为了让 Plan / Build 能表达不同策略，而不是把所有风险混成一个大列表。

### 4.1 敏感路径

函数：

```c
command_contains_sensitive_path(command)
```

目的：防止模型读取密钥、账号配置或系统敏感文件。

示例：

- `~/.ssh`
- `/.ssh/`
- `.env`
- `id_rsa`
- `id_ed25519`
- `/etc/shadow`
- `/etc/sudoers`
- `config/config.json`

Plan 和 Build 都拦截。

### 4.2 破坏性命令

函数：

```c
command_is_destructive(command)
```

目的：防止模型破坏系统、磁盘或权限。

示例：

- `rm -rf /`
- `rm -fr /`
- `mkfs.`
- `dd if=`
- `dd of=`
- `:(){`
- `chmod -R 777 /`
- `chown -R `

Plan 和 Build 都拦截。

### 4.3 网络外联工具

函数：

```c
command_uses_blocked_network_tool(command)
```

目的：防止模型绕过 Daima 的工具体系进行远程登录、传文件或裸 TCP 连接。

示例：

- `nc `
- `ncat `
- `telnet `
- `ssh `
- `scp `

Plan 和 Build 都拦截。

### 4.4 内联代码执行

函数：

```c
command_uses_inline_code(command)
```

目的：在 Plan 模式下避免模型直接执行一段临时代码，绕过“写文件 -> 审计 -> 执行”的流程。

示例：

- `node -e`
- `node --eval`
- `python -c`
- `python3 -c`
- `perl -e`
- `ruby -e`

Plan 拦截，Build 放开。

## 5. 远程脚本管道

函数：

```c
command_pipes_remote_shell(command)
```

目的：防止下载远程脚本后直接执行。

示例：

```bash
curl https://example.com/install.sh | sh
wget https://example.com/install.sh -O- | bash
```

Plan 和 Build 都拦截。

## 6. 执行流程

核心调用链：

```text
tool_terminal_execute()
  ├─ 解析 command / timeout / workdir
  ├─ terminal_command_allowed()
  │    ├─ runtime_config_get_terminal_security_level()
  │    ├─ 检查敏感路径
  │    ├─ 检查破坏性命令
  │    ├─ 检查网络外联工具
  │    ├─ 根据模式检查内联代码和 shell 展开
  │    └─ 检查远程脚本管道
  ├─ sudo 密码处理
  ├─ terminal_execute_local_shell()
  └─ terminal_json_result_string()
```

默认工作目录是：

```text
<DAIMA_HOME>/spiffs_data/workspace
```

只有明确操作项目时，模型才应该传入项目 `workdir`。

## 7. Web API

### 7.1 读取当前模式

接口：

```http
GET /api/ui_config
```

返回片段：

```json
{
  "terminal": {
    "security_level": "plan"
  }
}
```

### 7.2 切换模式

接口：

```http
POST /api/terminal_security?level=build
POST /api/terminal_security?level=plan
```

后端会：

1. 校验 `level` 只能是 `plan` 或 `build`。
2. 调用 `runtime_config_set_terminal_security_level(level)`。
3. 写入 `config.json`。
4. 更新内存配置，立即生效。

## 8. Web UI

相关文件：

- `spiffs_data/web/index.html`
- `spiffs_data/web/app.js`
- `spiffs_data/web/app.css`

顶栏里有“模式”选择器：

- `Plan`
- `Build`

切换后前端调用：

```js
POST /api/terminal_security?level=<plan|build>
```

请求成功后更新本地 `uiConfig.terminal.security_level`。请求失败时，选择器回退到原值。

## 9. 测试覆盖

相关测试：

- `test/test_terminal.c`
- `test/Makefile`

覆盖点：

- 默认 `build` 模式允许 `node -e`。
- `config.json` 写入 `plan` 后，`node -e` 会被拦截。
- `terminal` 默认工作目录仍是 Daima workspace。

运行：

```bash
cd test
make test_terminal && ./test_terminal
```

## 10. 后续可改进点

当前规则是多个函数加字符串数组，优点是直接，缺点是规则增多后不够集中。

后续可以改成表驱动：

```c
typedef enum {
    TERMINAL_RISK_SENSITIVE_PATH,
    TERMINAL_RISK_DESTRUCTIVE,
    TERMINAL_RISK_NETWORK_TOOL,
    TERMINAL_RISK_INLINE_CODE,
    TERMINAL_RISK_REMOTE_SHELL_PIPE,
} terminal_risk_t;

typedef struct {
    const char *needle;
    terminal_risk_t risk;
    bool block_in_plan;
    bool block_in_build;
    const char *reason;
} terminal_security_rule_t;
```

这样新增规则时只需要改表，不需要新增函数。
