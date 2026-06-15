#include "drivers/tool/tool_get_time.h"

#include "autoconf.h"

#include "http.h"



#include <string.h>

#include <stdlib.h>

#include <time.h>

#include <ctype.h>

#include <stdbool.h>

#include <strings.h>

#include "linux/printk.h"

#include "linux/kernel.h"

static const struct tool s_get_time_tool = {

    .name = "get_current_time",

    .description = "获取当前日期和时间（并同步系统时钟）。需要知道日期/时间时使用。",

    .input_schema_json =

        "{\"type\":\"object\","

        "\"properties\":{},"

        "\"required\":[]}",

    .execute = tool_get_time_execute,

};



static const char *MONTHS[] = {

    "Jan","Feb","Mar","Apr","May","Jun",

    "Jul","Aug","Sep","Oct","Nov","Dec"

};



static bool parse_http_date(const char *date_str, time_t *out_time)

{

    int day, year, hour, min, sec;

    char mon_str[4] = {0};



    if (sscanf(date_str, "%*[^,], %d %3s %d %d:%d:%d",

               &day, mon_str, &year, &hour, &min, &sec) != 6) {

        return false;

    }



    int mon = -1;

    for (int i = 0; i < 12; i++) {

        if (strcmp(mon_str, MONTHS[i]) == 0) { mon = i; break; }

    }

    if (mon < 0) return false;



    struct tm tm = {

        .tm_sec = sec, .tm_min = min, .tm_hour = hour,

        .tm_mday = day, .tm_mon = mon, .tm_year = year - 1900,

    };



    const char *prev_tz = getenv("TZ");

    char tz_backup[128] = {0};

    if (prev_tz && prev_tz[0]) {

        strscpy(tz_backup, prev_tz, sizeof(tz_backup));

    }



    setenv("TZ", "UTC0", 1);

    tzset();

    time_t t = mktime(&tm);

    if (tz_backup[0]) {

        setenv("TZ", tz_backup, 1);

    } else {

        unsetenv("TZ");

    }

    tzset();



    if (t < 0) return false;

    *out_time = t;

    return true;

}



static bool extract_date_header(const char *headers, char *out, size_t out_size)

{

    if (!headers) return false;

    const char *p = headers;

    while (*p) {

        const char *line_end = strstr(p, "\r\n");

        size_t len = line_end ? (size_t)(line_end - p) : strlen(p);

        if (len > 6 && strncasecmp(p, "Date:", 5) == 0) {

            const char *val = p + 5;

            while (*val && isspace((unsigned char)*val)) val++;

            size_t vlen = len - (size_t)(val - p);

            if (vlen >= out_size) vlen = out_size - 1;

            memcpy(out, val, vlen);

            out[vlen] = '\0';

            return true;

        }

        if (!line_end) break;

        p = line_end + 2;

    }

    return false;

}



static void format_time(time_t t, char *out, size_t out_size)

{

    struct tm local;

    localtime_r(&t, &local);

    strftime(out, out_size, "%Y-%m-%d %H:%M:%S %Z (%A)", &local);

}



err_t tool_get_time_execute(const char *input_json, char *output, size_t output_size)

{

    (void)input_json;

    pr_info("Fetching current time...");



    host_http_response_t resp = {0};

    err_t err = host_http_request("HEAD", "https://example.com/", NULL, NULL, 5000, &resp);



    time_t t = time(NULL);

    bool ok = false;



    if (err == 0 && resp.headers) {

        char date_val[64] = {0};

        if (extract_date_header(resp.headers, date_val, sizeof(date_val))) {

            ok = parse_http_date(date_val, &t);

        }

    }



    host_http_response_free(&resp);



    if (!ok) {

        pr_warn("Falling back to local system time");

    }



    format_time(t, output, output_size);

    pr_info("Time: %s", output);

    return 0;

}



const struct tool *tool_get_time_definition(void)

{

    return &s_get_time_tool;

}


static int get_time_tool_probe(struct device *dev)
{
    (void)dev;
    return 0;
}

static struct tool_device s_get_time_device = {
    .name = "get_current_time",
    .description = "获取当前日期和时间（并同步系统时钟）。需要知道日期/时间时使用。",
    .input_schema_json = "{\"type\":\"object\"," "\"properties\":{}," "\"required\":[]}",
};

static struct tool_driver s_get_time_driver = {
    .drv.name = "get_current_time",
    .drv.probe = get_time_tool_probe,
    .execute = tool_get_time_execute,
};

const struct tool_device *tool_get_time_device(void)
{
    return &s_get_time_device;
}

const struct tool_driver *tool_get_time_driver(void)
{
    return &s_get_time_driver;
}
