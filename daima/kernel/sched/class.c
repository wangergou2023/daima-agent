#include "sched.h"

#include <stddef.h>

static const struct sched_class sched_classes[SCHED_CLASSES] = {
    [SCHED_CLASS_PLANNER] = {
        .name = "PLANNER",
        .priority = 0,
        .prompt_suffix = "你当前处于规划模式。请先分析用户需求，然后生成一个清晰的执行计划（步骤列表），不要直接编写代码。回复格式：## 执行计划\n1. ...\n2. ...",
    },
    [SCHED_CLASS_EXECUTOR] = {
        .name = "EXECUTOR",
        .priority = 1,
        .prompt_suffix = "你当前处于执行模式。请严格按照以下计划逐步执行，每完成一个步骤后确认完成状态，再继续下一步。不要跳跃或跳过步骤。",
    },
    [SCHED_CLASS_REVIEWER] = {
        .name = "REVIEWER",
        .priority = 2,
        .prompt_suffix = "你当前处于审查模式。请检查前面的执行结果：1)每个步骤是否已完成 2)代码是否有遗漏或错误 3)是否需要补充或修正。如有问题请指出，如已完成请回复'审查通过'。",
    },
};

static const enum sched_class_id implement_classes[] = {
    SCHED_CLASS_PLANNER,
    SCHED_CLASS_EXECUTOR,
    SCHED_CLASS_REVIEWER,
};

static const enum sched_class_id fix_classes[] = {
    SCHED_CLASS_EXECUTOR,
    SCHED_CLASS_REVIEWER,
};

static const enum sched_class_id single_executor_class[] = {
    SCHED_CLASS_EXECUTOR,
};

const struct sched_class *sched_class_for_id(enum sched_class_id id)
{
    if (id < 0 || id >= SCHED_CLASSES) {
        return NULL;
    }
    return &sched_classes[id];
}

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
