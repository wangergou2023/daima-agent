/* 文件日志接口：环形缓冲区方式写入 agent.log，用于 Agent 自诊断。 */

#pragma once

/** 追加写入日志文件（格式：HH:MM:SS.mmm [级别] 标签: 消息）。 */
void log_file_write(int level, const char *tag, const char *msg);
