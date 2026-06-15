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
static const char *resolve_runtime_timezone(void)
{
    return runtime_config_get_timezone();
}

int main(int argc, char **argv)
{
#ifdef BUILD_FOR_MIPS
    /* Auto-register systemd service on boot (rootfs is RO, runtime link needed each boot) */
    mkdir("/run/systemd/system", 0755);
    symlink("/data/daima/daima.service", "/run/systemd/system/daima.service");
#endif

    if (argc > 1) {
        const char *arg = argv[1];
        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            daima_bootstrap_print_usage(argv[0]);
            return 0;
        }
        fprintf(stderr, "Unsupported option: %s\n", arg);
        daima_bootstrap_print_usage(argv[0]);
        return 1;
    }

    daima_bootstrap_prepare_runtime();

    const char *runtime_tz = resolve_runtime_timezone();
    setenv("TZ", runtime_tz, 1);
    tzset();

    pr_info("========================================");
    pr_info("  Daima - Host AI Agent (Linux)");
    pr_info("========================================");
    pr_info("Free memory: %d bytes", (int)daima_get_free_memory());
    pr_info("Timezone: %s", runtime_tz);

    do_basic_setup();

    BUG_ON(llm_proxy_init() != 0);
    BUG_ON(tool_registry_init() != 0);
    BUG_ON(agent_loop_init() != 0);

    BUG_ON(channel_router_start() != 0);

    BUG_ON(agent_loop_start() != 0);
    err_t ws_err = ws_server_start();
    if (ws_err != 0) {
        pr_warn("WebSocket server failed to start: %s", err_name(ws_err));
    }

    pr_info("All services started!");
    char host_ip[INET_ADDRSTRLEN] = "0.0.0.0";
    daima_bootstrap_get_primary_ipv4(host_ip, sizeof(host_ip));
    pr_info("代马 Daima 已就绪，Web UI: http://%s:%d", host_ip, runtime_config_get_web_port());

    while (1) {
        sleep(1);
    }

    return 0;
}
