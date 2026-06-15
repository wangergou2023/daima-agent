#include "paths.h"
#include "cJSON.h"
#include "drivers/tool/tool_work_item.h"
#include "work_item.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void mkdir_p(const char *path)
{
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0777);
            *p = '/';
        }
    }
    mkdir(tmp, 0777);
}

static void run_tool(const char *input, char *output, size_t output_size)
{
    memset(output, 0, output_size);
    assert(tool_work_item_execute(input, output, output_size) == 0);
}

static void read_first_line(char *buf, size_t size)
{
    FILE *f = fopen(path_work_items_file(), "r");
    assert(f);
    assert(fgets(buf, (int)size, f));
    fclose(f);
}

static int count_valid_work_items(void)
{
    FILE *f = fopen(path_work_items_file(), "r");
    assert(f);
    int count = 0;
    char buf[16384];
    while (fgets(buf, sizeof(buf), f)) {
        cJSON *item = cJSON_Parse(buf);
        if (item && cJSON_IsObject(item)) {
            count++;
        }
        cJSON_Delete(item);
    }
    fclose(f);
    return count;
}

static void collect_tool_failure_once(const char *signature)
{
    cJSON *input = cJSON_CreateObject();
    assert(input);
    cJSON_AddStringToObject(input, "type", "defect");
    cJSON_AddStringToObject(input, "source", "log");
    cJSON_AddStringToObject(input, "title", "apply_patch 连续收到空参数导致工具调用失败");
    cJSON_AddStringToObject(input, "description", "apply_patch 收到 input={}，工具返回缺少 patch 字段。");
    cJSON_AddStringToObject(input, "expected", "apply_patch 调用必须提供 patch。");
    cJSON_AddStringToObject(input, "actual", "apply_patch 收到空参数。");
    cJSON_AddStringToObject(input, "status", "triaged");
    cJSON_AddStringToObject(input, "priority", "P1");
    cJSON_AddStringToObject(input, "error_signature", signature);

    cJSON *evidence = cJSON_CreateObject();
    cJSON_AddStringToObject(evidence, "session_id", "web_test");
    cJSON *logs = cJSON_CreateArray();
    cJSON_AddItemToArray(logs, cJSON_CreateString("Tool apply_patch failed: ERR_INVALID_ARG input={}"));
    cJSON_AddItemToObject(evidence, "logs", logs);
    cJSON *tool_calls = cJSON_CreateArray();
    cJSON *call = cJSON_CreateObject();
    cJSON_AddStringToObject(call, "tool", "apply_patch");
    cJSON_AddStringToObject(call, "input", "{}");
    cJSON_AddStringToObject(call, "error", "ERR_INVALID_ARG");
    cJSON_AddStringToObject(call, "output", "错误：缺少 'patch' 字段");
    cJSON_AddItemToArray(tool_calls, call);
    cJSON_AddItemToObject(evidence, "tool_calls", tool_calls);
    cJSON_AddItemToObject(input, "evidence", evidence);

    cJSON *item = NULL;
    assert(work_item_store_collect_structured(input, &item) == 0);
    cJSON_Delete(item);
    cJSON_Delete(input);
}

