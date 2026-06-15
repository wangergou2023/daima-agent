/* 定时任务服务接口。 */

#pragma once

#include "err.h"
#include <stdbool.h>
#include <stdint.h>

/* 定时类型 */
typedef enum {
    CRON_KIND_EVERY  = 0,  /* 周期任务：间隔秒数 */
    CRON_KIND_AT     = 1,  /* 一次性任务：Unix 时间戳 */
    CRON_KIND_DAILY  = 2,  /* 按本地时钟每天/指定星期触发 */
    CRON_KIND_WEEKLY = 3,  /* 按本地时钟指定星期触发 */
} cron_kind_t;

/* 单个定时任务 */
typedef struct {
    char id[9];            /* 8 位十六进制 ID + 终止符 */
    char name[32];
    bool enabled;
    cron_kind_t kind;
    uint32_t interval_s;   /* 对于 EVERY：间隔秒数 */
    int64_t at_epoch;      /* 对于 AT：Unix 时间戳 */
    uint32_t time_of_day_s;/* 对于 DAILY/WEEKLY：本地当天秒数 */
    uint8_t weekdays;      /* bit0=周日 ... bit6=周六；0 表示每天 */
    char message[256];     /* 注入入站队列的消息 */
    char channel[16];      /* 回复通道（默认 "system"） */
    char chat_id[64];      /* 回复 chat_id（默认 "cron"） */
    int64_t last_run;      /* 上次运行时间戳 */
    int64_t next_run;      /* 下次运行时间戳 */
    bool delete_after_run; /* 执行后移除任务（仅 AT） */
} cron_job_t;

/**
 * 初始化定时任务服务。加载 SPIFFS 中的任务。
 */
err_t cron_service_init(void);

/**
 * 启动定时任务计时器。请在启动完成且时间已同步后调用。
 */
err_t cron_service_start(void);

/**
 * 停止定时任务计时器。
 */
void cron_service_stop(void);

/**
 * 添加新的定时任务。
 * @param job  任务结构体指针（会自动生成 id）
 * @return 成功返回 0，若任务数已满返回 ERR_NO_MEM
 */
err_t cron_add_job(cron_job_t *job);

/**
 * 按 ID 移除定时任务。
 * @param job_id  8 位任务 ID
 * @return 成功返回 0，未找到返回 ERR_NOT_FOUND
 */
err_t cron_remove_job(const char *job_id);

/**
 * 列出所有定时任务。
 * @param jobs      输出：任务指针数组
 * @param count     输出：任务数量
 */
void cron_list_jobs(const cron_job_t **jobs, int *count);
