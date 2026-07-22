/* Transcript 结构化记录：每次 Agent 执行的结果摘要，供 HR 扫描蒸馏。 */
#pragma once

#include "err.h"
#include <stdbool.h>
#include <stddef.h>
#include <time.h>

#define TRANSCRIPT_ID_LEN            32
#define TRANSCRIPT_USER_INPUT_LEN    1024
#define TRANSCRIPT_SKILL_TAGS_LEN    256
#define TRANSCRIPT_TOOLS_USED_LEN    256
#define TRANSCRIPT_SUMMARY_LEN       2048
#define TRANSCRIPT_OUTPUT_LEN        2048
#define TRANSCRIPT_ERROR_CODE_LEN    32
#define TRANSCRIPT_ERROR_MSG_LEN     256
#define TRANSCRIPT_EXECUTED_BY_LEN   16
#define TRANSCRIPT_AGENT_ID_LEN      64
#define TRANSCRIPT_DECISION_LEN      16
#define TRANSCRIPT_DECISION_REASON_LEN 256
#define TRANSCRIPT_ARTIFACTS_LEN     512

#define TRANSCRIPT_SKILL_TAGS_MAX    8
#define TRANSCRIPT_ARTIFACTS_MAX     8
#define TRANSCRIPT_QUERY_MAX         100

/** 单条执行 Transcript。 */
typedef struct {
    char record_id[TRANSCRIPT_ID_LEN];             /* txn-YYYYMMDD-NNN */
    char task_id[TRANSCRIPT_ID_LEN];                /* chat_id + seq */
    char chat_id[64];

    char user_input[TRANSCRIPT_USER_INPUT_LEN];     /* 用户原始输入 */

    /* 技能标签列表（空格分隔） */
    char skill_tags[TRANSCRIPT_SKILL_TAGS_LEN];     /* "frontend react forms" */

    /* 使用的工具列表（空格分隔） */
    char tools_used[TRANSCRIPT_TOOLS_USED_LEN];     /* "read_file write_file bash" */

    char execution_summary[TRANSCRIPT_SUMMARY_LEN]; /* 执行路径摘要 */
    char output[TRANSCRIPT_OUTPUT_LEN];             /* 主要输出 */

    /* 执行结果 */
    char result_status[8];                          /* "success" "failure" "partial" */

    /* 错误信息（失败时） */
    char error_code[TRANSCRIPT_ERROR_CODE_LEN];
    char error_msg[TRANSCRIPT_ERROR_MSG_LEN];

    /* 谁执行的 */
    char executed_by[TRANSCRIPT_EXECUTED_BY_LEN];   /* "boss" 或 "specialist" */
    char agent_id[TRANSCRIPT_AGENT_ID_LEN];         /* specialist 时为 agent_id */

    /* 指标 */
    int total_tokens;
    int model_calls;
    int tool_calls;
    int duration_ms;

    /* 路由决策 */
    char routing_decision[TRANSCRIPT_DECISION_LEN]; /* "delegated" 或 "fallback" */
    char match_agent_id[TRANSCRIPT_AGENT_ID_LEN];
    float match_score;
    char fallback_reason[TRANSCRIPT_DECISION_REASON_LEN];

    /* 产出物 */
    char artifacts[TRANSCRIPT_ARTIFACTS_LEN];       /* 逗号分隔的文件路径 */

    time_t timestamp;
} transcript_record_t;

/**
 * 将一条 Transcript 追加写入持久化存储。
 * 存储路径: ${memory_dir}/data/transcripts/<chat_slug>/<record_id>.json
 */
err_t transcript_append(const transcript_record_t *record);

/**
 * 查询 Transcript 记录，支持按时间范围、结果状态、技能标签过滤。
 * filter_skill_tags: 为 NULL 时不过滤技能，空格分隔多标签
 * filter_status: 为 NULL 时不过滤状态（"success"/"failure"/"partial"）
 * since_ts: 为 0 时不过滤时间
 * limit: 最大返回条数
 * out_records: 输出缓冲区
 * out_capacity: 输出缓冲区容量
 * out_count: 实际返回数量
 */
err_t transcript_query(const char *filter_skill_tags,
                       const char *filter_status,
                       time_t since_ts,
                       int limit,
                       transcript_record_t *out_records,
                       int out_capacity,
                       int *out_count);

/**
 * 初始化 Transcript 存储（创建目录）。
 */
err_t transcript_init(void);

/**
 * 从用户输入和输出文本中提取技能标签（基于关键词字典匹配）。
 * user_input: 用户原始消息
 * output_text: 助手输出文本（可为NULL），用于增强匹配
 * out_tags: 空格分隔的标签字符串
 * buf_size: out_tags 缓冲区大小（建议 >= TRANSCRIPT_SKILL_TAGS_LEN）
 */
err_t transcript_extract_skill_tags(const char *user_input,
                                    const char *output_text,
                                    char *out_tags,
                                    size_t buf_size);
