/* HR 聚类：按技能标签重叠度对 Transcript 分组。 */
#pragma once

#include "err.h"
#include "transcript.h"

#define HR_CLUSTER_MAX              16
#define HR_CLUSTER_TXN_MAX          32
#define HR_CLUSTER_SHARED_SKILLS_LEN 256
#define HR_CLUSTER_ID_LEN           32

typedef struct {
    char cluster_id[HR_CLUSTER_ID_LEN];
    int transcript_count;
    char transcript_refs[HR_CLUSTER_TXN_MAX][TRANSCRIPT_ID_LEN];
    char shared_skills[HR_CLUSTER_SHARED_SKILLS_LEN];
    float success_rate;
    char representative_tasks[HR_CLUSTER_TXN_MAX][TRANSCRIPT_USER_INPUT_LEN];
} task_cluster_t;

err_t hr_cluster_transcripts(const transcript_record_t *records,
                             int record_count,
                             int min_overlap,
                             int min_cluster_size,
                             float min_success_rate,
                             task_cluster_t *out_clusters,
                             int out_capacity,
                             int *out_count);
