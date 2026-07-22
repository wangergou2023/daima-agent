#include "hr_distill.h"

#include "drivers/llm/llm_proxy.h"
#include "runtime.h"
#include "cjson.h"
#include "linux/printk.h"
#include "linux/slab.h"
#include "linux/kernel.h"
#include <stdio.h>
#include <string.h>

#define HR_DISTILL_PROMPT_BUF_SIZE (32 * 1024)
#define HR_DISTILL_MAX_RETRIES       3

/* 从 LLM 响应中提取 JSON，支持 markdown 代码块包裹 */
static char *extract_json_from_text(const char *text)
{
	const char *json_start;

	if (!text || !text[0])
		return NULL;

	/* 先尝试找到 ```json ... ``` 包裹的内容 */
	const char *fence = strstr(text, "```json");
	if (fence) {
		fence += 7;
		while (*fence == '\n' || *fence == '\r') fence++;
		const char *end = strstr(fence, "```");
		if (end && end > fence) {
			size_t len = (size_t)(end - fence);
			char *result = kmalloc(len + 1, GFP_KERNEL);
			if (result) {
				memcpy(result, fence, len);
				result[len] = '\0';
				return result;
			}
		}
	}

	/* 直接找 { */
	json_start = strchr(text, '{');
	if (json_start)
		return strdup(json_start);  /* 用 strdup，堆分配统一释放 */

	return NULL;
}

/* 校验蒸馏结果是否完整 */
static bool distill_result_valid(const agent_definition_t *agent)
{
	return agent->name[0] &&
	       agent->core_skills[0] &&
	       agent->system_prompt[0] &&
	       strlen(agent->system_prompt) > 50;  /* system_prompt 不能太短 */
}

