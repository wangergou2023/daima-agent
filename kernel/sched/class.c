/* SCHED_CLASS 调度类表：定义 PLANNER/EXECUTOR/REVIEWER 三类 agent 的属性。
 * 每类有 name（标识）、priority（调度优先级，越小越优先）、prompt_suffix（角色注入）。
 * sched_class_for_intent() 根据意图返回应激活的调度类组合。 */

#include "sched.h"

#include <stddef.h>

/* 调度类定义表：PLANNER 只分析规划不写代码，EXECUTOR 按计划执行，REVIEWER 审查结果。 */
static const struct sched_class sched_classes[SCHED_CLASSES] = {
    [SCHED_CLASS_PLANNER] = {
        .name = "PLANNER",
        .priority = 0,
        .prompt_suffix = "You are a PLANNER. Analyze the task and create a step-by-step plan. Output format: ## Plan / 1. step / 2. step. Do NOT write code.",
    },
    [SCHED_CLASS_EXECUTOR] = {
        .name = "EXECUTOR",
        .priority = 1,
        .prompt_suffix = "You are an EXECUTOR. Follow the plan step by step. Confirm each step after completion before moving to the next.",
    },
    [SCHED_CLASS_REVIEWER] = {
        .name = "REVIEWER",
        .priority = 2,
        .prompt_suffix = "You are a REVIEWER. Check the execution results: 1) Are all steps complete? 2) Any errors or omissions? 3) Any corrections needed? Reply PASS if all good.",
    },
};

/* IMPLEMENT 意图：三步流水线 PLAN → EXEC → REVIEW */
static const enum sched_class_id implement_classes[] = {
    SCHED_CLASS_PLANNER,
    SCHED_CLASS_EXECUTOR,
    SCHED_CLASS_REVIEWER,
};

/* FIX 意图：直接执行 + 审查，跳过规划 */
static const enum sched_class_id fix_classes[] = {
    SCHED_CLASS_EXECUTOR,
    SCHED_CLASS_REVIEWER,
};

/* QA/OPEN/INVESTIGATE 意图：单 EXECUTOR 处理 */
static const enum sched_class_id single_executor_class[] = {
    SCHED_CLASS_EXECUTOR,
};

/** 根据调度类 ID 查找对应的调度类定义。 */
const struct sched_class *sched_class_for_id(enum sched_class_id id)
{
    if (id < 0 || id >= SCHED_CLASSES) {
        return NULL;
    }
    return &sched_classes[id];
}

/** 根据意图返回应激活的调度类列表。
 *  IMPLEMENT → PLANNER+EXECUTOR+REVIEWER（3个）
 *  FIX → EXECUTOR+REVIEWER（2个）
 *  QA/OPEN/INVESTIGATE → EXECUTOR（1个）
 *  @param count  输出：调度类个数 */
const struct sched_class *sched_class_for_intent(enum intent intent, int *count)
{
    static struct sched_class selected[SCHED_MAX_AGENTS];
    const enum sched_class_id *ids = NULL;
    int nr = 0;

    switch (intent) {
    case INTENT_IMPLEMENT:
        ids = implement_classes;
        nr = (int)(sizeof(implement_classes) / sizeof(implement_classes[0]));
        break;
    case INTENT_FIX:
        ids = fix_classes;
        nr = (int)(sizeof(fix_classes) / sizeof(fix_classes[0]));
        break;
    case INTENT_QA:
    case INTENT_OPEN:
    case INTENT_INVESTIGATE:
        ids = single_executor_class;
        nr = 1;
        break;
    case INTENT_COUNT:
    default:
        nr = 0;
        break;
    }

    if (count) {
        *count = nr;
    }
    for (int i = 0; i < nr && i < SCHED_MAX_AGENTS; i++) {
        selected[i] = sched_classes[ids[i]];
    }
    return nr > 0 ? selected : NULL;
}

const char *sched_class_name(enum sched_class_id id)
{
    const struct sched_class *cls = sched_class_for_id(id);
    return cls ? cls->name : "UNKNOWN";
}
