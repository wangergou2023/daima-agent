# Product Requirement Document
## 项目名称
**Agent Kernel：Boss-HR 动态 Agent 生成框架**

## 版本
v1.0

## 日期
2026-06-30（初稿），2026-07-14（更新）

## 作者
Sisyphus

---

# 1. 背景

当前系统希望构建一个以 `Bus / Driver / Device` 为基础的 Agent 内核框架，使系统能够：

1. 由 **Boss Agent** 作为统一任务入口；
2. 在已有专业 Agent 不匹配时，由 **Boss 自己完成任务**；
3. 由 **HR Agent** 基于 Boss 的历史对话/执行记录，总结和蒸馏出新的专业 Agent；
4. 后续 Boss 在接到类似任务时，能将任务直接分配给相应专业 Agent 处理。

该框架的核心目标不是预先定义所有 Agent，而是让专业 Agent 从 Boss 的实际工作中“长出来”。

---

# 2. 问题定义

传统多 Agent 设计通常有两个问题：

1. **过度预定义**  
   一开始就人工设计大量专业 Agent，成本高，且很多 Agent 长期不被使用。

2. **技能拼装过于静态**  
   如果 HR 只是从 Skill Registry 中机械组合 skill 生成 Agent，会导致：
   - prompt 生硬；
   - 与实际任务风格脱节；
   - 缺乏 Boss 的真实工作偏好与经验沉淀。

因此需要一种新的机制：

- **Boss 先做事**
- **HR 再从 Boss 的实际工作记录中总结规律**
- **把规律固化为新的专业 Agent**

---

# 3. 产品目标

## 3.1 核心目标

构建一个支持以下闭环的 Agent 框架：

```text
用户任务 → Boss 分析任务 → 
(有匹配 Agent ? 分配 : Boss 自己执行) → 
记录执行过程 → HR 分析历史记录 → 
生成/更新专业 Agent → 下次任务更高效分配
```

## 3.2 业务价值

- 降低前期 Agent 设计成本
- 让 Agent 体系从真实使用中演化
- 逐步形成稳定的专业分工
- 提高 Boss 的任务分发效率
- 让新生成的 Agent 继承 Boss 的工作风格和经验

---

# 4. 非目标

以下内容不在本阶段范围内：

1. 多 Boss 协作
2. Agent 之间复杂的 peer-to-peer 自主协商
3. Skill Marketplace / 外部技能商店
4. 完整 UI 产品形态
5. 自动微调底层模型
6. 权限系统与企业级 RBAC 细节

---

# 5. 用户故事

## 5.1 作为终端用户
我希望只向 Boss 提任务，而不需要自己判断应该找哪个 Agent。

## 5.2 作为 Boss
我希望在没有合适 Agent 时，能立刻自己处理任务，而不是被 HR 阻塞。

## 5.3 作为 HR
我希望基于 Boss 的历史工作记录，自动发现高频专业模式，并将其固化为新的专业 Agent。

## 5.4 作为系统维护者
我希望 Agent 的生成是可解释、可回溯、可演化的，而不是凭空生成。

---

# 6. 核心概念

## 6.1 Boss Agent
系统统一入口，负责：
- 接收用户任务
- 分析任务所需能力
- 查找匹配的专业 Agent
- 若无匹配 Agent，则自己执行任务
- 将执行过程写入记录系统

## 6.2 HR Agent
后台优化角色，负责：
- 扫描 Boss 的历史对话/执行记录
- 识别高频能力模式
- 将 Boss 的工作风格与技能组合蒸馏为新 Agent
- 更新已有 Agent 的能力边界

## 6.3 Professional Agent
由 HR 基于 Boss 的历史工作记录生成的专业角色，如：
- 前端工程师
- 嵌入式工程师
- 营养师

这些 Agent 具备：
- 稳定身份
- 专属 system prompt
- 独立记忆
- 清晰能力边界

## 6.4 Skill Registry
Skill Registry 是**词典**，不是**菜谱**。

作用：
- 给 Boss 做任务分析时提供 skill 标签映射
- 给系统理解 transcript 时提供技能语义索引
- 给工具挂载提供依赖信息

不负责直接拼出专业 Agent。

## 6.5 Transcript Store
存储 Boss 实际执行任务的全过程记录，是 HR 生成 Agent 的核心原料。

---

# 7. 产品原则

## 7.1 Boss 不阻塞
没有匹配 Agent 时，Boss 必须自己干，而不是等待 HR 创建新 Agent。

## 7.2 HR 从经验中学习
HR 创建 Agent 的依据应来自 Boss 的真实工作记录，而不是静态 skill 拼接。

