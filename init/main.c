/* Agent 唯一入口。4 阶段启动流程：agent_home 准备 → spiffs 初始化 → do_basic_setup → agent_start。 */

#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>

#include "channel_router.h"
#include "bootstrap.h"
#include "runtime.h"
#include "loop.h"
#include "hooks.h"
#include "bus.h"
#include "linux/driver.h"
#include "linux/bus.h"
#include "linux/init.h"
#include "drivers/channel/feishu/feishu_bot.h"
#include "drivers/channel/vector/vector_channel.h"
#include "drivers/channel/gateway/ws_server.h"
#include "drivers/llm/llm_proxy.h"
#include "drivers/memory/memory_store.h"
#include "drivers/memory/session_store.h"
#include "autoconf.h"
#include "linux/kernel.h"
#include "linux/printk.h"
#include "os.h"
#include "drivers/platform/platform.h"
#include "proxy.h"
#include "drivers/skill/skill_loader.h"
#include "drivers/tool/tool_registry.h"
#include "drivers/voice/voice_channel.h"
#include "drivers/voice/voice_wake.h"

/**
 * 从运行时配置解析当前时区。
 * @return 时区字符串（如 "Asia/Shanghai"）。
 */
static const char *resolve_runtime_timezone(void)
{
    return runtime_config_get_timezone();
}

/**
 * 程序主入口。4 阶段启动：
 *   阶段 1: bootstrap_prepare_runtime() — 路径初始化 + 目录创建 + 配置加载
 *   阶段 2: do_basic_setup() — 4 级手动 initcall 链（消息总线、IPC、存储、cron、设备总线）
 *   阶段 3: llm_proxy_init + tool_registry_init + agent_loop_init — 驱动和循环初始化
 *   阶段 4: channel_router_start + agent_loop_start + ws_server_start — 启动所有服务
 *
 * @param argc 参数个数（未使用，保留兼容性）
 * @param argv 参数列表（未使用，保留兼容性）
 * @return 0 成功，非 0 失败
 */
int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

#ifdef BUILD_FOR_MIPS
    /* MIPS 平台：每次启动自动注册 systemd 服务（rootfs 只读，需运行时链接） */
    mkdir("/run/systemd/system", 0755);
    symlink("/data/agent-data/agent.service", "/run/systemd/system/agent.service");
#endif

    /* 阶段 1: 运行时准备 — 路径、目录、配置 */
    bootstrap_prepare_runtime();

    /* 设置时区 */
    const char *runtime_tz = resolve_runtime_timezone();
    setenv("TZ", runtime_tz, 1);
    tzset();

    pr_info("========================================");
    pr_info("  Agent - Host AI Agent (Linux)");
    pr_info("========================================");
    pr_info("Free memory: %d bytes", (int)platform_free_memory());
    pr_info("Timezone: %s", runtime_tz);

    /* 阶段 2: 基础设置 — 4 级手动 initcall 链 */
    BUG_ON(do_basic_setup() != 0);

    /* 阶段 3: 驱动层初始化 — LLM 代理、工具注册、Agent 循环 */
    BUG_ON(llm_proxy_init() != 0);
    BUG_ON(tool_registry_init() != 0);
    BUG_ON(agent_loop_init() != 0);

    /* 阶段 4a: 启动通道路由（飞书/Vector/WebSocket） */
    BUG_ON(channel_router_start() != 0);

    /* 阶段 4b: 启动 Agent 主循环和 WebSocket 服务 */
    BUG_ON(agent_loop_start() != 0);
    err_t ws_err = ws_server_start();
    if (ws_err != 0) {
        pr_warn("WebSocket server failed to start: %s", err_name(ws_err));
    }

    /* 阶段 4c: 打印就绪信息，进入空闲循环 */
    pr_info("All services started!");
    char host_ip[INET_ADDRSTRLEN] = "0.0.0.0";
    bootstrap_get_primary_ipv4(host_ip, sizeof(host_ip));
    pr_info("Agent 已就绪，Web UI: http://%s:%d", host_ip, runtime_config_get_web_port());

    while (1) {
        sleep(1);
    }

    return 0;
}
