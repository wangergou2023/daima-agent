/* 简化版 todo 工具：维护一个本地 JSON 待办列表。 */



#include "drivers/tool/tool_todo.h"

#include "paths.h"

#include "autoconf.h"



#include <stdio.h>

#include <stdlib.h>

#include <string.h>

#include <stdbool.h>

#include <errno.h>

#include "cjson.h"

#include "linux/printk.h"

#include "linux/slab.h"

static const struct tool s_todo_tool = {

    .name = "todo",

    .description = "管理本地待办列表。支持 list、add、set、update、remove、clear，适合先列计划再执行。",

    .input_schema_json =

        "{\"type\":\"object\","

        "\"properties\":{"

        "\"action\":{\"type\":\"string\",\"description\":\"list / add / set / update / remove / clear，默认 list\"},"

        "\"id\":{\"type\":\"integer\",\"description\":\"update/remove 时使用的待办 ID\"},"

        "\"text\":{\"type\":\"string\",\"description\":\"add/update 时的待办内容\"},"

        "\"items\":{\"type\":\"array\",\"description\":\"set 时使用；可传字符串数组，或 {text, done} 对象数组\"},"

        "\"done\":{\"type\":\"boolean\",\"description\":\"update 时设置完成状态\"}"

        "},"

        "\"required\":[]}",

    .execute = tool_todo_execute,

};



static cJSON *todo_load_root(void)

{

    FILE *f = fopen(path_todo_file(), "r");

    if (!f) {

        cJSON *root = cJSON_CreateObject();

        cJSON_AddItemToObject(root, "items", cJSON_CreateArray());

        return root;

    }



    fseek(f, 0, SEEK_END);

    long size = ftell(f);

    fseek(f, 0, SEEK_SET);

    if (size < 0 || size > 128 * 1024) {

        fclose(f);

        return NULL;

    }



    char *buf = kzalloc((size_t)size + 1, GFP_KERNEL);

    if (!buf) {

        fclose(f);

        return NULL;

    }

    size_t n = fread(buf, 1, (size_t)size, f);

    fclose(f);

    buf[n] = '\0';



    cJSON *root = cJSON_Parse(buf);

    kfree(buf);

    if (!root || !cJSON_IsObject(root)) {

        cJSON_Delete(root);

        return NULL;

    }



    cJSON *items = cJSON_GetObjectItem(root, "items");

    if (!items || !cJSON_IsArray(items)) {

        cJSON_Delete(root);

        return NULL;

    }

    return root;

}



static err_t todo_save_root(cJSON *root)

{

    if (!root) return ERR_INVALID_ARG;



    char *json = cJSON_PrintUnformatted(root);

    if (!json) return ERR_NO_MEM;



    FILE *f = fopen(path_todo_file(), "w");

    if (!f) {

        kfree(json);

        return ERR_FAIL;

    }



    size_t len = strlen(json);

    size_t written = fwrite(json, 1, len, f);

    fclose(f);

    kfree(json);

    return written == len ? 0 : ERR_FAIL;

}



static cJSON *todo_items_array(cJSON *root)

{

    if (!root) return NULL;

    return cJSON_GetObjectItem(root, "items");

}



static int todo_next_id(cJSON *items)

{

    int max_id = 0;

    if (!items || !cJSON_IsArray(items)) return 1;



    cJSON *item = NULL;

    cJSON_ArrayForEach(item, items) {

        cJSON *id = cJSON_GetObjectItem(item, "id");

        if (id && cJSON_IsNumber(id) && id->valueint > max_id) {

            max_id = id->valueint;

        }

    }

    return max_id + 1;

}



static cJSON *todo_find_item(cJSON *items, int id, int *index_out)

{

    if (!items || !cJSON_IsArray(items)) return NULL;

    int index = 0;

    cJSON *item = NULL;

    cJSON_ArrayForEach(item, items) {

        cJSON *item_id = cJSON_GetObjectItem(item, "id");

        if (item_id && cJSON_IsNumber(item_id) && item_id->valueint == id) {

            if (index_out) *index_out = index;

            return item;

        }

        index++;

    }

    return NULL;

}



static bool todo_item_done_value(cJSON *done)

{

    if (!done) return false;

    if (cJSON_IsBool(done)) {

        return cJSON_IsTrue(done);

    }

    if (cJSON_IsNumber(done)) {

        return done->valueint != 0;

    }

    return false;

}



static cJSON *todo_build_item(int id, const char *text, bool done)

{

    if (!text || !text[0]) {

        return NULL;

    }



    cJSON *item = cJSON_CreateObject();

    if (!item) {

        return NULL;

    }

    cJSON_AddNumberToObject(item, "id", id);

    cJSON_AddStringToObject(item, "text", text);

    cJSON_AddBoolToObject(item, "done", done);

    return item;

}



