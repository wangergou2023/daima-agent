#include "hr_pipeline.h"

#include "hr_scan.h"
#include "hr_cluster.h"
#include "hr_distill.h"
#include "registry/registry.h"
#include "linux/printk.h"
#include "linux/slab.h"
#include "linux/kernel.h"
#include <string.h>
#include <time.h>
#include <ctype.h>

err_t hr_run_pipeline(bool auto_register, int *out_registered_count)
{
    if (out_registered_count)
        *out_registered_count = 0;

    pr_info("=== HR Pipeline: scan start ===");

    transcript_record_t *records = kmalloc(
        sizeof(transcript_record_t) * HR_SCAN_DEFAULT_LIMIT, GFP_KERNEL);
    if (!records)
        return ERR_NO_MEM;

    int record_count = 0;
    err_t err = hr_scan_transcripts(7, HR_SCAN_DEFAULT_LIMIT,
                                    records, HR_SCAN_DEFAULT_LIMIT,
                                    &record_count);
    if (err != 0 || record_count == 0) {
        kfree(records);
        pr_info("HR: no transcripts to scan");
        return 0;
    }

    task_cluster_t clusters[HR_CLUSTER_MAX];
    int cluster_count = 0;
    hr_cluster_transcripts(records, record_count, 2, 3, 0.8f,
                           clusters, HR_CLUSTER_MAX, &cluster_count);

    if (cluster_count == 0) {
        kfree(records);
        pr_info("HR: no clusters found");
        return 0;
    }

    pr_info("HR: %d cluster(s) from %d transcripts", cluster_count, record_count);

    char scan_id[32];
    snprintf(scan_id, sizeof(scan_id), "scan-%ld", (long)time(NULL));

    int registered = 0;
    for (int i = 0; i < cluster_count; i++) {
        agent_definition_t agent;
        err = hr_distill_agent(&clusters[i], scan_id, &agent);
        if (err != 0) {
            pr_warn("HR: distillation failed for cluster %s", clusters[i].cluster_id);
            continue;
        }

        /* 生成 agent_id：name 转小写、空格改连字符 */
        char agent_id[AGENT_ID_LEN];
        snprintf(agent_id, sizeof(agent_id), "agent-%.50s-%d", agent.name, i + 1);
        for (char *p = agent_id; *p; p++) {
            if (*p == ' ')
                *p = '-';
            else if (*p >= 'A' && *p <= 'Z')
                *p = (char)(*p + 32);
        }
        strscpy(agent.agent_id, agent_id, sizeof(agent.agent_id));

        agent.created_at = time(NULL);
        agent.updated_at = agent.created_at;
        agent.version = 1;

        if (auto_register) {
            err = agent_registry_register(&agent);
            if (err == 0) {
                registered++;
                pr_info("HR: registered %s (%s)", agent.agent_id, agent.name);
            } else {
                pr_warn("HR: registration failed for %s: %d", agent.name, err);
            }
        } else {
            pr_info("HR: candidate ready: %s (%s) - skills: %s",
                    agent.name, agent_id, agent.core_skills);
        }
    }

    kfree(records);
    if (out_registered_count)
        *out_registered_count = registered;
    pr_info("=== HR Pipeline: complete, %d agent(s) registered ===", registered);
    return 0;
}