err_t hr_distill_agent(const task_cluster_t *cluster,
                       const char *scan_id,
                       agent_definition_t *out_agent)
{
	(void)scan_id;
	if (!cluster || !out_agent)
		return ERR_INVALID_ARG;

	char *prompt = kmalloc(HR_DISTILL_PROMPT_BUF_SIZE, GFP_KERNEL);
	if (!prompt)
		return ERR_NO_MEM;

	char txn_text[16384];
	txn_text[0] = '\0';
	for (int i = 0; i < cluster->transcript_count && i < HR_CLUSTER_TXN_MAX; i++) {
		char line[TRANSCRIPT_USER_INPUT_LEN + 256];
		snprintf(line, sizeof(line),
		         "Task %d: \"%.900s\"\n",
		         i + 1, cluster->representative_tasks[i]);
		strncat(txn_text, line, sizeof(txn_text) - strlen(txn_text) - 1);
	}

	snprintf(prompt, HR_DISTILL_PROMPT_BUF_SIZE,
		"你是 HR Agent，负责从 Boss 的执行记录中蒸馏出新的 Specialist Agent。\n\n"
		"共享技能标签: %s\n"
		"样本数: %d\n"
		"成功率: %.0f%%\n\n"
		"Transcript 摘要:\n%s\n\n"
		"请基于上述执行记录，输出一个严格 JSON 格式的 Specialist Agent 定义：\n"
		"{\n"
		"  \"name\": \"Agent 名称（如 React Frontend Specialist）\",\n"
		"  \"description\": \"一两句描述\",\n"
		"  \"core_skills\": \"空格分隔的核心技能标签\",\n"
		"  \"optional_skills\": \"空格分隔的可选技能标签\",\n"
		"  \"system_prompt\": \"完整的 system prompt，继承 Boss 的工作风格\",\n"
		"  \"toolset\": \"空格分隔的工具名（如 read_file write_file edit_file bash）\",\n"
		"  \"representative_task_types\": [\"任务类型1\", \"任务类型2\"]\n"
		"}\n\n"
		"要求：\n"
		"- system_prompt 要能独立指导 Agent 完成此类任务\n"
		"- 继承 Boss 的高效、直接风格\n"
		"- 只输出 JSON，不要解释",
		cluster->shared_skills,
		cluster->transcript_count,
		(double)(cluster->success_rate * 100.0f),
		txn_text);

	err_t err = 0;
	bool distilled = false;

	for (int retry = 0; retry < HR_DISTILL_MAX_RETRIES && !distilled; retry++) {
		/* 构建请求（每次重试复用 prompt） */
		cJSON *req = cJSON_CreateArray();
		cJSON *user = cJSON_CreateObject();
		cJSON_AddStringToObject(user, "role", "user");

		if (retry == 0) {
			cJSON_AddStringToObject(user, "content", prompt);
		} else {
			/* 重试时追加修正提示 */
			char *retry_prompt = kmalloc(HR_DISTILL_PROMPT_BUF_SIZE, GFP_KERNEL);
			if (retry_prompt) {
				snprintf(retry_prompt, HR_DISTILL_PROMPT_BUF_SIZE,
					"%s\n\n【重要】上一次输出 JSON 格式无效或字段不完整。"
					"请确保输出严格合法 JSON，"
					"且 name、core_skills、system_prompt 三个字段非空。"
					"system_prompt 至少 100 字。只输出 JSON。", prompt);
				cJSON_AddStringToObject(user, "content", retry_prompt);
				kfree(retry_prompt);
			} else {
				cJSON_AddStringToObject(user, "content", prompt);
			}
		}
		cJSON_AddItemToArray(req, user);

		llm_response_t resp;
		memset(&resp, 0, sizeof(resp));
		err = llm_chat_tools(
			"你是 HR Agent，严格输出 JSON，不要调用工具，不要输出解释。",
			req, NULL, &resp);

		cJSON_Delete(req);

		if (err != 0 || !resp.text || !resp.text[0]) {
			llm_response_free(&resp);
			if (retry < HR_DISTILL_MAX_RETRIES - 1) {
				pr_warn("HR distill LLM call failed (retry %d/%d): %d",
					retry + 1, HR_DISTILL_MAX_RETRIES, err);
				continue;
			}
			kfree(prompt);
			return err != 0 ? err : ERR_FAIL;
		}

		char *json_text = extract_json_from_text(resp.text);
		if (!json_text) {
			llm_response_free(&resp);
			if (retry < HR_DISTILL_MAX_RETRIES - 1) {
				pr_warn("HR distill: no JSON found in response (retry %d/%d)",
					retry + 1, HR_DISTILL_MAX_RETRIES);
				continue;
			}
			kfree(prompt);
			return ERR_FAIL;
		}

		cJSON *result = cJSON_Parse(json_text);
		kfree(json_text);

		if (!result) {
			llm_response_free(&resp);
			if (retry < HR_DISTILL_MAX_RETRIES - 1) {
				pr_warn("HR distill: JSON parse failed (retry %d/%d)",
					retry + 1, HR_DISTILL_MAX_RETRIES);
				continue;
			}
			kfree(prompt);
			return ERR_FAIL;
		}

		memset(out_agent, 0, sizeof(*out_agent));

#define GET_S(field, key) \
		do { const char *_v = cJSON_GetStringValue(cJSON_GetObjectItem(result, key)); \
		     if (_v) strscpy(out_agent->field, _v, sizeof(out_agent->field)); } while (0)

		GET_S(name, "name");
		GET_S(description, "description");
		GET_S(core_skills, "core_skills");
		GET_S(optional_skills, "optional_skills");
		GET_S(system_prompt, "system_prompt");
		GET_S(toolset, "toolset");

#undef GET_S

		cJSON_Delete(result);
		llm_response_free(&resp);

		/* 校验结果完整性 */
		if (!distill_result_valid(out_agent)) {
			if (retry < HR_DISTILL_MAX_RETRIES - 1) {
				pr_warn("HR distill: incomplete result (retry %d/%d) name=%s skills=%s prompt_len=%zu",
					retry + 1, HR_DISTILL_MAX_RETRIES,
					out_agent->name[0] ? out_agent->name : "(empty)",
					out_agent->core_skills[0] ? out_agent->core_skills : "(empty)",
					strlen(out_agent->system_prompt));
				continue;
			}
			kfree(prompt);
			return ERR_FAIL;
		}

		distilled = true;
	}

	kfree(prompt);

	if (!distilled)
		return ERR_FAIL;

	/* 填充元数据 */
	strscpy(out_agent->origin, "distilled_from_boss", sizeof(out_agent->origin));
	strscpy(out_agent->model_provider, runtime_config_get_active_provider_name(),
	        sizeof(out_agent->model_provider));
	strscpy(out_agent->model_name, runtime_config_get_provider_model(),
	        sizeof(out_agent->model_name));
	out_agent->context_limit = runtime_config_get_context_limit_tokens();
	out_agent->max_tokens = runtime_config_get_max_output_tokens();
	out_agent->temperature = 0.3f;
	strscpy(out_agent->lifecycle_status, "active", sizeof(out_agent->lifecycle_status));
	out_agent->distillation_confidence = cluster->success_rate;

	char refs[AGENT_SOURCE_TXN_REFS_LEN];
	refs[0] = '\0';
	for (int i = 0; i < cluster->transcript_count; i++) {
		if (i > 0)
			strncat(refs, ",", sizeof(refs) - strlen(refs) - 1);
		strncat(refs, cluster->transcript_refs[i],
		        sizeof(refs) - strlen(refs) - 1);
	}
	strscpy(out_agent->source_transcript_refs, refs, sizeof(out_agent->source_transcript_refs));
	strscpy(out_agent->created_by, "hr", sizeof(out_agent->created_by));

	pr_info("HR distilled: %s (skills: %s, retries: %d)",
		out_agent->name, out_agent->core_skills,
		distilled ? 0 : HR_DISTILL_MAX_RETRIES);
	return 0;
}
