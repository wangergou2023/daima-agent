# Circular Dependency Analysis

**Generated:** 2026-06-12
**Commit:** 222497d

## Current Dependency Graph

The project has a highly centralized dependency structure with one large strongly connected component.

### Module-Level Dependencies

```
daima_host (root)
  ├── app (bootstrap, paths, config)
  ├── agent (loop, turn lifecycle)
  ├── tools (registry, implementations)
  ├── channels (feishu, vector)
  ├── llm (proxy, payloads)
  ├── memory (session store)
  ├── gateway (websocket server)
  ├── voice (ASR/TTS)
  ├── bus (message bus)
  ├── cron (scheduled tasks)
  └── platform (OS abstraction)

agent ──> app, bus, channels, llm, memory, skills, tools, vision, voice
tools ──> app, bus, channels, cron, llm, memory, skills, work_items
channels ──> app, bus, proxy, voice
gateway ──> agent, app, bus, llm, memory, pet
voice ──> app, audio, bus, channels, proxy
llm ──> agent, app, host_http, daima_base64, daima_text
memory ──> app, daima_config
cron ──> app, bus, channels
app ──> agent, bus, channels, gateway, voice
```

### Identified Cycles

1. **agent -> app -> channels -> agent** (transitive)
2. **agent -> tools -> channels -> agent** (transitive)
3. **app -> gateway -> agent -> app** (transitive)

### High Coupling Hotspots

| Module | Fan-Out | Risk |
|--------|---------|------|
| daima_host.c | 15+ | Highest |
| agent/agent_loop.c | 10+ | High |
| tools/tool_registry.c | 15+ | High |
| app/channel_runtime.c | 8+ | Medium |
| gateway/ws_server_host.c | 8+ | Medium |

## Recommended Solutions

### Phase 1: Reduce Hub Coupling

1. **Introduce callback interfaces** for cross-module communication:
   ```c
   typedef void (*channel_send_fn_t)(const char *chat_id, const char *message);
   typedef void (*tool_execute_fn_t)(const char *name, const char *input, char *output, size_t size);
   ```

2. **Make message_bus a pure queue** with no channel knowledge:
   - Remove feishu/vector includes from bus/
   - Use opaque message types

3. **Create app/core.h** with forward declarations:
   ```c
   #pragma once
   typedef struct daima_runtime daima_runtime_t;
   typedef struct daima_channel daima_channel_t;
   ```

### Phase 2: Layer Separation

```
Layer 1: Platform (OS abstraction, no business logic)
Layer 2: Infrastructure (HTTP, JSON, filesystem, logging)
Layer 3: Core (message bus, memory store, config)
Layer 4: Services (LLM proxy, channels, tools)
Layer 5: Application (agent loop, routing, orchestration)
```

### Phase 3: Dependency Inversion

- Use opaque pointers for cross-module structs
- Pass function pointers instead of direct calls
- Event-driven architecture for loose coupling

## Files to Refactor First

1. `main/daima_host.c` - Reduce direct includes
2. `main/agent/agent_loop.c` - Extract channel callbacks
3. `main/tools/tool_registry.c` - Use dynamic registration
4. `main/app/channel_runtime.c` - Split into per-channel files

## Notes

- No clean acyclic layering exists today
- Refactors should assume changes can ripple through many modules
- Start with leaf utilities, then hubs, then cross-layer code
