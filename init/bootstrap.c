/* 启动引导：运行时准备 + 8 级 initcall 链。 */

#include "bootstrap.h"
#include "fs.h"
#include "paths.h"
#include "runtime.h"

#include "bus.h"
#include "linux/bus.h"
#include "linux/core_task.h"
#include "hooks.h"
#include "drivers/memory/memory_store.h"
#include "drivers/memory/session_store.h"
#include "proxy.h"
#include "drivers/skill/skill_loader.h"
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

/**
 * 打印命令行帮助信息，显示 agent 主目录和配置文件路径。
 * @param prog 程序名（argv[0]）
 */
void bootstrap_print_usage(const char *prog)
{
    printf("Usage: %s [--help]\n", prog);
    printf("\n");
    printf("Agent home: %s\n", path_home());
    printf("Runtime configuration is loaded from %s\n", path_runtime_config_file());
    printf("Example template: %s/config.example.json\n", path_config_dir());
}

/**
 * 创建 SPIFFS 目录布局。
 * 路径层级：agent_home/ → spiffs_data/ → config/, memory/, sessions/, cache/, web/, skills/, workspace/
 */
static void ensure_spiffs_layout(void)
{
    fs_ensure_dir(path_home());
    fs_ensure_dir(path_spiffs_base());
    fs_ensure_dir(path_config_dir());
    fs_ensure_dir(path_memory_dir());
    fs_ensure_dir(path_session_dir());
    fs_ensure_dir(path_cache_dir());
    fs_ensure_dir(path_web_dir());
    fs_ensure_dir(path_feishu_image_dir());
    fs_ensure_dir(path_skills_dir());
    fs_ensure_dir(path_workspace_dir());
}

/**
 * 启动前运行时准备：路径初始化 → 目录创建 → 配置加载。
 */
void bootstrap_prepare_runtime(void)
{
    paths_init();
    ensure_spiffs_layout();
    err_t cfg_err = runtime_config_init();
    if (cfg_err != 0) {
        pr_warn("Runtime config init failed: %s", err_name(cfg_err));
    }
}

/**
 * 获取本机首选 IPv4 地址，跳过回环、0.0.0.0 和链路本地地址。
 * @param out    输出缓冲区
 * @param out_sz 缓冲区大小
 * @return 成功返回 true，失败返回 false
 */
bool bootstrap_get_primary_ipv4(char *out, size_t out_sz)
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
        if (!(ifa->ifa_flags & IFF_UP)) continue;       /* 跳过未启用的接口 */
        if (ifa->ifa_flags & IFF_LOOPBACK) continue;    /* 跳过回环接口 */

        struct sockaddr_in *sin = (struct sockaddr_in *)ifa->ifa_addr;
        if (!inet_ntop(AF_INET, &sin->sin_addr, out, out_sz)) continue;
        if (strcmp(out, "0.0.0.0") == 0) continue;          /* 跳过未绑定地址 */
        if (strncmp(out, "169.254.", 8) == 0) continue;     /* 跳过链路本地地址 */
        found = true;
        break;
    }

    freeifaddrs(ifaddr);
    return found;
}

/**
 * 核心基础设置 — 4 级 initcall 链：
 *
 *   core_initcall (1):    消息总线 + IPC + 钩子系统 — 最底层通信基础设施
 *   postcore_initcall (2): 内存存储 + 会话存储 — 持久化服务
 *   subsys_initcall (3):  cron 定时 + 心跳 + HTTP 代理 + 技能加载 — 子系统服务
 *   device_initcall (4):  总线初始化 + 通道注册 + LLM 注册 + 核间启动 — 设备与驱动
 *
 * 注：pure(0)、arch(3)、fs(5)、late(7) 4 级在本项目中未使用或推迟执行。
 *
 * @return 成功返回 0，失败触发 BUG_ON 终止
 */
int do_basic_setup(void)
{
    /* 第 1 级：core_initcall — IPC 与消息通信基础 */
    pr_info("core_initcall...");
    BUG_ON(message_bus_init() != 0);
    BUG_ON(core_ipc_init() != 0);
    agent_hooks_init();

    /* 第 2 级：postcore_initcall — 存储层（内存 + 会话持久化） */
    pr_info("postcore_initcall...");
    BUG_ON(memory_store_init() != 0);
    BUG_ON(session_store_init() != 0);

    /* 第 3 级：subsys_initcall — 子系统（cron、心跳、代理、技能） */
    pr_info("subsys_initcall...");
    BUG_ON(cron_service_init() != 0);
    BUG_ON(heartbeat_init() != 0);
    BUG_ON(http_proxy_init() != 0);
    BUG_ON(skill_loader_init() != 0);

    /* 第 4 级：device_initcall — 设备总线 + 通道 + LLM + 多核启动 */
    pr_info("device_initcall...");
    BUG_ON(bus_init() != 0);
    BUG_ON(bus_channel_register_all() != 0);
    BUG_ON(bus_llm_register_all() != 0);
    executor_core_start();
    memory_core_start();

    cron_service_start();
    heartbeat_start();

    pr_info("Basic setup complete");
    return 0;
}
