#include "hr_cluster.h"
#include "linux/printk.h"
#include "linux/slab.h"
#include "linux/kernel.h"
#include <string.h>

static int skill_overlap_count(const char *tags_a, const char *tags_b)
{
    if (!tags_a[0] || !tags_b[0])
        return 0;

    char buf_a[TRANSCRIPT_SKILL_TAGS_LEN];
    strscpy(buf_a, tags_a, sizeof(buf_a));

    int overlap = 0;
    char *saveptr = NULL;
    char *token = strtok_r(buf_a, " ", &saveptr);
    while (token) {
        if (strcasestr(tags_b, token))
            overlap++;
        token = strtok_r(NULL, " ", &saveptr);
    }
    return overlap;
}

static bool skill_tag_present(const char *all_tags, const char *token)
{
	/* 精确匹配：token 周围必须是空格、字符串边界或字符串开头/结尾 */
	const char *pos = all_tags;
	while ((pos = strcasestr(pos, token)) != NULL) {
		/* 检查前一个字符 */
		bool left_ok = (pos == all_tags) || (pos[-1] == ' ');
		/* 检查后一个字符 */
		size_t toklen = strlen(token);
		bool right_ok = (pos[toklen] == '\0') || (pos[toklen] == ' ');
		if (left_ok && right_ok)
			return true;
		pos++;
	}
	return false;
}

static void build_shared_skills(task_cluster_t *cluster,
                                const transcript_record_t *records,
                                const int *indices, int count,
                                int cluster_index)
{
	/* 收集所有技能标签并去重 */
	char all_tags[TRANSCRIPT_SKILL_TAGS_LEN * HR_CLUSTER_TXN_MAX];
	all_tags[0] = '\0';

	for (int i = 0; i < count; i++) {
		/* 逐个标签检查是否已存在 */
		char buf[TRANSCRIPT_SKILL_TAGS_LEN];
		strscpy(buf, records[indices[i]].skill_tags, sizeof(buf));
		char *saveptr = NULL;
		char *token = strtok_r(buf, " ", &saveptr);
		while (token) {
			/* 检查是否已在 all_tags 中（精确匹配） */
			if (!skill_tag_present(all_tags, token)) {
				size_t off = strlen(all_tags);
				if (off > 0 && off < sizeof(all_tags) - 1)
					all_tags[off++] = ' ';
				size_t remain = sizeof(all_tags) - off - 1;
				size_t toklen = strlen(token);
				if (toklen > remain)
					toklen = remain;
				memcpy(all_tags + off, token, toklen);
				all_tags[off + toklen] = '\0';
			}
			token = strtok_r(NULL, " ", &saveptr);
		}
	}
	strscpy(cluster->shared_skills, all_tags, sizeof(cluster->shared_skills));

	/* 找出最频繁的标签作为簇名 */
	char most_frequent_skill[64] = {0};
	int max_freq = 0;

	/* 对每个标签计数 */
	char count_buf[TRANSCRIPT_SKILL_TAGS_LEN * HR_CLUSTER_TXN_MAX];
	strscpy(count_buf, all_tags, sizeof(count_buf));
	char *saveptr2 = NULL;
	char *ctok = strtok_r(count_buf, " ", &saveptr2);
	while (ctok) {
		int freq = 0;
		for (int i = 0; i < count; i++) {
			if (strcasestr(records[indices[i]].skill_tags, ctok))
				freq++;
		}
		if (freq > max_freq) {
			max_freq = freq;
			strscpy(most_frequent_skill, ctok, sizeof(most_frequent_skill));
		}
		ctok = strtok_r(NULL, " ", &saveptr2);
	}

	if (most_frequent_skill[0]) {
		/* 首字母大写 */
		if (most_frequent_skill[0] >= 'a' && most_frequent_skill[0] <= 'z')
			most_frequent_skill[0] = (char)(most_frequent_skill[0] - 32);
		snprintf(cluster->cluster_id, sizeof(cluster->cluster_id),
		         "%s-cluster-%d", most_frequent_skill, cluster_index + 1);
	}
}

err_t hr_cluster_transcripts(const transcript_record_t *records,
                             int record_count,
                             int min_overlap,
                             int min_cluster_size,
                             float min_success_rate,
                             task_cluster_t *out_clusters,
                             int out_capacity,
                             int *out_count)
{
    *out_count = 0;
    if (record_count <= 0 || out_capacity <= 0 || !out_clusters)
        return 0;

    if (min_overlap <= 0)
        min_overlap = 2;
    if (min_cluster_size <= 0)
        min_cluster_size = 3;
    if (min_success_rate <= 0.0f)
        min_success_rate = 0.8f;

    bool assigned[HR_CLUSTER_TXN_MAX];
    memset(assigned, 0, sizeof(assigned));

    for (int i = 0; i < record_count && *out_count < out_capacity; i++) {
        if (assigned[i])
            continue;
        if (!records[i].skill_tags[0])
            continue;

        int cluster_indices[HR_CLUSTER_TXN_MAX];
        int cluster_size = 0;
        cluster_indices[cluster_size++] = i;
        assigned[i] = true;

        for (int j = i + 1; j < record_count && cluster_size < HR_CLUSTER_TXN_MAX; j++) {
            if (assigned[j])
                continue;
            if (skill_overlap_count(records[i].skill_tags, records[j].skill_tags) >= min_overlap) {
                cluster_indices[cluster_size++] = j;
                assigned[j] = true;
            }
        }

        if (cluster_size < min_cluster_size)
            continue;

        int success_count = 0;
        for (int k = 0; k < cluster_size; k++) {
            if (strcmp(records[cluster_indices[k]].result_status, "success") == 0)
                success_count++;
        }
        float rate = (float)success_count / (float)cluster_size;
        if (rate < min_success_rate)
            continue;

        task_cluster_t *cluster = &out_clusters[*out_count];
        memset(cluster, 0, sizeof(*cluster));
        snprintf(cluster->cluster_id, sizeof(cluster->cluster_id),
                 "cluster-%d", *out_count + 1);
        cluster->transcript_count = cluster_size;
        cluster->success_rate = rate;

        for (int k = 0; k < cluster_size; k++) {
            strscpy(cluster->transcript_refs[k],
                    records[cluster_indices[k]].record_id,
                    sizeof(cluster->transcript_refs[k]));
            strscpy(cluster->representative_tasks[k],
                    records[cluster_indices[k]].user_input,
                    sizeof(cluster->representative_tasks[k]));
        }

        build_shared_skills(cluster, records, cluster_indices, cluster_size, *out_count);
        (*out_count)++;
    }

    pr_info("HR cluster: %d clusters from %d records", *out_count, record_count);
    return 0;
}
