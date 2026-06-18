# ARCH PLATFORM ABSTRACTION LAYER

**Generated:** 2026-06-19
**Commit:** 0d927af
**Branch:** main

## OVERVIEW

3 平台抽象。`ARCH=host`（默认）/ `make mips` / `make arm` 切换。每架构独立 Makefile 声明 `CROSS_COMPILE`、`CFLAGS`、`obj-y`。mips/arm 复用 host 的 LLM 代理和 WebSocket 服务器（`../host/` 引用）。

## STRUCTURE

```
arch/
├── host/                      # x86_64 Linux/macOS（10 文件）
│   ├── Makefile               # CC=gcc, macOS Homebrew 自动探测
│   ├── llm_proxy_host.c       # LLM 协议路由 + HTTP 代理（1231 行）
│   ├── llm_http_client_host.c # libcurl HTTP 客户端
│   ├── llm_http_client_host.h
│   ├── ws_server_host.c       # WebSocket 服务器（363 行）
│   ├── portability.c          # #ifdef 平台差异收敛（41 行）
│   ├── portability.h          # memrchr + 空闲内存
│   ├── audio_io_stub.c        # 音频存根
│   ├── vision_capture_stub.c  # 视觉存根
│   └── voice_wake_stub.c      # 语音唤醒存根
├── mips/                      # MIPS 嵌入式（4 文件）
│   ├── Makefile               # CC=mips-linux-uclibc-gnu-gcc
│   ├── audio_io_mips.c
│   ├── vision_capture_mips.c
│   └── voice_wake_mips.c
└── arm/                       # ARM 嵌入式（4 文件）
    ├── Makefile               # CC=arm-linux-gnueabihf-gcc
    ├── audio_io_arm.c
    ├── vision_capture_arm.c
    └── voice_wake_arm.c
```

## WHERE TO LOOK

| Task | Location | Notes |
|------|----------|-------|
| LLM 协议路由 + 调用 | `host/llm_proxy_host.c` | OpenAI/Anthropic 协议分发，异步/同步模式 |
| WebSocket 服务器 | `host/ws_server_host.c` | RFC 6455 握手 + 帧协议，HTTP/WS 同端口 |
| 平台差异收敛 | `host/portability.c` | `memrchr`（GNU 扩展→macOS 自实现），空闲内存 |
| macOS Homebrew 路径 | `host/Makefile:5-12` | 自动探测 `brew --prefix openssl` / `curl` |
| MIPS 交叉编译 | `mips/Makefile` | `-latomic -limp -lsysutils -lalog -lrt -lstdc++` |
| ARM 交叉编译 | `arm/Makefile` | `arm-linux-gnueabihf-` 工具链 |

## CONVENTIONS

- **`ARCH=host` 默认**：`CROSS_COMPILE` 为空，原生 gcc；`make mips` / `make arm` 切换
- **Makefile 独立**：每架构声明自己的 `CC`、`LDFLAGS`、`obj-y`；无共享 Makefile 片段
- **Host macOS 适配**：`uname -s` 检测 Darwin，自动探测 Homebrew 的 `openssl@3` / `curl` 路径
- **交叉编译工具链**：mips → `mips-linux-uclibc-gnu-`，arm → `arm-linux-gnueabihf-`
- **平台差异隔离**：所有 `#ifdef __linux__` / `#ifdef __APPLE__` 仅出现在 `host/portability.c`，业务代码通过 `portability.h` 调用
- **文件共享**：mips/arm 的 `obj-y` 通过 `../host/llm_proxy_host.o` 等路径复用 host 源文件，不复制代码
- **Stub 模式**：host 的 audio/vision/voice 为 stub（空实现），mips/arm 有真实硬件驱动
- **Stub 去重**：mips/arm 的 audio_io/vision_capture/voice_wake 通过 `../host/` 引用 host 的 stub 实现，不再维护独立的 dup 文件
