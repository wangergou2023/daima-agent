/* 核心配置与编译选项。 */

#pragma once

/* Daima 全局配置
 * - 以编译期宏为默认
 * - 运行时业务配置统一来自 spiffs_data/config/config.json
 */

#ifndef DAIMA_SECRET_PROXY_HOST
#define DAIMA_SECRET_PROXY_HOST      ""
#endif
#ifndef DAIMA_SECRET_PROXY_PORT
#define DAIMA_SECRET_PROXY_PORT      ""
#endif
#ifndef DAIMA_SECRET_PROXY_TYPE
#define DAIMA_SECRET_PROXY_TYPE      ""
#endif

/* 智能体主循环
 * - 栈大小与优先级直接影响稳定性
 */
#define DAIMA_AGENT_STACK             (24 * 1024)
#define DAIMA_AGENT_PRIO              6
#define DAIMA_AGENT_CORE              1
#define DAIMA_AGENT_MAX_HISTORY       64
#define DAIMA_AGENT_MAX_TOOL_ITER     20
#define DAIMA_MAX_TOOL_CALLS          4
#define DAIMA_INTENT_GATE_ENABLED     1
#define DAIMA_CATEGORY_ROUTING_ENABLED 1
#define DAIMA_PLAN_REVIEW_ENABLED     1
#ifndef DAIMA_SKILL_SCOPED_TOOLS_ENABLED
#define DAIMA_SKILL_SCOPED_TOOLS_ENABLED  1
#endif

/* 大模型（固定 OpenAI 兼容协议）
 * - STREAM_BUF_SIZE 用于 HTTP 响应缓存
 */
#define DAIMA_LLM_MAX_TOKENS          4096
#define DAIMA_OPENAI_API_URL          "https://api.openai.com/v1/chat/completions"
#define DAIMA_LLM_STREAM_BUF_SIZE     (256 * 1024)
#define DAIMA_LLM_LOG_VERBOSE_PAYLOAD 0   /* 1=完整分片输出 */
#define DAIMA_LLM_LOG_PREVIEW_BYTES   160 /* 0=不输出预览，仅输出长度 */

/* 图片理解 (Vision) */
#ifdef DAIMA_ENABLE_VISION
#define DAIMA_VISION_MAX_IMAGE_SIZE   (10 * 1024 * 1024)  /* 最大图片文件大小 10MB */
#define DAIMA_VISION_MAX_IMAGES       8                    /* 单次请求最大图片数 */
#endif

/* 图像抓拍（MIPS/IMP）
 * - 仅 MIPS 平台使用
 */
#define DAIMA_VISION_SNAPSHOT_DIR     "/tmp/daima_vision"
#define DAIMA_VISION_JPEG_TIMEOUT_MS  10000
#define DAIMA_VISION_JPEG_WARMUP_MS   500
#define DAIMA_VISION_JPEG_QP          40

/* 消息总线
 * - 入站/出站队列深度与任务参数
 */
#define DAIMA_BUS_QUEUE_LEN           16
#define DAIMA_OUTBOUND_STACK          (12 * 1024)
#define DAIMA_OUTBOUND_PRIO           5
#define DAIMA_OUTBOUND_CORE           0

/* 运行时数据目录
 * - 统一由 app/daima_paths.c 解析
 * - 优先使用 DAIMA_HOME
 * - 否则尝试根据可执行文件位置推导
 * - 再回退到 ~/.daima
 */
#define DAIMA_DEFAULT_HOME_DIR        "~/.daima"
/* Prompt 调试快照
 * - last_prompt.md: 最近一次最终 prompt
 * - DAIMA_DEBUG_PROMPT_DUMP=0/1 可在运行时关闭/开启
 */
#define DAIMA_DEBUG_PROMPT_DUMP      1
#define DAIMA_CONTEXT_BUF_SIZE       (64 * 1024)
#define DAIMA_SESSION_MAX_MSGS       96
/* read_file（文本读取）
 * - 默认按行分页，避免一次把太多代码塞进上下文
 * - MAX_CHARS 是单次输出的软上限；超出时会提前截断并提示缩小范围
 */
