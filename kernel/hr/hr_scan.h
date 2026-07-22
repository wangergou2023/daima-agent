/* HR Transcript 扫描：查询最近成功的 Transcript 记录。 */
#pragma once

#include "err.h"
#include "transcript.h"
#include <time.h>

#define HR_SCAN_DEFAULT_SINCE_DAYS  7
#define HR_SCAN_DEFAULT_LIMIT       100

err_t hr_scan_transcripts(int since_days,
                          int limit,
                          transcript_record_t *out_records,
                          int out_capacity,
                          int *out_count);
