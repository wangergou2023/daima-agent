#include "bootstrap.h"
#include "fs.h"
#include "paths.h"
#include "runtime.h"

#include "bus.h"
#include "hooks.h"
#include "drivers/memory/memory_store.h"
#include "drivers/memory/session_store.h"
#include "proxy.h"
#include "drivers/skill/skill_loader.h"
#include "drivers/voice/voice_channel.h"
#include "drivers/channel/feishu/feishu_bot.h"
#include "drivers/channel/vector/vector_channel.h"
#include "linux/init.h"
#include "kernel/time/timer.h"
#include "linux/workqueue.h"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "autoconf.h"
#include "linux/kernel.h"
#include "linux/printk.h"
void daima_bootstrap_print_usage(const char *prog)
{
    printf("Usage: %s [--help]\n", prog);
    printf("\n");
    printf("Daima home: %s\n", daima_path_home());
    printf("Runtime configuration is loaded from %s\n", daima_path_runtime_config_file());
    printf("Example template: %s/config.example.json\n", daima_path_config_dir());
}

static void ensure_spiffs_layout(void)
{
    daima_fs_ensure_dir(daima_path_home());
    daima_fs_ensure_dir(daima_path_spiffs_base());
    daima_fs_ensure_dir(daima_path_config_dir());
    daima_fs_ensure_dir(daima_path_memory_dir());
    daima_fs_ensure_dir(daima_path_session_dir());
    daima_fs_ensure_dir(daima_path_cache_dir());
    daima_fs_ensure_dir(daima_path_web_dir());
    daima_fs_ensure_dir(daima_path_feishu_image_dir());
    daima_fs_ensure_dir(daima_path_skills_dir());
    daima_fs_ensure_dir(daima_path_workspace_dir());
}

void daima_bootstrap_prepare_runtime(void)
{
    daima_paths_init();
    ensure_spiffs_layout();
    err_t cfg_err = runtime_config_init();
    if (cfg_err != 0) {
        pr_warn("Runtime config init failed: %s", err_name(cfg_err));
    }
}

bool daima_bootstrap_get_primary_ipv4(char *out, size_t out_sz)
{
    if (!out || out_sz == 0) {
        return false;
    }

    struct ifaddrs *ifaddr = NULL;
    if (getifaddrs(&ifaddr) != 0) {
        return false;
    }

    bool found = false;
    for (struct ifaddrs *ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr) continue;
        if (ifa->ifa_addr->sa_family != AF_INET) continue;
        if (!(ifa->ifa_flags & IFF_UP)) continue;
        if (ifa->ifa_flags & IFF_LOOPBACK) continue;

        struct sockaddr_in *sin = (struct sockaddr_in *)ifa->ifa_addr;
        if (!inet_ntop(AF_INET, &sin->sin_addr, out, out_sz)) continue;
        if (strcmp(out, "0.0.0.0") == 0) continue;
        if (strncmp(out, "169.254.", 8) == 0) continue;
        found = true;
        break;
    }

    freeifaddrs(ifaddr);
    return found;
}

void do_basic_setup(void)
{
    pr_info("core_initcall...");
    BUG_ON(message_bus_init() != 0);
    agent_hooks_init();

    pr_info("postcore_initcall...");
    BUG_ON(memory_store_init() != 0);
    BUG_ON(session_store_init() != 0);

    pr_info("subsys_initcall...");
    BUG_ON(cron_service_init() != 0);
    BUG_ON(heartbeat_init() != 0);
    BUG_ON(http_proxy_init() != 0);
    BUG_ON(skill_loader_init() != 0);

    pr_info("device_initcall...");
    BUG_ON(voice_channel_init() != 0);
    BUG_ON(feishu_bot_init() != 0);
    BUG_ON(feishu_bot_start() != 0);
    BUG_ON(vector_channel_init() != 0);

    cron_service_start();
    heartbeat_start();

    pr_info("Basic setup complete");
}