int main(void)
{
    char home[512];
    snprintf(home, sizeof(home), "/tmp/agent-work-item-test-%ld", (long)getpid());
    setenv("AGENT_HOME", home, 1);
    char memory_dir[1024];
    snprintf(memory_dir, sizeof(memory_dir), "%s/spiffs_data/memory", home);
    mkdir_p(memory_dir);
    paths_init();

    char out[8192];
    run_tool("{\"action\":\"add\",\"type\":\"defect\",\"title\":\"刷新后历史丢失\",\"description\":\"用户反馈刷新后消息偶发丢失\",\"expected\":\"刷新后保留历史\",\"actual\":\"历史偶发不可见\",\"status\":\"needs_info\",\"priority\":\"P1\"}",
             out, sizeof(out));
    assert(strstr(out, "已创建 work item"));
    assert(strstr(out, "WI-"));
    assert(strstr(out, "P1 defect needs_info"));

    run_tool("{\"action\":\"add\",\"type\":\"missing\",\"title\":\"缺少 review 队列\",\"status\":\"accepted\",\"priority\":\"P2\"}",
             out, sizeof(out));
    assert(strstr(out, "002"));

    run_tool("{\"action\":\"list\",\"status\":\"needs_info\"}", out, sizeof(out));
    assert(strstr(out, "刷新后历史丢失"));
    assert(!strstr(out, "缺少 review 队列"));

    /* 去重测试：同标题+同类型，第二次 add 应标记 duplicate_of */
    run_tool("{\"action\":\"add\",\"type\":\"defect\",\"title\":\"刷新后历史丢失\",\"description\":\"再次反馈\"}",
             out, sizeof(out));
    assert(strstr(out, "疑似重复"));
    assert(strstr(out, "WI-"));

    /* review 队列测试 */
    run_tool("{\"action\":\"review\"}", out, sizeof(out));
    assert(strstr(out, "待审核 Work Items"));
    assert(strstr(out, "needs_info"));
    assert(strstr(out, "操作："));

    /* review 批量审核：将 needs_info 的两条标记为 accepted */
    char review_batch[1024];
    snprintf(review_batch, sizeof(review_batch),
             "{\"action\":\"review\",\"ids\":[\"%s\",\"%s\"],\"status\":\"accepted\"}",
             "WI-20260605-001", "WI-20260605-003");
    run_tool(review_batch, out, sizeof(out));
    assert(strstr(out, "已审核"));

    /* 审核后这些项不应再出现在 review 队列 */
    run_tool("{\"action\":\"review\"}", out, sizeof(out));
    assert(!strstr(out, "WI-20260605-001"));
    assert(!strstr(out, "WI-20260605-003"));

    /* review 单项操作 */
    run_tool("{\"action\":\"review\",\"id\":\"WI-20260605-002\",\"status\":\"rejected\"}",
             out, sizeof(out));
    assert(strstr(out, "已审核"));

    char line[8192];
    read_first_line(line, sizeof(line));
    cJSON *item = cJSON_Parse(line);
    assert(item);
    const char *id = cJSON_GetStringValue(cJSON_GetObjectItem(item, "id"));
    assert(id);
    char update_input[512];
    snprintf(update_input, sizeof(update_input),
             "{\"action\":\"update\",\"id\":\"%s\",\"status\":\"planned\",\"priority\":\"P0\"}", id);
    cJSON_Delete(item);
    run_tool(update_input, out, sizeof(out));
    assert(strstr(out, "P0 defect planned"));

    FILE *f = fopen(path_work_items_file(), "a");
    assert(f);
    fprintf(f, "{bad-json\n");
    fclose(f);

    run_tool("{\"action\":\"summary\"}", out, sizeof(out));
    assert(strstr(out, "跳过 1 条无效记录"));
    assert(strstr(out, "高优先级事项"));
    assert(strstr(out, "可进入实现事项"));

    assert(tool_work_item_execute("{\"action\":\"add\",\"type\":\"bad\",\"title\":\"bad\"}", out, sizeof(out)) != 0);

    int before_structured = count_valid_work_items();
    const char *sig = "tool:apply_patch|err:ERR_INVALID_ARG|output:empty_input";
    collect_tool_failure_once(sig);
    assert(count_valid_work_items() == before_structured + 1);
    collect_tool_failure_once(sig);
    assert(count_valid_work_items() == before_structured + 1);

    run_tool("{\"action\":\"list\",\"status\":\"triaged\"}", out, sizeof(out));
    assert(strstr(out, "apply_patch 连续收到空参数"));

    FILE *wf = fopen(path_work_items_file(), "r");
    assert(wf);
    bool saw_signature = false;
    bool saw_occurrences = false;
    char item_line[16384];
    while (fgets(item_line, sizeof(item_line), wf)) {
        cJSON *parsed = cJSON_Parse(item_line);
        if (!parsed) continue;
        const char *parsed_sig = cJSON_GetStringValue(cJSON_GetObjectItem(parsed, "error_signature"));
        if (parsed_sig && strcmp(parsed_sig, sig) == 0) {
            saw_signature = true;
            cJSON *occ = cJSON_GetObjectItem(parsed, "occurrences");
            saw_occurrences = cJSON_IsNumber(occ) && occ->valueint == 2;
        }
        cJSON_Delete(parsed);
    }
    fclose(wf);
    assert(saw_signature);
    assert(saw_occurrences);
    return 0;
}