static cJSON *todo_build_items_from_input(cJSON *input_items)

{

    if (!input_items || !cJSON_IsArray(input_items)) {

        return NULL;

    }



    cJSON *new_items = cJSON_CreateArray();

    if (!new_items) {

        return NULL;

    }



    int next_id = 1;

    cJSON *entry = NULL;

    cJSON_ArrayForEach(entry, input_items) {

        const char *text = NULL;

        bool done = false;



        if (cJSON_IsString(entry) && entry->valuestring && entry->valuestring[0]) {

            text = entry->valuestring;

        } else if (cJSON_IsObject(entry)) {

            cJSON *text_item = cJSON_GetObjectItem(entry, "text");

            if (text_item && cJSON_IsString(text_item) && text_item->valuestring && text_item->valuestring[0]) {

                text = text_item->valuestring;

                done = todo_item_done_value(cJSON_GetObjectItem(entry, "done"));

            }

        }



        if (!text) {

            cJSON_Delete(new_items);

            return NULL;

        }



        cJSON *item = todo_build_item(next_id++, text, done);

        if (!item) {

            cJSON_Delete(new_items);

            return NULL;

        }

        cJSON_AddItemToArray(new_items, item);

    }



    return new_items;

}



static void todo_append_item_line(char *output, size_t output_size, size_t *off, cJSON *item)

{

    if (!output || !off || !item) return;

    cJSON *id = cJSON_GetObjectItem(item, "id");

    cJSON *text = cJSON_GetObjectItem(item, "text");

    cJSON *done = cJSON_GetObjectItem(item, "done");

    if (!id || !cJSON_IsNumber(id) || !text || !cJSON_IsString(text)) return;



    const char *mark = todo_item_done_value(done) ? "x" : " ";

    *off += snprintf(output + *off, output_size - *off,

                     "- [%s] %d. %s\n", mark, id->valueint, text->valuestring);

}



static void todo_render(cJSON *root, char *output, size_t output_size)

{

    cJSON *items = todo_items_array(root);

    int total = items ? cJSON_GetArraySize(items) : 0;

    int done_count = 0;

    size_t off = snprintf(output, output_size, "TODO (%d items)\n", total);



    if (items) {

        cJSON *item = NULL;

        cJSON_ArrayForEach(item, items) {

            cJSON *done = cJSON_GetObjectItem(item, "done");

            if (todo_item_done_value(done)) {

                done_count++;

            }

            todo_append_item_line(output, output_size, &off, item);

        }

    }



    if (total == 0) {

        snprintf(output + off, output_size - off, "（当前没有待办）\n");

    } else {

        snprintf(output + off, output_size - off,

                 "\nSummary: %d total, %d done, %d active\n",

                 total, done_count, total - done_count);

    }

}



err_t tool_todo_execute(const char *input_json, char *output, size_t output_size)

