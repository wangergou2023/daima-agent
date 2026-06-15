#include "drivers/tool/tool_weather.h"
#include "http.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "cjson.h"
#include "linux/printk.h"
#include "linux/kernel.h"
#define WEATHER_API_TIMEOUT_MS  6000
#define WEATHER_API_URL         "https://uapis.cn/api/v1/misc/weather"

static const struct tool s_weather_tool = {
    .name = "weather",
    .description = "查询当前天气、未来 1-7 天预报或逐小时天气（无需 API Key）。可传 location 或 adcode；若都不传，则按调用方 IP 自动定位。",
    .input_schema_json =
        "{\"type\":\"object\","
        "\"properties\":{"
        "\"location\":{\"type\":\"string\",\"description\":\"城市/地区名称，支持中文或英文，如 北京 / Tokyo；可选\"},"
        "\"adcode\":{\"type\":\"string\",\"description\":\"可选行政区编码，如 110000；若提供则优先按 adcode 查询\"},"
        "\"type\":{\"type\":\"string\",\"description\":\"current、forecast 或 hourly；默认 current\"},"
        "\"days\":{\"type\":\"integer\",\"description\":\"预报天数（1-7，仅 forecast 时有效，可选）\"}"
        "},"
        "\"required\":[]}",
    .execute = tool_weather_execute,
};

static size_t url_encode_query(const char *src, char *dst, size_t dst_size)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t pos = 0;

    if (!dst || dst_size == 0) return 0;
    if (!src) {
        dst[0] = '\0';
        return 0;
    }

    for (; *src && pos < dst_size - 4; src++) {
        unsigned char c = (unsigned char)*src;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            dst[pos++] = (char)c;
        } else if (c == ' ') {
            dst[pos++] = '%';
            dst[pos++] = '2';
            dst[pos++] = '0';
        } else {
            dst[pos++] = '%';
            dst[pos++] = hex[c >> 4];
            dst[pos++] = hex[c & 0x0F];
        }
    }

    dst[pos] = '\0';
    return pos;
}

static const char *json_get_string(cJSON *obj, const char *key)
{
    if (!obj || !key) return NULL;
    cJSON *item = cJSON_GetObjectItem(obj, key);
    if (item && cJSON_IsString(item)) return item->valuestring;
    return NULL;
}

static double json_get_number(cJSON *obj, const char *key, double fallback)
{
    if (!obj || !key) return fallback;
    cJSON *item = cJSON_GetObjectItem(obj, key);
    if (item && cJSON_IsNumber(item)) return item->valuedouble;
    return fallback;
}

static bool appendf(char *buf, size_t size, size_t *off, const char *fmt, ...)
{
    if (!buf || !off || *off >= size) return false;

    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf + *off, size - *off, fmt, ap);
    va_end(ap);
    if (n < 0) return false;

    size_t written = (size_t)n;
    if (written >= size - *off) {
        *off = size - 1;
        buf[size - 1] = '\0';
        return false;
    }

    *off += written;
    return true;
}

static err_t fetch_weather_json(const char *location, const char *adcode,
                                     bool want_forecast, bool want_hourly,
                                     cJSON **root_out, char *output, size_t output_size)
{
    char url[768];
    size_t off = 0;
    off += strscpy(url + off, WEATHER_API_URL, sizeof(url) - off);

    bool has_query = false;
    if (location && location[0]) {
        char encoded[256];
        url_encode_query(location, encoded, sizeof(encoded));
        off += snprintf(url + off, sizeof(url) - off, "%ccity=%s", has_query ? '&' : '?', encoded);
        has_query = true;
    }
    if (adcode && adcode[0]) {
        off += snprintf(url + off, sizeof(url) - off, "%cadcode=%s", has_query ? '&' : '?', adcode);
        has_query = true;
    }
    if (want_forecast) {
        off += snprintf(url + off, sizeof(url) - off, "%cforecast=true", has_query ? '&' : '?');
        has_query = true;
    }
    if (want_hourly) {
        off += snprintf(url + off, sizeof(url) - off, "%chourly=true", has_query ? '&' : '?');
    }

    host_http_response_t resp = {0};
    pr_info("UAPI weather request: %s", url);
    err_t err = host_http_request("GET", url, NULL, NULL, WEATHER_API_TIMEOUT_MS, &resp);
    if (err != 0) {
        host_http_response_free(&resp);
        snprintf(output, output_size, "错误：天气请求失败");
        return err;
    }

    if (resp.status != 200 || !resp.body || !resp.body[0]) {
        long status = resp.status;
        host_http_response_free(&resp);
        snprintf(output, output_size, "错误：天气服务返回状态码 %ld", status);
        return ERR_FAIL;
    }

    cJSON *root = cJSON_Parse(resp.body);
    host_http_response_free(&resp);
    if (!root || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        snprintf(output, output_size, "错误：天气服务返回了无效 JSON");
        return ERR_FAIL;
    }

    *root_out = root;
    return 0;
}

