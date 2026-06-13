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

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "autoconf.h"
#include "log.h"

static const char *TAG = "bootstrap";

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
    daima_err_t cfg_err = runtime_config_init();
    if (cfg_err != DAIMA_OK) {
        DAIMA_LOGW(TAG, "Runtime config init failed: %s", daima_err_to_name(cfg_err));
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
    DAIMA_LOGI(TAG, "core_initcall...");
    DAIMA_ERROR_CHECK(message_bus_init());
    agent_hooks_init();

    DAIMA_LOGI(TAG, "postcore_initcall...");
    DAIMA_ERROR_CHECK(memory_store_init());
    DAIMA_ERROR_CHECK(session_store_init());

    DAIMA_LOGI(TAG, "subsys_initcall...");
    DAIMA_ERROR_CHECK(http_proxy_init());
    DAIMA_ERROR_CHECK(skill_loader_init());

    DAIMA_LOGI(TAG, "device_initcall...");
    DAIMA_ERROR_CHECK(voice_channel_init());
    DAIMA_ERROR_CHECK(feishu_bot_init());
    DAIMA_ERROR_CHECK(feishu_bot_start());
    DAIMA_ERROR_CHECK(vector_channel_init());

    DAIMA_LOGI(TAG, "Basic setup complete");
}
