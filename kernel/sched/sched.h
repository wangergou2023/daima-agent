/* 多 Agent 调度器核心数据结构。
 * 管理 agent 生命周期：入队 → 分发 → 启动 → 等待 → 合并 → 回收。
 * 三类调度角色（PLANNER/EXECUTOR/REVIEWER）按 intent 映射，最多 4 agent 并行。 */

#pragma once

#include "intent.h"
#include "plan.h"
#include "drivers/llm/llm_proxy.h"
#include "err.h"
#include "linux/list.h"

#include <stdbool.h>
#include <stddef.h>

#include "cjson.h"

/* 调度器容量限制 */
#define SCHED_MAX_AGENTS 4		/* 最大并行 agent 数 */
#define SCHED_CLASSES 3			/* 调度类总数（PLANNER/EXECUTOR/REVIEWER） */
#define SCHED_RESULT_MAX 4096		/* 单个 agent 结果缓冲区大小 */
#define SCHED_MERGED_MAX 16384		/* 多 agent 合并结果缓冲区大小 */

/* 调度类标识：按角色分配不同的 system prompt 后缀和优先级 */
enum sched_class_id {
	SCHED_CLASS_PLANNER = 0,	/* 规划者：生成计划，不可写代码 */
	SCHED_CLASS_EXECUTOR = 1,	/* 执行者：按计划实现代码 */
	SCHED_CLASS_REVIEWER = 2,	/* 审查者：评审执行结果 */
};

/* agent 生命周期状态 */
enum sched_agent_state {
	SCHED_AGENT_INIT = 0,		/* 初始化：已分配未入队 */
	SCHED_AGENT_WAITING,		/* 等待：在就绪队列中等待 CPU */
	SCHED_AGENT_RUNNING,		/* 运行中：正在执行 LLM 调用 */
	SCHED_AGENT_DONE,		/* 完成：正常结束 */
	SCHED_AGENT_TIMEOUT,		/* 超时：执行超时被强制终止 */
	SCHED_AGENT_ERROR,		/* 错误：执行过程中发生异常 */
};

/* 调度类：定义一类 agent 的行为特征 */
struct sched_class {
	const char *name;		/* 调度类名称（如 "planner"） */
	int priority;			/* 优先级：数值越大越优先调度 */
	const char *prompt_suffix;	/* 追加到 system prompt 的角色指令后缀 */
};

/* 可调度 agent：一个 LLM 调用实例的完整生命周期上下文 */
struct sched_agent {
	struct list_head run_list;	/* 链表节点：挂入调运行队列 */
	int pid;			/* agent 进程 ID（唯一标识） */
	enum sched_class_id class;	/* 所属调度类 */
	enum sched_agent_state state;	/* 当前生命周期状态 */
	err_t error;			/* 错误码：SCHED_AGENT_ERROR 时有效 */
	char prompt_add[1024];		/* 额外追加的 prompt 指令 */
	char task_desc[512];		/* 任务描述文本 */
	llm_async_chat_t *async_chat;	/* LLM 异步聊天句柄（用于流式调用） */
	llm_response_t response;	/* LLM 返回的完整响应 */
	cJSON *scoped_messages;		/* 作用域消息列表（仅该 agent 可见的历史） */
	char result[SCHED_RESULT_MAX];	/* agent 输出结果文本 */
	unsigned long start_time;	/* 启动时间戳（用于超时检测） */
	unsigned long finish_time;	/* 完成时间戳 */
};

/* 调度运行队列：管理一轮调度中的所有 agent */
struct sched_runqueue {
	struct list_head agent_list;			/* agent 链表头 */
	struct sched_agent agents[SCHED_MAX_AGENTS];	/* agent 数组（静态分配） */
	int nr_running;					/* 当前运行中 agent 数 */
	int nr_agents;					/* 队列中总 agent 数 */
	int timeout_ms;					/* 单个 agent 超时阈值（毫秒） */
	char merged[SCHED_MERGED_MAX];			/* 多 agent 合并输出缓冲区 */
};

/* 初始化调度器子系统 */
void sched_init(void);

/**
 * 按意图分发 agent 到运行队列。
 * @param intent    用户消息意图（QA/IMPLEMENT/INVESTIGATE 等）
 * @param plan      评审后的执行计划（可为 NULL）
 * @param user_msg  用户原始消息
 * @param rq        输出：填充的调度运行队列
 * @return 成功返回 0，失败返回错误码
 */
err_t sched_dispatch(enum intent intent, const struct plan *plan,
			   const char *user_msg, struct sched_runqueue *rq);

/**
 * 启动队列中所有 agent 的 LLM 调用。
 * @param rq            运行队列
 * @param system_prompt  系统提示词
 * @param messages       对话历史（JSON 数组）
 * @param tools          工具定义 JSON
 */
void sched_start(struct sched_runqueue *rq,
		 const char *system_prompt, cJSON *messages, const char *tools);

/**
 * 等待队列中所有 agent 完成（阻塞调用）。
 * @param rq  运行队列
 * @return 成功返回 0
 */
err_t sched_wait(struct sched_runqueue *rq);

/**
 * 合并所有 agent 的输出到单个缓冲区。
 * @param rq     运行队列
 * @param output 输出缓冲区
 * @param size   缓冲区大小
 */
void sched_merge(struct sched_runqueue *rq, char *output, size_t size);

/**
 * 清理运行队列，释放 agent 资源。
 * @param rq  运行队列
 */
void sched_exit(struct sched_runqueue *rq);

/* 调度类查询接口 */
const struct sched_class *sched_class_for_id(enum sched_class_id id);
const struct sched_class *sched_class_for_intent(enum intent intent, int *count);
const char *sched_class_name(enum sched_class_id id);

/* 队列操作：入队/出队/调度选择 */
void sched_enqueue(struct sched_runqueue *rq, const struct sched_class *cls,
		   const char *task);
void sched_dequeue(struct sched_runqueue *rq, struct sched_agent *agent);
struct sched_agent *sched_pick_next(struct sched_runqueue *rq);
void sched_complete(struct sched_runqueue *rq, struct sched_agent *agent,
		    err_t err);

/* agent 生命周期操作 */
void sched_agent_init(struct sched_agent *agent, const struct sched_class *cls,
		      const char *task);
void sched_agent_launch(struct sched_agent *agent, const char *prompt,
			cJSON *messages, const char *tools);
bool sched_agent_is_done(struct sched_agent *agent);
void sched_agent_reap(struct sched_agent *agent);
