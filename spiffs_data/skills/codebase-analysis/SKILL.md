---
name: 代码库分析
description: 用搜索优先、定点精读的方式分析任意代码仓库的结构、主链路和关键模块。
---

# 代码库分析

分析任意代码仓库时，优先建立结构地图，再定点读取关键文件，避免无目标地阅读全文。

## 何时使用
- 用户要求分析当前目录、当前工程、某个仓库或某个模块
- 用户想知道入口、模块划分、主流程、依赖关系
- 用户准备移植某个功能，想先定位核心文件和关键链路

## 使用步骤
1. 先用 `list_dir` 或 `search_files target=files output_mode=files_only` 看目录结构。
2. 再用 `search_files` 缩小范围，不要一开始就连续读取很多源码文件。
3. 优先找这些信息：
   - 构建入口：构建脚本、项目配置文件
   - 进程入口：`main`、`start`、`init`
   - 主循环/调度：agent、event loop、scheduler、dispatcher
   - 工具/能力注册：registry、router、toolset、plugin loader
   - 外部接口：HTTP、WebSocket、CLI、消息通道、RPC
   - 状态与存储：session、memory、cache、config
4. 用 `read_file` 分页读取最相关的文件，必要时再用 `offset` 继续看后半段。
5. 输出时优先总结：
   - 这是什么项目
   - 主入口在哪里
   - 核心模块有哪些
   - 关键数据流/调用链是什么
   - 如果要改功能，应该先看哪些文件

## 推荐搜索姿势
- 先看文件分布：
  - `search_files {"pattern":"agent","target":"files","path":"./main","output_mode":"files_only"}`
- 再看命中密度：
  - `search_files {"pattern":"init","target":"content","path":".","file_glob":"*.c","output_mode":"count"}`
- 最后看具体上下文：
  - `search_files {"pattern":"init","target":"content","path":".","file_glob":"*.c","output_mode":"content","limit":10}`

## 注意事项
- 不要默认把整个仓库所有源码和头文件全文读完。
- 头文件通常只在确认接口、结构体、宏定义时再读。
- 回答时优先讲结构和主链路，而不是逐文件流水账。
- 如果用户只要概览，读到足够支撑结论时就可以停止继续探索。
