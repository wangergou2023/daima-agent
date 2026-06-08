# Web 宠物系统说明

> 状态：当前实现说明。适用读者：维护 Web UI、宠物互动、宠物资源包和宠物通道的开发者。相关代码：`spiffs_data/web/pet.js`、`spiffs_data/web/app.js`、`main/pet/`、`main/gateway/ws_http_helpers.c`

## 这是什么

这是 Daima Web 聊天页右下角的宠物系统。当前已经接入 `guga` 宠物包，并支持：

- 在网页右下角渲染宠物 spritesheet 动画
- 根据请求生命周期和用户输入空闲时间自动切换过程态
- 允许 AI 在最终回复末尾追加隐藏指令，选择宠物表达态
- 为宠物提供独立的 `pet` 互动通道，不混入主聊天记录
- 通过 skill 约束 AI 的选态方式，避免乱用状态

当前宠物资源位于 `spiffs_data/guga.codex-pet/`，核心文件如下：

- `spiffs_data/guga.codex-pet/pet.json`
- `spiffs_data/guga.codex-pet/spritesheet.webp`

默认宠物不再写死在前端，而是通过运行时配置下发：

- `spiffs_data/config/config.json` -> `web.default_pet_package_id`
- `GET /api/ui_config` -> `{ "pet": { "default_package_id": "...", "packages": [...] } }`
- 前端会把用户上次手动选择的宠物记到浏览器本地，下次优先恢复

## 核心能力

### 1. 资源加载

后端通过 `/pets/...` 路由暴露 `.codex-pet` 资源，前端启动时会读取：

- `/pets/guga.codex-pet/pet.json`
- `/pets/guga.codex-pet/spritesheet.webp`

对应代码：

- `main/gateway/ws_http_helpers.c`
- `spiffs_data/web/pet.js`

### 2. 前端动画播放

前端使用单张 `spritesheet.webp` 做逐帧播放，不依赖 GIF。

当前实现要点：

- 每帧大小固定为 `192x208`
- spritesheet 总布局按 `8 列 x 9 行` 处理
- 每一行的实际帧数不完全一致
- 前端会在加载图片后扫描每一行是否存在透明外的有效像素，从而自动推断真实帧数，避免播放到空白帧时闪烁

这部分逻辑在：

- `spiffs_data/web/pet.js`

### 3. 两层状态控制

宠物状态分成两层：

#### 过程态

由前端自动控制，不依赖 AI 判断：

- `running`：用户消息已发出，等待最终回复返回中
- `idle`：初始状态，以及用户约 4 秒没有新输入时

#### 表达态

由 AI 在最终回复中选择：

- `idle`：普通结束、回到待机
- `review`：分析、检查、审查
- `waving`：轻互动、友好回应
- `jumping`：明显完成、庆祝、突破
- `failed`：失败、报错、缺资源

补充说明：

- `runRight`、`runLeft` 目前保留为表演态
- 只有用户明确想看方向动作效果时，才建议 AI 使用

## 主流程

### 页面初始化

1. `index.html` 提供右下角宠物挂件容器和宠物选择器
2. `app.js` 先请求 `/api/ui_config`
3. 按默认配置和本地记忆决定当前宠物包
4. `pet.js` 再读取对应 `pet.json`
5. 加载 `spritesheet.webp`
6. 扫描每行有效帧数
7. 初始状态显示为 `idle`

### 用户切换宠物

1. 前端根据 `/api/ui_config` 里的 `packages` 渲染一个轻量下拉选择器
2. 用户切换后，旧的 `petController` 会销毁
3. 新的 `petController` 立即按所选包重新加载资源
4. 当前选择会记到浏览器本地，下次打开页面优先恢复

### 连接建立

1. WebSocket 连接成功
2. 如果当前没有待处理回复，则切到 `idle`
3. 如果有尚未完成的请求，则保持 `running`

### 用户发消息

1. 提交消息
2. 前端把 `pendingAssistantResponse` 设为 `true`
3. 宠物切到 `running`
4. 等待工具消息或最终回复

### 用户点击宠物

1. 前端先本地播放“向左跑一下再跑回来”的交互动作
2. 同时通过 WebSocket 发送独立的 `pet_action`
3. 后端把事件写入 `pet` 通道，而不是主 `websocket` 通道
4. AI 返回 `pet_response`
5. 前端只在宠物头顶展示气泡，不把这条内容加入主聊天消息列表

### 工具执行中

1. 如果 Web 端收到 `tool` 类型消息
2. 宠物继续保持 `running`

### 最终回复到达

1. 前端解析回复文本
2. 查找末尾隐藏指令 `[[pet:state=<state>]]`
3. 从展示文本中移除该指令
4. 根据状态切换宠物动作
5. 若状态为 `waving`、`jumping`、`review`，播放一小段后自动回到 `idle`

### 连接异常

当前默认不额外控制“未连接 / 断线 / 重连中”的宠物状态。

## 关键文件

### 后端

- `main/gateway/ws_http_helpers.c`
  - 提供 `/pets/...` 静态资源访问，以及 `/api/ui_config`
- `main/app/runtime_config.c`
  - 读取 `web.default_pet_package_id`
- `main/pet/pet_event.c`
  - 统一维护宠物协议常量、chat_id 规则、互动 prompt、pet 通道附加提示词
