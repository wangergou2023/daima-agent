/* 文件日志：环形缓冲区方式写入 agent.log，超出 512KB 时截断保留最新 384KB。 */

#include "log_file.h"

#include "fs.h"
#include "paths.h"
#include "linux/printk.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include "linux/slab.h"

/* 日志文件大小限制：超出时自动截断 */
#define LOG_FILE_MAX_SIZE  (512 * 1024)   /* 最大 512KB */
#define LOG_FILE_TRIM_KEEP (384 * 1024)   /* 截断后保留 384KB */

/**
 * 日志级别 → 单字符表示。
 * @param level 日志级别（LOG_ERROR/LOG_WARN/LOG_INFO/LOG_DEBUG）
 * @return 级别字符 "E"/"W"/"I"/"D"
 */
static const char *level_char(int level)
{
    switch (level) {
    case LOG_ERROR: return "E";
    case LOG_WARN:  return "W";
    case LOG_INFO:  return "I";
    case LOG_DEBUG: return "D";
    default: return "?";
    }
}

/**
 * 若日志文件超过 LOG_FILE_MAX_SIZE，截断保留末尾 LOG_FILE_TRIM_KEEP 字节。
 * 截断时对齐 UTF-8 边界（跳过续字节 0x80-0xBF）和新行首字符。
 * @param path 日志文件路径
 */
static void trim_if_needed(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0 || st.st_size <= LOG_FILE_MAX_SIZE) return;

    FILE *f = fopen(path, "r");
    if (!f) return;

    /* 定位到保留区域的起始位置 */
    fseek(f, st.st_size - LOG_FILE_TRIM_KEEP, SEEK_SET);
    /* 跳到下一个换行之后 */
    while (fgetc(f) != '\n' && ftell(f) < st.st_size) {}

    long keep_start = ftell(f);
    if (keep_start <= 0) { fclose(f); return; }

    /* 对齐 UTF-8 边界：跳过续字节 (0x80-0xBF) */
    fseek(f, keep_start, SEEK_SET);
    int c;
    while ((c = fgetc(f)) != EOF && (unsigned char)c >= 0x80 && (unsigned char)c <= 0xBF) {}
    if (c != EOF) keep_start = ftell(f) - 1;
    if (keep_start <= 0) { fclose(f); return; }

    /* 读取保留内容 → 重写文件 */
    size_t keep_len = (size_t)(st.st_size - keep_start);
    char *buf = kmalloc(keep_len + 1, GFP_KERNEL);
    if (!buf) { fclose(f); return; }

    fseek(f, keep_start, SEEK_SET);
    size_t nread = fread(buf, 1, keep_len, f);
    fclose(f);
    buf[nread] = '\0';

    f = fopen(path, "w");
    if (!f) { kfree(buf); return; }
    fwrite(buf, 1, nread, f);
    fclose(f);
    kfree(buf);
}

/**
 * 追加写入日志文件。格式：HH:MM:SS.mmm [级别] 标签: 消息
 * 写入后自动检查并截断超出大小限制的日志文件。
 * @param level 日志级别
 * @param tag   日志标签
 * @param msg   日志消息
 */
void log_file_write(int level, const char *tag, const char *msg)
{
    char log_path[512];
    snprintf(log_path, sizeof(log_path), "%s/agent.log", path_memory_dir());

    fs_ensure_dir(path_memory_dir());

    /* 时间戳：HH:MM:SS.mmm */
    struct timeval tv;
    gettimeofday(&tv, NULL);
    struct tm tm;
    localtime_r(&tv.tv_sec, &tm);

    char ts[32];
    strftime(ts, sizeof(ts), "%H:%M:%S", &tm);

    FILE *f = fopen(log_path, "a");
    if (!f) return;

    fprintf(f, "%s.%03ld [%s] %s: %s\n",
            ts, tv.tv_usec / 1000,
            level_char(level), tag ? tag : "-", msg ? msg : "");
    fclose(f);

    trim_if_needed(log_path);
}
