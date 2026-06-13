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
#include "cron.h"
#include "drivers/channel/gateway/ws_server.h"
#include "heartbeat.h"
#include "drivers/llm/llm_proxy.h"
#include "drivers/memory/memory_store.h"
#include "drivers/memory/session_store.h"
#include "autoconf.h"
#include "log.h"
#include "os.h"
#include "drivers/platform/platform.h"
#include "proxy.h"
#include "drivers/skill/skill_loader.h"
#include "drivers/tool/tool_registry.h"
#include "drivers/voice/voice_channel.h"
#include "drivers/voice/voice_wake.h"

static const char *TAG = "daima";

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

    DAIMA_LOGI(TAG, "========================================");
    DAIMA_LOGI(TAG, "  Daima - Host AI Agent (Linux)");
    DAIMA_LOGI(TAG, "========================================");
    DAIMA_LOGI(TAG, "Free memory: %d bytes", (int)daima_get_free_memory());
    DAIMA_LOGI(TAG, "Timezone: %s", runtime_tz);

    do_basic_setup();

    DAIMA_ERROR_CHECK(llm_proxy_init());
    DAIMA_ERROR_CHECK(tool_registry_init());
    DAIMA_ERROR_CHECK(cron_service_init());
    DAIMA_ERROR_CHECK(heartbeat_init());
    DAIMA_ERROR_CHECK(agent_loop_init());

    DAIMA_ERROR_CHECK(channel_router_start());

    DAIMA_ERROR_CHECK(agent_loop_start());
    cron_service_start();
    heartbeat_start();
    daima_err_t ws_err = ws_server_start();
    if (ws_err != DAIMA_OK) {
        DAIMA_LOGW(TAG, "WebSocket server failed to start: %s", daima_err_to_name(ws_err));
    }

    DAIMA_LOGI(TAG, "All services started!");
    char host_ip[INET_ADDRSTRLEN] = "0.0.0.0";
    daima_bootstrap_get_primary_ipv4(host_ip, sizeof(host_ip));
    DAIMA_LOGI(TAG, "代马 Daima 已就绪，Web UI: http://%s:%d", host_ip, runtime_config_get_web_port());

    while (1) {
        sleep(1);
    }

    return 0;
}