- `main/gateway/ws_client_session.c`
  - 解析 `pet_action`，调用 `main/pet/` 辅助函数后写入独立 `pet` 通道
- `main/gateway/ws_server_host.c`
  - 把 `pet_response` 发回对应 WebSocket 会话
- `main/agent/agent_turn_prepare.c`
  - 为 `pet` 通道挂接统一的提示词约束
- `install.sh`
  - 安装时把 `spiffs_data/*.codex-pet` 拷贝到运行目录
- `main/agent/context_builder.c`
  - 保留通用系统提示；宠物专用附加策略不再写死在这里

### 前端

- `spiffs_data/web/index.html`
  - 提供宠物挂件 DOM、气泡容器、宠物选择器，并加载 `pet.js`
- `spiffs_data/web/app.js`
  - 聊天页主控；负责拉取 `ui_config`、渲染宠物选择器、记忆当前宠物，并在 WebSocket 生命周期里调用 `petController`
- `spiffs_data/web/pet.js`
  - 宠物前端控制器；负责资源加载、逐帧动画、状态切换、气泡、点击/拖拽、`pet_action` / `pet_response`，以及切换时的销毁重建
- `spiffs_data/web/app.css`
  - 右下角布局、阴影、悬浮感、互动气泡、移动端缩放

### 技能

- `spiffs_data/skills/pet-director/SKILL.md`
  - 约束 AI 何时、如何输出 `[[pet:state=...]]`

## 隐藏指令协议

AI 在最终回复末尾可以附加一个隐藏标记：

```text
[[pet:state=idle]]
```

规则：

- 只能放在最终回复末尾
- 一次只应输出一个
- 前端会把它从展示内容中去掉
- 目前支持的状态：
  - `idle`
  - `runRight`
  - `runLeft`
  - `waving`
  - `jumping`
  - `failed`
  - `waiting`
  - `running`
  - `review`

建议：

- 过程态优先交给前端自动控制
- AI 优先只选择表达态

## 当前默认策略

当前采用“前端控过程、AI 控表达”的策略：

- 前端自动：
  - `running`
  - `idle`
- AI 选择：
  - `review`
  - `waving`
  - `jumping`
  - `failed`

这样做的原因：

- 请求是否仍在等待结果，前端最清楚
- 回答语气和情绪变化 AI 最清楚
- 两边职责分离后更稳定，不容易互相覆盖

## 如何新增或替换宠物

### 1. 准备资源包

将新的宠物包放进：

- `spiffs_data/<pet-name>.codex-pet/`

目录下至少包含：

- `pet.json`
- `spritesheet.webp`

### 2. 满足当前前端假设

当前前端默认假设：

- 单帧大小为 `192x208`
- 总体布局为 `8 列 x 9 行`

如果未来宠物包格式不同，需要同步修改前端常量：

- `PET_FRAME_WIDTH`
- `PET_FRAME_HEIGHT`
- `PET_SPRITE_COLUMNS`
- `PET_STATE_CONFIG`

### 3. 安装

执行：

```bash
./install.sh
```

安装脚本会把 `.codex-pet` 目录一起拷贝到运行目录。

### 4. 切换默认宠物

修改运行时配置即可：

```json
{
  "web": {
    "default_pet_package_id": "guga.codex-pet"
  }
}
```

对应文件：

- `spiffs_data/config/config.json`

前端启动时会请求 `/api/ui_config`，再按配置创建 `petController`。

如果用户在网页里手动切换过宠物，浏览器本地选择会覆盖这个默认值。

## 调试建议

### 资源是否可访问

先确认后端能否返回宠物资源：

```bash
curl http://127.0.0.1:1234/pets/guga.codex-pet/pet.json
```

### 网页是否加载到新脚本

如果页面效果没变化，优先排查：

- 是否重启了 `./build-host/daima`
- 是否刷新了浏览器
- 是否命中了旧缓存
- `/api/ui_config` 是否已经返回新的 `default_package_id`
- `/api/ui_config` 的 `packages` 里是否已经包含新的 `.codex-pet`

### 宠物不动或状态不对

重点检查：

- `app.js` 和 `pet.js` 是否成功加载
- `pet.json` 是否返回 200
- `spritesheet.webp` 是否返回 200
- AI 回复里是否真的带了 `[[pet:state=...]]`

### 有闪烁

如果再次出现“跑完闪一下”的问题，通常是：

- 某一行真实有效帧数少于预设帧数
- 或者新宠物资源布局和当前前端假设不一致

优先检查：

- 该行是否存在空白尾帧
- 帧尺寸是否仍是 `192x208`
- 行列布局是否仍是 `8x9`

## 最近改动 / 注意事项

- 已支持 `.codex-pet` 资源通过 `/pets/...` 对外暴露
- 已支持安装脚本自动复制宠物包
- 已支持前端自动检测每行有效帧数，避免空白帧闪烁
- 已支持 AI 通过隐藏指令控制表达态
- 已把宠物协议字符串、chat_id 规则、互动 prompt、pet 通道附加提示词集中到 `main/pet/`
- 已通过 `pet-director` skill 限制主聊天里的宠物表达态边界
- 已支持后端自动发现多个 `.codex-pet` 包，并在网页右下角切换

后续可以继续做的方向：

- 给宠物加入点击、hover、拖拽互动
- 让 AI 的状态选择更细化，但仍保持“过程态 / 表达态”分层
