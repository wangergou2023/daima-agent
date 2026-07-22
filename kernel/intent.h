#pragma once

#include "err.h"

#include <stdbool.h>

enum intent {
    INTENT_QA = 0,          // 问答
    INTENT_IMPLEMENT,       // 实现
    INTENT_INVESTIGATE,     // 调研
    INTENT_FIX,             // 修复
    INTENT_OPEN,            // 开放
    INTENT_COUNT
};

const char *intent_name(enum intent intent);
err_t intent_gate_classify(const char *user_message,
                                  enum intent *out_intent);
bool intent_gate_text_looks_like_action_request_for_test(const char *user_message);
enum intent intent_gate_fallback_for_text(const char *user_message);

/* ──── Boss 任务分析 ──── */

#define TASK_SKILL_TAGS_LEN 256

typedef struct {
    enum intent intent_type;
    char capability_tags[TASK_SKILL_TAGS_LEN];  /* 空格分隔 */
    float confidence;
    bool requires_file_write;
    bool requires_network;
    int estimated_complexity;  /* 1=low, 2=medium, 3=high */
} task_analysis_t;

/**
 * 分析用户任务：返回意图类型 + 能力标签 + 约束。
 * 先用关键词预判，再用现有 LLM 分类逻辑。
 */
err_t intent_gate_analyze_task(const char *user_message,
                               task_analysis_t *out_analysis);