#define DAIMA_READ_FILE_DEFAULT_LIMIT 120
#define DAIMA_READ_FILE_MAX_LIMIT     400
#define DAIMA_READ_FILE_MAX_CHARS     6000
#define DAIMA_READ_FILE_MAX_LINE_CHARS 600
#define DAIMA_SAFE_EDIT_ENABLED       1
#define DAIMA_SEARCH_FILES_DEFAULT_LIMIT 40
#define DAIMA_SEARCH_FILES_MAX_LIMIT     200
#define DAIMA_SEARCH_FILES_MAX_LINE_CHARS 240
#define DAIMA_SEARCH_FILES_MAX_CONTEXT   3
#define DAIMA_SESSION_SEARCH_DEFAULT_LIMIT 8
#define DAIMA_SESSION_SEARCH_MAX_LIMIT     40

/* 上下文压缩
 * - 在会话历史过长时，将中间消息压缩成结构化摘要并回写 session
 * - 高收益阈值参数已转入 config.json
 */
#define DAIMA_CONTEXT_COMPRESS_ENABLED        1
#define DAIMA_CONTEXT_COMPRESS_MAX_CHARS      12000
#define DAIMA_CONTEXT_COMPRESS_PROTECT_FIRST  1
#define DAIMA_CONTEXT_COMPRESS_MAX_PASSES     2
#define DAIMA_COMPACTION_RECOVERY_ENABLED     1

/* 定时任务 / 心跳
 * - cron 定期检查
 * - 心跳用于状态记录
 */
#define DAIMA_CRON_MAX_JOBS           16

/* 飞书（Feishu/Lark） */
#define DAIMA_FEISHU_MAX_MSG_LEN      4096
#define DAIMA_FEISHU_POLL_STACK       (12 * 1024)
#define DAIMA_FEISHU_POLL_PRIO        5
#define DAIMA_FEISHU_POLL_CORE        0

/* WebSocket 网关 */
#define DAIMA_WS_MAX_CLIENTS          4

/* 语音/音频（MIPS）
 * - 仅 MIPS 平台真实生效，Host 为 stub
 */
#define DAIMA_AUDIO_SAMPLE_RATE       16000
#define DAIMA_AUDIO_CHANNELS          1
#define DAIMA_AUDIO_BITS_PER_SAMPLE   16
#define DAIMA_AUDIO_FRAME_MS          40

#define DAIMA_AUDIO_AI_DEV_ID         1
#define DAIMA_AUDIO_AI_CHN_ID         0
#define DAIMA_AUDIO_AO_DEV_ID         0
#define DAIMA_AUDIO_AO_CHN_ID         0
#define DAIMA_AUDIO_AI_FRM_NUM        40
#define DAIMA_AUDIO_AO_FRM_NUM        20

/* 唤醒按键 GPIO（sysfs）
 * - GPIO 号 / 轮询 / 防抖等高收益参数已转入 config.json
 */
#define DAIMA_VOICE_CHAT_ID           "voice"

/* 串口 CLI */
#define DAIMA_CLI_STACK               (4 * 1024)
#define DAIMA_CLI_PRIO                3
#define DAIMA_CLI_CORE                0

/* Vector / MCP 集成
 * - robot-mcp 子进程路径和轮询参数
 */
#define MCP_BIN_DEFAULT               "./robot-mcp"
#define MCP_READ_BUF_SIZE             (64 * 1024)   /* JSON 行最大长度 */
#define MCP_AUDIO_BUF_SIZE            (32 * 1024)   /* 解码后音频缓冲区 */
#define MCP_INIT_TIMEOUT_MS           30000
#define MCP_CALL_TIMEOUT_MS           30000
#define MCP_POLL_STACK                (12 * 1024)
#define MCP_POLL_PRIO                 5

/* Vector 音频参数
 * 说话结束由机器人 AudioDone 通知决定；这里只保留最大缓冲样本数。
 */
#ifndef VAD_MAX_SAMPLES
#define VAD_MAX_SAMPLES       (16000 * 30)
#endif

#define DAIMA_BUF_SMALL       256
#define DAIMA_BUF_MEDIUM      512
#define DAIMA_BUF_LARGE       1024
#define DAIMA_BUF_XLARGE      4096
#define DAIMA_BUF_PATH        512

#define DAIMA_TIMEOUT_SHORT       10000
#define DAIMA_TIMEOUT_MEDIUM      15000
#define DAIMA_TIMEOUT_DEFAULT     30000
#define DAIMA_TIMEOUT_LONG        60000

#define DAIMA_WS_PING_INTERVAL_SEC  20
#define DAIMA_WS_PONG_TIMEOUT_SEC   60