## 7.3 Agent 是蒸馏结果
专业 Agent 应是 Boss 工作风格、技能使用模式和成功经验的蒸馏结果。

## 7.4 先运行，后固化
系统优先保证任务被完成，再考虑角色抽象和组织优化。

## 7.5 渐进式专业化
专业 Agent 应该从高频重复成功模式中逐渐演化产生，而非预先定义过多角色。

---

# 8. 目标流程

## 8.1 主流程

### Step 1: 用户提交任务
用户只与 Boss 对话。

示例：
- “帮我写一个 React 表单页”
- “帮我写一个 STM32 GPIO 驱动”
- “帮我设计一份减脂餐方案”

### Step 2: Boss 分析任务
Boss 将任务映射为一组能力标签或技能向量。

### Step 3: 匹配已有 Agent
Boss 查询当前已注册的专业 Agent：

- 若找到匹配 Agent → 直接分配
- 若找不到 → Boss 自己执行

### Step 4: 记录执行过程
Boss 在执行完成后，将以下信息写入 Transcript Store：

- 原始任务
- 使用到的 skill 标签
- 推理与执行路径摘要
- 使用工具
- 执行结果
- 成功/失败标记
- 时间消耗

### Step 5: HR 后台扫描
HR 定期扫描新的 transcript 记录，对高频成功模式做聚类分析。

### Step 6: HR 生成专业 Agent
HR 从同类任务中提取：

- 高频共同技能
- 常见任务边界
- Boss 的工作风格
- 成功解决路径

并将其蒸馏成专业 Agent 的定义。

### Step 7: Boss 后续分配
当下次出现类似任务时，Boss 优先将任务分配给该新生成 Agent。

---

# 9. 关键机制设计

## 9.1 Boss 直干机制

### 描述
若没有匹配的专业 Agent，Boss 必须具备直接处理任务的能力。

### 原因
- 避免系统运行时依赖 HR
- 避免新任务首次出现时卡住
- 保证系统始终可用

### 要求
- Boss 能临时调取 Skill Registry 中的技能信息
- Boss 可以 one-shot 方式处理任务
- 该次执行不必立刻生成正式 Agent

---

## 9.2 HR 的 Agent 生成机制

### 描述
HR 不是“拼技能”，而是“看 Boss 干活记录，抽象出角色”。

### 输入
Boss 的 transcript 记录。

### 输出
新的专业 Agent 定义，包括：
- Agent 名称
- Agent 描述
- 核心能力集合
- system prompt
- 工具依赖
- 可选扩展能力
- 初始记忆

### 本质
是**行为蒸馏**，不是**静态拼装**。

---

## 9.3 Agent 生成触发条件

HR 不应在每次任务后都创建 Agent。建议触发条件满足以下部分规则：

### 触发规则候选
1. 同类 skill pattern 在近 N 天内出现 ≥ 3 次
2. 至少覆盖 2~3 个不同任务，而非同一任务重复修改
3. 成功率达到阈值（如 ≥ 80%）
4. 技能组合相似度高于阈值
5. 用户手动要求：“HR 帮我把这类工作固化成 Agent”

### 输出策略
- 可先给出建议
- 经用户确认后创建
- 或在配置允许时自动创建

---

## 9.4 Agent 蒸馏机制

### 目标
让新 Agent 不只是“会这些技能”，而是“像 Boss 一样处理这类问题”。

### 蒸馏内容
HR 应从 transcript 中提取：

1. 任务范围
2. 常用技能组合
3. 常见处理顺序
4. 风格偏好
5. 决策原则
6. 错误规避策略
7. 工具使用习惯

### 结果
形成一个更自然、更连贯的 system prompt，而不是碎片拼接。

---

# 10. 系统架构要求

## 10.1 架构总览

```text
User
  ↓
Boss Agent
  ├─ Task Analysis
  ├─ Agent Matching
  ├─ Direct Execution Fallback
  └─ Transcript Writing
        ↓
Transcript Store
        ↓
HR Agent
  ├─ Pattern Mining
  ├─ Cluster Analysis
  ├─ Distillation
  └─ Agent Registration
        ↓
Agent Registry
        ↓
Professional Agents
```

---

## 10.2 Bus / Driver / Device 映射

### Bus
负责系统内事件与消息流转。

### 新增 Driver

#### Boss Driver
负责：
- 任务分析
- Agent 路由
- fallback 直干
- transcript 写入

#### HR Driver
负责：
- transcript 扫描
- 模式聚类
- Agent 蒸馏
- Agent 注册
- Agent 演化更新

### 复用 Driver

#### Agent Runtime Driver
负责：
- 执行已有专业 Agent
- 执行 Boss 的 one-shot 任务
- 上下文隔离

