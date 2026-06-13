# daima-agent

嵌入式 AI Agent，Linux 内核风格架构。C11，单二进制，运行在嵌入式 Linux 开发板上。

## 架构

```
daima/
├── init/main.c          ← 启动
├── kernel/              ← 核心调度 (agent循环/路由/计划/扩展hook)
├── ipc/bus.c            ← 消息总线
├── lib/                 ← 工具库
├── net/                 ← HTTP/TLS/代理
├── fs/                  ← 文件系统
├── drivers/             ← 设备驱动 (LLM/通道/工具/存储/语音)
├── arch/                ← 平台 (host/mips/arm)
├── extensions/          ← 可加载模块
├── Kconfig              ← 功能开关
└── include/autoconf.h   ← 自动配置
```

## 构建

```bash
make defconfig     # 默认配置
make menuconfig    # 图形化选功能
./build.sh         # host 编译
./build.sh mips    # MIPS 交叉编译
./build.sh arm     # ARM 交叉编译
```

## 开发

遵循 Linux 内核开发模式:
- `make menuconfig` 开关功能
- `drivers/` 驱动层抽象
- `arch/` 平台隔离
- `extensions/module_*` 可插拔模块
- hook 生命周期: intent → prepare → run → finish
