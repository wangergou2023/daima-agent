# Daima 文档

这里是 Daima 的项目文档入口。文档按用途分层：

- `architecture/`：代码结构、运行框架、模块边界，适合梳理后端和整体架构。
- `features/`：已经落地的功能说明，适合维护具体功能或排查行为。
- `designs/`：设计方案和阶段规划，适合了解某个能力为什么这样做、后续怎么演进。
- `superpowers/`：Codex/Superpowers 工作流生成的规格和执行计划，保留原结构。

## 推荐阅读顺序

1. [Daima 代码架构](architecture/daima-code-architecture.md)
   先了解项目定位、启动流程、Agent 主循环、LLM 层、工具系统和通道边界。

2. [Web 宠物系统](features/web-pet.md)
   了解 Web UI 右下角宠物的资源加载、状态流转、隐藏指令协议和调试方式。

3. [Terminal 安全模式](features/terminal-security-modes.md)
   了解 Web 顶栏 Plan / Build 模式、terminal 工具拦截规则、配置写入和 API。

4. [Work Item 演进系统设计](designs/work-item-evolution.md)
   了解如何把用户反馈、日志和测试失败沉淀为结构化事项，支撑后续自演进能力。

## 维护约定

- 新的“当前实现说明”放到 `features/` 或 `architecture/`。
- 新的“未来方案/阶段规划”放到 `designs/`。
- 文档开头保留 `状态`、`适用读者`、`相关代码` 三项，方便判断是否该继续读。
- 文件名使用英文短横线，正文可以继续使用中文。