{

    cJSON *input = cJSON_Parse(input_json);

    if (!input || !cJSON_IsObject(input)) {

        snprintf(output, output_size, "错误：输入 JSON 无效");

        cJSON_Delete(input);

        return ERR_INVALID_ARG;

    }



    const char *action = cJSON_GetStringValue(cJSON_GetObjectItem(input, "action"));

    if (!action || !action[0]) {

        action = "list";

    }



    cJSON *root = todo_load_root();

    if (!root) {

        snprintf(output, output_size, "错误：无法读取或解析 %s", path_todo_file());

        cJSON_Delete(input);

        return ERR_FAIL;

    }



    cJSON *items = todo_items_array(root);

    err_t err = 0;



    if (strcmp(action, "list") == 0) {

        todo_render(root, output, output_size);

    } else if (strcmp(action, "add") == 0) {

        const char *text = cJSON_GetStringValue(cJSON_GetObjectItem(input, "text"));

        if (!text || !text[0]) {

            snprintf(output, output_size, "错误：add 动作需要非空 text");

            err = ERR_INVALID_ARG;

        } else {

            cJSON *item = todo_build_item(todo_next_id(items), text, false);

            if (!item) {

                snprintf(output, output_size, "错误：创建待办项失败");

                err = ERR_NO_MEM;

                goto todo_cleanup;

            }

            cJSON_AddItemToArray(items, item);

            err = todo_save_root(root);

            if (err == 0) {

                todo_render(root, output, output_size);

            } else {

                snprintf(output, output_size, "错误：保存待办列表失败");

            }

        }

    } else if (strcmp(action, "set") == 0) {

        cJSON *input_items = cJSON_GetObjectItem(input, "items");

        cJSON *new_items = todo_build_items_from_input(input_items);

        if (!new_items) {

            snprintf(output, output_size,

                     "错误：set 动作需要 items 数组，元素可为字符串或 {text, done}");

            err = ERR_INVALID_ARG;

        } else {

            cJSON_ReplaceItemInObject(root, "items", new_items);

            items = todo_items_array(root);

            err = todo_save_root(root);

            if (err == 0) {

                todo_render(root, output, output_size);

            } else {

                snprintf(output, output_size, "错误：保存待办列表失败");

            }

        }

    } else if (strcmp(action, "update") == 0) {

        cJSON *id = cJSON_GetObjectItem(input, "id");

        if (!id || !cJSON_IsNumber(id)) {

            snprintf(output, output_size, "错误：update 动作需要 id");

            err = ERR_INVALID_ARG;

        } else {

            cJSON *item = todo_find_item(items, id->valueint, NULL);

            if (!item) {

                snprintf(output, output_size, "错误：未找到 id=%d 的待办", id->valueint);

                err = ERR_NOT_FOUND;

            } else {

                cJSON *text = cJSON_GetObjectItem(input, "text");

                cJSON *done = cJSON_GetObjectItem(input, "done");

                if (text && cJSON_IsString(text)) {

                    cJSON_ReplaceItemInObject(item, "text", cJSON_CreateString(text->valuestring));

                }

                if (done && (cJSON_IsBool(done) || cJSON_IsNumber(done))) {

                    cJSON_ReplaceItemInObject(item, "done", cJSON_CreateBool(todo_item_done_value(done)));

                }

                err = todo_save_root(root);

                if (err == 0) {

                    todo_render(root, output, output_size);

                } else {

                    snprintf(output, output_size, "错误：保存待办列表失败");

                }

            }

        }

    } else if (strcmp(action, "remove") == 0) {

        cJSON *id = cJSON_GetObjectItem(input, "id");

        if (!id || !cJSON_IsNumber(id)) {

            snprintf(output, output_size, "错误：remove 动作需要 id");

            err = ERR_INVALID_ARG;

        } else {

            int index = -1;

            cJSON *item = todo_find_item(items, id->valueint, &index);

            if (!item || index < 0) {

                snprintf(output, output_size, "错误：未找到 id=%d 的待办", id->valueint);

                err = ERR_NOT_FOUND;

            } else {

                cJSON_DeleteItemFromArray(items, index);

                err = todo_save_root(root);

                if (err == 0) {

                    todo_render(root, output, output_size);

                } else {

                    snprintf(output, output_size, "错误：保存待办列表失败");

                }

            }

        }

    } else if (strcmp(action, "clear") == 0) {

        cJSON_ReplaceItemInObject(root, "items", cJSON_CreateArray());

        err = todo_save_root(root);

        if (err == 0) {

            todo_render(root, output, output_size);

        } else {

            snprintf(output, output_size, "错误：清空待办列表失败");

        }

    } else {

        snprintf(output, output_size, "错误：未知 action=%s，支持 list/add/set/update/remove/clear", action);

        err = ERR_INVALID_ARG;

    }



    if (err == 0) {

        pr_info("todo: action=%s", action);

    }



todo_cleanup:

    cJSON_Delete(root);

    cJSON_Delete(input);

    return err;

}



const struct tool *tool_todo_definition(void)

{

    return &s_todo_tool;

}


static int todo_tool_probe(struct device *dev)
{
    (void)dev;
    return 0;
}

static struct tool_device s_todo_device = {
    .name = "todo",
    .description = "管理本地待办列表。支持 list、add、set、update、remove、clear，适合先列计划再执行。",
    .input_schema_json = "{\"type\":\"object\"," "\"properties\":{" "\"action\":{\"type\":\"string\",\"description\":\"list / add / set / update / remove / clear，默认 list\"}," "\"id\":{\"type\":\"integer\",\"description\":\"update/remove 时使用的待办 ID\"}," "\"text\":{\"type\":\"string\",\"description\":\"add/update 时的待办内容\"}," "\"items\":{\"type\":\"array\",\"description\":\"set 时使用；可传字符串数组，或 {text, done} 对象数组\"}," "\"done\":{\"type\":\"boolean\",\"description\":\"update 时设置完成状态\"}" "}," "\"required\":[]}",
};

static struct tool_driver s_todo_driver = {
    .drv.name = "todo",
    .drv.probe = todo_tool_probe,
    .execute = tool_todo_execute,
};

const struct tool_device *tool_todo_device(void)
{
    return &s_todo_device;
}

const struct tool_driver *tool_todo_driver(void)
{
    return &s_todo_driver;
}
