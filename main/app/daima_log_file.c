#include "app/daima_log_file.h"

#include "app/daima_fs.h"
#include "app/daima_paths.h"
#include "daima_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>

#define LOG_FILE_MAX_SIZE  (512 * 1024)
#define LOG_FILE_TRIM_KEEP (384 * 1024)

static const char *level_char(int level)
{
    switch (level) {
    case DAIMA_LOG_ERROR: return "E";
    case DAIMA_LOG_WARN:  return "W";
    case DAIMA_LOG_INFO:  return "I";
    case DAIMA_LOG_DEBUG: return "D";
    default: return "?";
    }
}

static void trim_if_needed(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0 || st.st_size <= LOG_FILE_MAX_SIZE) return;

    FILE *f = fopen(path, "r");
    if (!f) return;

    fseek(f, st.st_size - LOG_FILE_TRIM_KEEP, SEEK_SET);
    while (fgetc(f) != '\n' && ftell(f) < st.st_size) {}

    long keep_start = ftell(f);
    if (keep_start <= 0) { fclose(f); return; }

    /* align to UTF-8 boundary: skip continuation bytes (0x80-0xBF) */
    fseek(f, keep_start, SEEK_SET);
    int c;
    while ((c = fgetc(f)) != EOF && (unsigned char)c >= 0x80 && (unsigned char)c <= 0xBF) {}
    if (c != EOF) keep_start = ftell(f) - 1;
    if (keep_start <= 0) { fclose(f); return; }

    size_t keep_len = (size_t)(st.st_size - keep_start);
    char *buf = malloc(keep_len + 1);
    if (!buf) { fclose(f); return; }

    fseek(f, keep_start, SEEK_SET);
    size_t nread = fread(buf, 1, keep_len, f);
    fclose(f);
    buf[nread] = '\0';

    f = fopen(path, "w");
    if (!f) { free(buf); return; }
    fwrite(buf, 1, nread, f);
    fclose(f);
    free(buf);
}

void daima_log_file_write(int level, const char *tag, const char *msg)
{
    const char *path = daima_path_log_file();
    if (!path) return;

    daima_fs_ensure_dir(daima_path_memory_dir());

    struct timeval tv;
    gettimeofday(&tv, NULL);
    struct tm tm;
    localtime_r(&tv.tv_sec, &tm);

    char ts[32];
    strftime(ts, sizeof(ts), "%H:%M:%S", &tm);

    FILE *f = fopen(path, "a");
    if (!f) return;

    fprintf(f, "%s.%03ld [%s] %s: %s\n",
            ts, tv.tv_usec / 1000,
            level_char(level), tag ? tag : "-", msg ? msg : "");
    fclose(f);

    trim_if_needed(path);
}