static void format_current_weather(cJSON *root, char *output, size_t output_size)
{
    const char *province = json_get_string(root, "province");
    const char *city = json_get_string(root, "city");
    const char *district = json_get_string(root, "district");
    const char *weather = json_get_string(root, "weather");
    const char *wind_direction = json_get_string(root, "wind_direction");
    const char *wind_power = json_get_string(root, "wind_power");
    const char *report_time = json_get_string(root, "report_time");
    double temperature = json_get_number(root, "temperature", 0);
    double humidity = json_get_number(root, "humidity", -1);
    double temp_max = json_get_number(root, "temp_max", 0);
    double temp_min = json_get_number(root, "temp_min", 0);

    size_t off = 0;
    const char *place = city && city[0] ? city : (province && province[0] ? province : "当前位置");
    appendf(output, output_size, &off, "%s", place);
    if (district && district[0] && (!city || strcmp(district, city) != 0)) {
        appendf(output, output_size, &off, " %s", district);
    }
    appendf(output, output_size, &off, "：%s，%.1f°C", weather ? weather : "天气未知", temperature);
    if (humidity >= 0) {
        appendf(output, output_size, &off, "，湿度 %.0f%%", humidity);
    }
    if (wind_direction && wind_power) {
        appendf(output, output_size, &off, "，%s %s", wind_direction, wind_power);
    }
    if (cJSON_GetObjectItem(root, "temp_max") && cJSON_GetObjectItem(root, "temp_min")) {
        appendf(output, output_size, &off, "，最高 %.0f°C / 最低 %.0f°C", temp_max, temp_min);
    }
    if (report_time && report_time[0]) {
        appendf(output, output_size, &off, "（%s）", report_time);
    }
}

static void format_forecast_weather(cJSON *root, int days, char *output, size_t output_size)
{
    size_t off = 0;
    format_current_weather(root, output, output_size);
    off = strlen(output);

    cJSON *forecast = cJSON_GetObjectItem(root, "forecast");
    if (!forecast || !cJSON_IsArray(forecast) || cJSON_GetArraySize(forecast) <= 0) {
        appendf(output, output_size, &off, "\n未来预报：暂无数据。");
        return;
    }

    int total = cJSON_GetArraySize(forecast);
    if (days <= 0 || days > total) days = total;
    appendf(output, output_size, &off, "\n未来 %d 天：", days);

    for (int i = 0; i < days; i++) {
        cJSON *item = cJSON_GetArrayItem(forecast, i);
        if (!item) continue;

        const char *date = json_get_string(item, "date");
        const char *week = json_get_string(item, "week");
        const char *weather_day = json_get_string(item, "weather_day");
        const char *weather_night = json_get_string(item, "weather_night");
        const char *wind_day = json_get_string(item, "wind_dir_day");
        const char *wind_scale_day = json_get_string(item, "wind_scale_day");
        double max_t = json_get_number(item, "temp_max", 0);
        double min_t = json_get_number(item, "temp_min", 0);
        double pop = json_get_number(item, "pop", -1);

        appendf(output, output_size, &off,
                "\n- %s %s：白天%s，夜间%s，%.0f~%.0f°C",
                date ? date : "--",
                week ? week : "",
                weather_day ? weather_day : "未知",
                weather_night ? weather_night : "未知",
                min_t, max_t);
        if (wind_day && wind_scale_day) {
            appendf(output, output_size, &off, "，%s %s", wind_day, wind_scale_day);
        }
        if (pop >= 0) {
            appendf(output, output_size, &off, "，降水概率 %.0f%%", pop);
        }
    }
}

