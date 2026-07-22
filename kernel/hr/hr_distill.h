/* HR 蒸馏：调用 LLM 从 Transcript 簇中蒸馏 AgentDefinition。 */
#pragma once

#include "err.h"
#include "hr_cluster.h"
#include "registry/registry.h"

err_t hr_distill_agent(const task_cluster_t *cluster,
                       const char *scan_id,
                       agent_definition_t *out_agent);
