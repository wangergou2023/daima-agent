# daima-agent

代马 AI 助手 — Vector 机器人嵌入式 Agent，支持中文语音对话、AI Agent 推理、机器人控制。

## 架构

```
daima (C, FreeRTOS-style task system)
  ├── voice_channel  → BigModel ASR/TTS (中文语音)
  ├── vector_channel → MCP 客户端 (popen robot-mcp)
  ├── llm            → OpenAI-compatible API (DeepSeek v4-pro)
  ├── agent          → Agent loop (turn prepare, tool use, finish)
  ├── tools          → 本地工具注册 (cron, skill, file, memory)
  ├── channels       → 飞书 / WebSocket 消息通道
  └── tts_player     → 分句 + BigModel TTS → PCM → Unix socket
```

## 构建

### macOS 交叉编译
```bash
./build-arm.sh
```

### Linux 本地编译
```bash
./build.sh
```

需要 ARM 交叉编译工具链和 **daima-sdk**（对应头文件和库）。

## 部署

```bash
# 一键部署（编译 + 上传 + 重启服务）
./install-robot.sh <robot-ip>

# 或手动
scp build-arm/daima root@<ip>:/data/daima/bin/daima
ssh root@<ip> "systemctl restart daima"
```

## 依赖

| 组件 | 路径 |
|------|------|
| cJSON | `third_party/cjson/` |
| libcurl | `third_party/curl/` |
| ARM 交叉编译链 | `third_party/build_libs_arm/` |

## 目录

| 目录 | 内容 |
|------|------|
| `main/agent/` | Agent 循环、context builder、turn prepare/finish |
| `main/voice/` | BigModel ASR/TTS 接口、VAD、分句播放 |
| `main/channels/vector/` | Vector MCP 客户端、音频缓冲、方向传感器 |
| `main/tools/` | 本地工具（vector 控制、cron、file、memory） |
| `main/llm/` | OpenAI-compatible API 调用 |
| `main/app/` | 应用入口、路由器 |
| `spiffs_data/skills/` | LLM Skill 文件（robot-control 等） |

## 相关项目

- [vector-mcp](https://github.com/wangergou2023/vector-mcp) — MCP 服务器，提供 19 个机器人控制工具
- [wire-os](https://github.com/wangergou2023/wire-os) — 机器人系统完整部署脚本
