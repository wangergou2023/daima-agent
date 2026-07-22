#include "hr_scan.h"
#include "linux/printk.h"

err_t hr_scan_transcripts(int since_days,
                          int limit,
                          transcript_record_t *out_records,
                          int out_capacity,
                          int *out_count)
{
    if (since_days <= 0)
        since_days = HR_SCAN_DEFAULT_SINCE_DAYS;
    if (limit <= 0)
        limit = HR_SCAN_DEFAULT_LIMIT;

    time_t since_ts = time(NULL) - (time_t)(since_days * 86400);

    int count = 0;
    err_t err = transcript_query(NULL, "success", since_ts, limit,
                                 out_records, out_capacity, &count);
    if (err != 0) {
        pr_err("HR scan failed: %d", err);
        *out_count = 0;
        return err;
    }

    *out_count = count;
    pr_info("HR scan: %d successful transcripts in %d days", count, since_days);
    return 0;
}
