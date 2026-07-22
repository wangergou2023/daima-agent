# 系统架构

## 整体架构

```
                    ┌──────────────┐
                    │   Channels   │  websocket / feishu / voice / vector
                    └──────┬───────┘
                           │
                    ┌──────▼───────┐
                    │     Bus      │  消息总线：入站/出站队列
                    └──────┬───────┘
                           │
              ┌────────────┼────────────┐
              │            │            │
    ┌─────▼─────┐  ┌──────▼──────┐  ┌──▼──────────┐
    │   Boss    │  │     HR      │  │ Specialist  │
    │ 任务入口  │  │  蒸馏优化   │  │  专业 Agent  │
    └─────┬─────┘  └──────┬──────┘  └──────┬───────┘
          │               │                │
          └───────────────┼────────────────┘
                          │
    ┌─────────────────────┼─────────────────────┐
    │  LLM Driver  │ Tool Driver │ Memory Driver │
    │  (多模型)    │ (26+工具)   │ (session等)   │
    └────────────────────────────────────────────┘
```

## 三层角色

| 角色 | 职责 | 生命周期 |
|------|------|---------|
| **Boss** | 统一入口，意图分析，Registry 匹配路由，兜底执行 | 硬编码，永远 active |
| **HR** | 后台扫描 Transcript，聚类蒸馏，注册新 Specialist | 预定义（`agents/hr/`），手动/自动触发 |
| **Specialist** | HR 蒸馏出的专用 Agent，独立 system_prompt + 工具集 | Registry 管理，可 retire/handoff |

## 核心流程

### 实时请求路径

```
WebSocket 消息 → ws_client → Bus → agent_turn_process_new_message
  ├─ intent_gate_classify（关键词 + LLM 双重分类）
  ├─ agent_turn_decide（intent→role + boss_route_task→delegate/fallback）
  │   └─ delegate: Registry 匹配 → 注入 Specialist system_prompt
  │   └─ fallback: Boss 亲自执行
  ├─ agent_turn_run（LLM ↔ 工具执行循环，最多 AGENT_MAX_TOOL_ITER 轮）
  └─ agent_turn_finish → 写 Transcript + 出站回复
```

### HR 蒸馏路径

```
trigger: 手动(hr scan) / 自动(每60s检查,≥5条新Transcript)
  → hr_scan_transcripts（查最近7天成功的 Transcript）
  → hr_cluster_transcripts（技能标签重叠度≥2 + 簇大小≥3 + 成功率≥80%）
  → hr_distill_agent（LLM 蒸馏 system_prompt，3次重试）
  → agent_registry_register（写入 Registry）
  → Web UI 下拉框自动出现新 Agent
```

## 关键组件

### Bus（`ipc/bus.c`）
消息总线，Driver 间唯一通信通道。入站/出站队列，阻塞 pop，非阻塞 push。

### Turn 管道（`kernel/turn/`）
单回合处理链：`entry → prepare → prompt_build → run → reply → persist`

### Agent Registry（`kernel/registry/`）
JSON 文件持久化的 Agent 定义存储。支持 CRUD + 技能匹配 + retire/handoff。启动时从 `spiffs_data/agents/` 种子加载。

### Transcript（`kernel/transcript.c`）
结构化执行记录，JSON 文件按 chat 分目录。HR 蒸馏的原材料。

### 工具门控
Specialist 被路由到时，`turn_entry.c` 调用 `tool_bus_filter_tools_json()` 将其可用工具限定为声明 toolset。

### Prompt 精简
非 Boss Agent 的 system_prompt 只包含自己的定义 + 基础 turn 上下文，不加载项目规则、会话摘要等无关内容（`turn_prompt_build.c`）。