static void format_hourly_weather(cJSON *root, char *output, size_t output_size)
{
    size_t off = 0;
    format_current_weather(root, output, output_size);
    off = strlen(output);

    cJSON *hourly = cJSON_GetObjectItem(root, "hourly_forecast");
    if (!hourly || !cJSON_IsArray(hourly) || cJSON_GetArraySize(hourly) <= 0) {
        appendf(output, output_size, &off, "\n逐小时预报：暂无数据。");
        return;
    }

    int total = cJSON_GetArraySize(hourly);
    if (total > 6) total = 6;
    appendf(output, output_size, &off, "\n未来几小时：");

    for (int i = 0; i < total; i++) {
        cJSON *item = cJSON_GetArrayItem(hourly, i);
        if (!item) continue;
        const char *time_str = json_get_string(item, "time");
        const char *weather = json_get_string(item, "weather");
        const char *wind_direction = json_get_string(item, "wind_direction");
        const char *wind_scale = json_get_string(item, "wind_scale");
        double temperature = json_get_number(item, "temperature", 0);
        double pop = json_get_number(item, "pop", -1);

        appendf(output, output_size, &off,
                "\n- %s：%s，%.0f°C",
                time_str ? time_str : "--",
                weather ? weather : "未知",
                temperature);
        if (wind_direction && wind_scale) {
            appendf(output, output_size, &off, "，%s %s", wind_direction, wind_scale);
        }
        if (pop >= 0) {
            appendf(output, output_size, &off, "，降水概率 %.0f%%", pop);
        }
    }
}

static void log_output_snippet(const char *output)
{
    if (!output) return;
    size_t len = strlen(output);
    size_t n = len > 240 ? 240 : len;
    char tmp[256];
    if (n > 0) memcpy(tmp, output, n);
    tmp[n] = '\0';
    pr_info("weather output (%d bytes): %s", (int)len, tmp);
}

err_t tool_weather_init(void)
{
    pr_info("Weather initialized (UAPI weather, no API key required)");
    return 0;
}

err_t tool_weather_execute(const char *input_json, char *output, size_t output_size)
{
    cJSON *input = cJSON_Parse(input_json);
    if (!input) {
        snprintf(output, output_size, "错误：输入 JSON 无效");
        return ERR_INVALID_ARG;
    }

    const char *location = json_get_string(input, "location");
    const char *adcode = json_get_string(input, "adcode");
    const char *type = json_get_string(input, "type");
    int days = 0;
    cJSON *days_j = cJSON_GetObjectItem(input, "days");
    if (days_j && cJSON_IsNumber(days_j)) {
        days = (int)days_j->valuedouble;
    }

    bool want_forecast = (type && strcmp(type, "forecast") == 0);
    bool want_hourly = (type && strcmp(type, "hourly") == 0);
    if (days > 0) {
        want_forecast = true;
    }
    if (days <= 0) days = 3;
    if (days > 7) days = 7;

    cJSON *root = NULL;
    err_t err = fetch_weather_json(
        location,
        adcode,
        want_forecast,
        want_hourly,
        &root,
        output,
        output_size);
    if (err != 0) {
        cJSON_Delete(input);
        pr_warn("UAPI weather failed: %s", output);
        return err;
    }

    if (want_hourly) {
        format_hourly_weather(root, output, output_size);
    } else if (want_forecast) {
        format_forecast_weather(root, days, output, output_size);
    } else {
        format_current_weather(root, output, output_size);
    }

    log_output_snippet(output);
    cJSON_Delete(root);
    cJSON_Delete(input);
    return 0;
}

static int weather_probe(struct device *dev)
{
    (void)dev;
    return 0;
}

static struct tool_device s_weather_device = {
    .name = "weather",
    .description = "查询当前天气、未来 1-7 天预报或逐小时天气（无需 API Key）。可传 location 或 adcode；若都不传，则按调用方 IP 自动定位。",
    .input_schema_json =
        "{\"type\":\"object\","
        "\"properties\":{"
        "\"location\":{\"type\":\"string\",\"description\":\"城市/地区名称，支持中文或英文，如 北京 / Tokyo；可选\"},"
        "\"adcode\":{\"type\":\"string\",\"description\":\"可选行政区编码，如 110000；若提供则优先按 adcode 查询\"},"
        "\"type\":{\"type\":\"string\",\"description\":\"current、forecast 或 hourly；默认 current\"},"
        "\"days\":{\"type\":\"integer\",\"description\":\"预报天数（1-7，仅 forecast 时有效，可选）\"}"
        "},"
        "\"required\":[]}",
};

static struct tool_driver s_weather_driver = {
    .drv.name = "weather",
    .drv.probe = weather_probe,
    .execute = tool_weather_execute,
};

const struct tool_device *tool_weather_device(void)
{
    return &s_weather_device;
}

const struct tool_driver *tool_weather_driver(void)
{
    return &s_weather_driver;
}

const struct tool *tool_weather_definition(void)
{
    return &s_weather_tool;
}
