/* Agent Registry: 动态 Specialist 注册、匹配与生命周期管理。 */
#pragma once

#include "err.h"
#include <stdbool.h>
#include <stddef.h>
#include <time.h>

#define AGENT_ID_LEN              64
#define AGENT_NAME_LEN            64
#define AGENT_DESC_LEN            256
#define AGENT_ORIGIN_LEN          24
#define AGENT_CORE_SKILLS_LEN     256
#define AGENT_OPTIONAL_SKILLS_LEN 256
#define AGENT_SYSTEM_PROMPT_LEN   4096
#define AGENT_TOOLSET_LEN         256
#define AGENT_MODEL_PROVIDER_LEN  32
#define AGENT_MODEL_NAME_LEN      64
#define AGENT_SOURCE_TXN_REFS_LEN 512
#define AGENT_LIFECYCLE_STATUS_LEN 16
#define AGENT_RETIRE_REASON_LEN   128
#define AGENT_HANDOFF_TO_LEN      AGENT_ID_LEN

#define AGENT_PROFILES_MAX        16
#define AGENT_MATCH_MAX           8

/** Agent 定义。 */
typedef struct {
    char agent_id[AGENT_ID_LEN];
    char name[AGENT_NAME_LEN];
    char description[AGENT_DESC_LEN];
    char origin[AGENT_ORIGIN_LEN];                  /* "distilled_from_boss" / "manual" */

    char core_skills[AGENT_CORE_SKILLS_LEN];         /* 空格分隔 */
    char optional_skills[AGENT_OPTIONAL_SKILLS_LEN]; /* 空格分隔 */

    char system_prompt[AGENT_SYSTEM_PROMPT_LEN];
    char toolset[AGENT_TOOLSET_LEN];                 /* 空格分隔 */

    /* 模型配置 */
    char model_provider[AGENT_MODEL_PROVIDER_LEN];
    char model_name[AGENT_MODEL_NAME_LEN];
    int context_limit;
    int max_tokens;
    float temperature;

    /* 来源追溯 */
    char source_transcript_refs[AGENT_SOURCE_TXN_REFS_LEN]; /* 逗号分隔 */

    /* 生命周期 */
    char lifecycle_status[AGENT_LIFECYCLE_STATUS_LEN]; /* "active" / "retired" */
    time_t retired_at;
    char retired_reason[AGENT_RETIRE_REASON_LEN];
    char handoff_to_agent_id[AGENT_HANDOFF_TO_LEN];

    /* 元数据 */
    char created_by[16];          /* "hr" / "manual" */
    time_t created_at;
    time_t updated_at;
    int version;
    float distillation_confidence;
} agent_definition_t;

/** 匹配结果。 */
typedef struct {
    char agent_id[AGENT_ID_LEN];
    char agent_name[AGENT_NAME_LEN];
    float score;
    int matched_skill_count;
    int total_requested_skills;
    float coverage_ratio;
    bool fallback_recommended;
    char fallback_reason[128];
} agent_match_result_t;

/* ---- Registry 操作 ---- */

/** 初始化 Registry（创建数据目录、加载已有 Agent）。 */
err_t agent_registry_init(void);

/** 注册一个新的 Specialist Agent。 */
err_t agent_registry_register(const agent_definition_t *def);

/** 按技能标签匹配 Agent，返回排序结果。 */
err_t agent_registry_find_matches(const char *capability_tags,
                                  float match_threshold,
                                  agent_match_result_t *out_matches,
                                  int out_capacity,
                                  int *out_count);

/** 按 agent_id 获取 Agent 定义。 */
err_t agent_registry_get(const char *agent_id, agent_definition_t *out_def);

/** 列出所有 active（或全部）Agent。 */
err_t agent_registry_list(bool include_retired,
                          agent_definition_t *out_defs,
                          int out_capacity,
                          int *out_count);

/** 更新 Agent 定义。 */
err_t agent_registry_update(const char *agent_id, const agent_definition_t *def);

/** 退休 Agent（标记 lifecycle_status="retired"）。 */
err_t agent_registry_retire(const char *agent_id, const char *reason);

