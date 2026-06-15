/* Ring-buffer log file for agent self-diagnosis. */

#pragma once

void log_file_write(int level, const char *tag, const char *msg);