#### Memory Driver
负责：
- transcript 存储
- Agent 独立记忆
- 历史索引

#### Tool Driver / LLM Driver
保持通用能力，不因 Boss/HR 角色改变。

---

# 11. 数据模型要求

## 11.1 Transcript Record

每条 Boss 执行记录至少应包含：

- task_id
- user_input
- normalized_skill_ids
- tools_used
- execution_summary
- result_status
- duration
- timestamp

可选字段：
- intermediate decisions
- error notes
- confidence
- output artifacts

---

## 11.2 Agent Definition

每个正式 Agent 至少应包含：

- agent_id
- name
- description
- origin = `distilled_from_boss`
- core_skills
- optional_skills
- system_prompt
- toolset
- model_profile
- created_at
- source_transcript_refs

---

## 11.3 Skill Metadata

Skill Registry 中每个 skill 至少应包含：

- skill_id
- name
- category
- tags
- suggested_tools
- semantic_aliases

说明：  
该元数据主要用于理解和索引，不直接作为 Agent prompt 生成源。

---

# 12. 功能需求

## 12.1 Boss 任务入口
系统必须提供统一 Boss 入口接收用户任务。

## 12.2 Agent 匹配
Boss 必须能够根据任务特征匹配已有 Agent。

## 12.3 Boss fallback 执行
在无匹配 Agent 时，Boss 必须能自己完成任务。

## 12.4 Transcript 记录
Boss 每次执行后必须记录结构化 transcript。

## 12.5 HR 扫描机制
HR 必须支持定时或事件驱动扫描 transcript。

## 12.6 模式识别
HR 必须识别高频、高成功率、相似技能组合的任务簇。

## 12.7 Agent 蒸馏生成
HR 必须能基于任务簇生成新的 Agent 定义。

## 12.8 Agent 注册
新生成 Agent 必须被写入 Agent Registry，供 Boss 下次匹配使用。

## 12.9 Agent 演化
HR 后续应支持对已有 Agent 做增量更新。

---

# 13. 非功能需求

## 13.1 可解释性
每个 Agent 必须能追溯到其来源 transcript。

## 13.2 可观测性
Boss、HR、Agent Runtime 的关键动作必须通过 Bus 可观测。

## 13.3 可扩展性
框架需支持未来更多角色类型，而不只限 Boss/HR。

## 13.4 低耦合
Boss、HR、Runtime、Memory、Tool 应通过 Bus 解耦通信。

## 13.5 可演化
Agent 定义应允许后续增量修订，而非一次生成后不可变。

---

# 14. 成功指标

## 14.1 初期指标
- Boss 可稳定处理无 Agent 覆盖的新任务
- HR 可从 transcript 中成功发现高频模式
- 系统可生成至少 1 个可复用专业 Agent

## 14.2 中期指标
- 30% 以上同类任务能被已有专业 Agent 直接接管
- 新 Agent 的任务成功率不低于 Boss 对应历史均值
- Agent 生成数量与实际复用次数之间保持正相关

## 14.3 长期指标
- Boss 逐渐从“亲自执行”转向“主要分发”
- 专业 Agent 数量增长平稳且具复用价值
- HR 成为组织结构的持续优化器

---

# 15. 风险与挑战

## 15.1 误聚类风险
HR 可能把表面相似但本质不同的任务错误归为一类。

## 15.2 过度生成风险
若阈值设置过低，系统会产生大量低价值 Agent。

## 15.3 prompt 蒸馏失真
HR 可能未能准确提炼 Boss 的工作风格。

## 15.4 Boss 过载
在 Agent 体系尚不成熟时，Boss 仍会承担大量任务。

## 15.5 记忆污染
若 transcript 质量差，可能影响后续 Agent 生成质量。

---

# 16. MVP 范围

首个版本建议仅实现以下最小闭环：

1. Boss 统一接收任务
2. Boss 匹配已有 Agent
3. 无匹配时 Boss 直接执行
4. 执行后写 transcript
5. HR 定期扫描 transcript
6. HR 发现高频模式后生成一个正式 Agent
7. Boss 在下一次相似任务中成功复用该 Agent

---

# 17. 未来演进方向

1. Agent 间委托
2. Agent 独立记忆成长
3. 多层级组织结构（Boss → Team Lead → Specialist）
4. HR 自动重构已有 Agent 边界
5. Agent 绩效评估
6. 用户可视化组织图谱
7. 技能市场与外部技能注入

---

# 18. 一句话定义

**这是一个“先由 Boss 亲自做事、再由 HR 从 Boss 的工作记录中提炼出专业角色”的动态 Agent 组织框架。**
