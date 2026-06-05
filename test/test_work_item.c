#include "app/daima_paths.h"
#include "cJSON.h"
#include "tools/tool_work_item.h"

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
    FILE *f = fopen(daima_path_work_items_file(), "r");
    assert(f);
    assert(fgets(buf, (int)size, f));
    fclose(f);
}

int main(void)
{
    char home[512];
    snprintf(home, sizeof(home), "/tmp/daima-work-item-test-%ld", (long)getpid());
    setenv("DAIMA_HOME", home, 1);
    char memory_dir[1024];
    snprintf(memory_dir, sizeof(memory_dir), "%s/spiffs_data/memory", home);
    mkdir_p(memory_dir);
    daima_paths_init();

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

    FILE *f = fopen(daima_path_work_items_file(), "a");
    assert(f);
    fprintf(f, "{bad-json\n");
    fclose(f);

    run_tool("{\"action\":\"summary\"}", out, sizeof(out));
    assert(strstr(out, "跳过 1 条无效记录"));
    assert(strstr(out, "高优先级事项"));
    assert(strstr(out, "可进入实现事项"));

    assert(tool_work_item_execute("{\"action\":\"add\",\"type\":\"bad\",\"title\":\"bad\"}", out, sizeof(out)) != 0);
    return 0;
}
