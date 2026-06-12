cmake_minimum_required(VERSION 3.12)

# 选择编译目标: 系统库 或 MIPS 交叉编译
# - BUILD_FOR_MIPS=ON 时启用交叉编译工具链与第三方库路径
# - 默认 OFF 时使用宿主机系统库（便于本地调试）
option(BUILD_FOR_MIPS "Build for MIPS architecture (cross-compile)" OFF)

# 启用图片理解功能 (Vision/Multimodal)
# - 仅控制编译期开关；运行时仍可根据实现决定是否返回图片
option(DAIMA_ENABLE_VISION "Enable image understanding and multimodal support" ON)

if(BUILD_FOR_MIPS)
    # MIPS uclibc 交叉编译设置
    # - 指定交叉编译器与系统信息
    # - third_party/build_libs_mips 下放置编译好的依赖库
    set(CMAKE_SYSTEM_NAME Linux)
    set(CMAKE_SYSTEM_PROCESSOR mips)
    set(CMAKE_C_COMPILER "mips-linux-uclibc-gnu-gcc")
    set(CMAKE_CXX_COMPILER "mips-linux-uclibc-gnu-g++")
    set(LOCAL_THIRD_PARTY "${CMAKE_CURRENT_LIST_DIR}/third_party/build_libs_mips")
    set(MIPS_LIB_DIR "${LOCAL_THIRD_PARTY}/lib" CACHE PATH "MIPS library directory")
    set(MIPS_INCLUDE_DIR "${LOCAL_THIRD_PARTY}/include" CACHE PATH "MIPS include directory")
    # MIPS 平台媒体库（IMP）路径
    set(MIPS_MEDIA_DIR "${CMAKE_CURRENT_LIST_DIR}/third_party/media")
    set(MIPS_MEDIA_LIB_DIR "${MIPS_MEDIA_DIR}/lib/uclibc" CACHE PATH "MIPS media library directory")
    set(MIPS_MEDIA_INCLUDE_DIR "${MIPS_MEDIA_DIR}/include" CACHE PATH "MIPS media include directory")
    message(STATUS "Building for MIPS using ${CMAKE_C_COMPILER}")
    message(STATUS "Using MIPS libraries from: ${LOCAL_THIRD_PARTY}")
else()
    if(NOT LOCAL_THIRD_PARTY)
        set(LOCAL_THIRD_PARTY "")
    endif()
    message(STATUS "Using system libraries")
    # macOS Homebrew paths
    if(APPLE AND EXISTS "/opt/homebrew/opt/cjson/include")
        include_directories("/opt/homebrew/opt/cjson/include")
    endif()
    if(APPLE AND EXISTS "/opt/homebrew/opt/openssl/include")
        include_directories("/opt/homebrew/opt/openssl/include")
    endif()
endif()

project(daima C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# 源文件集合（Host/MIPS 公共部分）
set(SRC
    ${CMAKE_CURRENT_LIST_DIR}/main/daima_host.c
    ${CMAKE_CURRENT_LIST_DIR}/main/app/daima_bootstrap.c
    ${CMAKE_CURRENT_LIST_DIR}/main/app/daima_fs.c
    ${CMAKE_CURRENT_LIST_DIR}/main/app/daima_paths.c
    ${CMAKE_CURRENT_LIST_DIR}/main/app/runtime_config.c
    ${CMAKE_CURRENT_LIST_DIR}/main/app/runtime_config_sections.c
    ${CMAKE_CURRENT_LIST_DIR}/main/app/channel_router.c
    ${CMAKE_CURRENT_LIST_DIR}/main/app/channel_runtime.c
    ${CMAKE_CURRENT_LIST_DIR}/main/app/tool_activity_notifier.c
    ${CMAKE_CURRENT_LIST_DIR}/main/app/interactive_requests.c
    ${CMAKE_CURRENT_LIST_DIR}/main/host_http.c
    ${CMAKE_CURRENT_LIST_DIR}/main/host_tls.c
    ${CMAKE_CURRENT_LIST_DIR}/main/proxy/http_proxy_host.c
    ${CMAKE_CURRENT_LIST_DIR}/main/llm/llm_proxy_host.c
    ${CMAKE_CURRENT_LIST_DIR}/main/llm/model_fallback.c
    ${CMAKE_CURRENT_LIST_DIR}/main/llm/llm_http_client_host.c
    ${CMAKE_CURRENT_LIST_DIR}/main/llm/llm_anthropic_payload.c
    ${CMAKE_CURRENT_LIST_DIR}/main/llm/llm_openai_payload.c
    ${CMAKE_CURRENT_LIST_DIR}/main/llm/model_fallback.c
    ${CMAKE_CURRENT_LIST_DIR}/main/tools/tool_weather_host.c
    ${CMAKE_CURRENT_LIST_DIR}/main/tools/tool_get_time_host.c
    ${CMAKE_CURRENT_LIST_DIR}/main/tools/tool_webfetch.c
    ${CMAKE_CURRENT_LIST_DIR}/main/tools/tool_daima_log.c
    ${CMAKE_CURRENT_LIST_DIR}/main/app/daima_log_file.c
    ${CMAKE_CURRENT_LIST_DIR}/main/tools/tool_daima_log.c
    ${CMAKE_CURRENT_LIST_DIR}/main/gateway/ws_server_host.c
    ${CMAKE_CURRENT_LIST_DIR}/main/gateway/ws_http_helpers.c
    ${CMAKE_CURRENT_LIST_DIR}/main/gateway/ws_client_session.c
    ${CMAKE_CURRENT_LIST_DIR}/main/pet/pet_event.c
    ${CMAKE_CURRENT_LIST_DIR}/main/platform/daima_platform.c
    ${CMAKE_CURRENT_LIST_DIR}/main/platform/daima_os.c
    ${CMAKE_CURRENT_LIST_DIR}/main/daima_text.c
    ${CMAKE_CURRENT_LIST_DIR}/main/utils/json_helpers.c
    ${CMAKE_CURRENT_LIST_DIR}/main/daima_base64.c
    ${CMAKE_CURRENT_LIST_DIR}/main/daima_env.c
    ${CMAKE_CURRENT_LIST_DIR}/main/agent/agent_loop.c
    ${CMAKE_CURRENT_LIST_DIR}/main/agent/agent_cancel.c
    ${CMAKE_CURRENT_LIST_DIR}/main/agent/channel_policy.c
    ${CMAKE_CURRENT_LIST_DIR}/main/agent/intent_gate.c
    ${CMAKE_CURRENT_LIST_DIR}/main/agent/category_router.c
    ${CMAKE_CURRENT_LIST_DIR}/main/agent/plan_review.c
    ${CMAKE_CURRENT_LIST_DIR}/main/agent/team_mode.c
    ${CMAKE_CURRENT_LIST_DIR}/main/agent/agent_turn_common.c
    ${CMAKE_CURRENT_LIST_DIR}/main/agent/agent_prompt_debug.c
    ${CMAKE_CURRENT_LIST_DIR}/main/agent/agent_turn_persist.c
    ${CMAKE_CURRENT_LIST_DIR}/main/agent/agent_turn_prepare.c
    ${CMAKE_CURRENT_LIST_DIR}/main/agent/agent_turn_run.c
    ${CMAKE_CURRENT_LIST_DIR}/main/agent/agent_turn_exec_helpers.c
    ${CMAKE_CURRENT_LIST_DIR}/main/agent/todo_enforcer.c
    ${CMAKE_CURRENT_LIST_DIR}/main/agent/tool_feedback.c
    ${CMAKE_CURRENT_LIST_DIR}/main/agent/agent_turn_finish.c
    ${CMAKE_CURRENT_LIST_DIR}/main/agent/tool_protocol_guard.c
    ${CMAKE_CURRENT_LIST_DIR}/main/agent/context_compressor.c
    ${CMAKE_CURRENT_LIST_DIR}/main/agent/compaction_recovery.c
    ${CMAKE_CURRENT_LIST_DIR}/main/agent/session_recovery.c
    ${CMAKE_CURRENT_LIST_DIR}/main/agent/context_compress_ops.c
    ${CMAKE_CURRENT_LIST_DIR}/main/agent/context_builder.c
    ${CMAKE_CURRENT_LIST_DIR}/main/agent/learning_review.c
    ${CMAKE_CURRENT_LIST_DIR}/main/bus/message_bus.c
    ${CMAKE_CURRENT_LIST_DIR}/main/cron/cron_service.c
    ${CMAKE_CURRENT_LIST_DIR}/main/heartbeat/heartbeat.c
    ${CMAKE_CURRENT_LIST_DIR}/main/memory/memory_store.c
    ${CMAKE_CURRENT_LIST_DIR}/main/memory/session_store.c
    ${CMAKE_CURRENT_LIST_DIR}/main/memory/session_store_file.c
    ${CMAKE_CURRENT_LIST_DIR}/main/memory/session_store_file_facts.c
    ${CMAKE_CURRENT_LIST_DIR}/main/memory/session_store_file_summary.c
    ${CMAKE_CURRENT_LIST_DIR}/main/memory/session_store_file_common.c
    ${CMAKE_CURRENT_LIST_DIR}/main/work_items/work_item_store.c
    ${CMAKE_CURRENT_LIST_DIR}/main/tools/tool_registry.c
    ${CMAKE_CURRENT_LIST_DIR}/main/tools/tool_invocation_context.c
    ${CMAKE_CURRENT_LIST_DIR}/main/tools/tool_runtime.c
    ${CMAKE_CURRENT_LIST_DIR}/main/tools/tool_file_list.c
    ${CMAKE_CURRENT_LIST_DIR}/main/tools/tool_file_ops.c
    ${CMAKE_CURRENT_LIST_DIR}/main/tools/tool_file_paths.c
    ${CMAKE_CURRENT_LIST_DIR}/main/tools/tool_file_checkpoint.c
    ${CMAKE_CURRENT_LIST_DIR}/main/tools/tool_safe_edit.c
    ${CMAKE_CURRENT_LIST_DIR}/main/tools/tool_hashline.c
    ${CMAKE_CURRENT_LIST_DIR}/main/tools/tool_file_read.c
    ${CMAKE_CURRENT_LIST_DIR}/main/tools/tool_file_query.c
    ${CMAKE_CURRENT_LIST_DIR}/main/tools/tool_file_mutations.c
    ${CMAKE_CURRENT_LIST_DIR}/main/tools/tool_file_search.c
    ${CMAKE_CURRENT_LIST_DIR}/main/tools/tool_files.c
    ${CMAKE_CURRENT_LIST_DIR}/main/tools/tool_cron.c
    ${CMAKE_CURRENT_LIST_DIR}/main/tools/tool_system.c
    ${CMAKE_CURRENT_LIST_DIR}/main/tools/tool_terminal_exec.c
    ${CMAKE_CURRENT_LIST_DIR}/main/tools/tool_todo.c
    ${CMAKE_CURRENT_LIST_DIR}/main/tools/tool_work_item.c
    ${CMAKE_CURRENT_LIST_DIR}/main/tools/tool_skills.c
    ${CMAKE_CURRENT_LIST_DIR}/main/tools/tool_session_search.c
    ${CMAKE_CURRENT_LIST_DIR}/main/tools/tool_session_search_scan.c
    ${CMAKE_CURRENT_LIST_DIR}/main/tools/tool_session_search_render.c
    ${CMAKE_CURRENT_LIST_DIR}/main/tools/tool_vector_motion.c
    ${CMAKE_CURRENT_LIST_DIR}/main/tools/tool_vector_body.c
    ${CMAKE_CURRENT_LIST_DIR}/main/tools/tool_vector_audio.c
    ${CMAKE_CURRENT_LIST_DIR}/main/tools/tool_vector_status.c
    ${CMAKE_CURRENT_LIST_DIR}/main/tools/tool_vector_anim.c
    ${CMAKE_CURRENT_LIST_DIR}/main/skills/skill_loader.c
    ${CMAKE_CURRENT_LIST_DIR}/main/skills/skill_meta.c
    ${CMAKE_CURRENT_LIST_DIR}/main/skills/skill_tools.c
    ${CMAKE_CURRENT_LIST_DIR}/main/audio/audio_io_stub.c
    ${CMAKE_CURRENT_LIST_DIR}/main/voice/voice_channel.c
    ${CMAKE_CURRENT_LIST_DIR}/main/voice/tts_player.c
    ${CMAKE_CURRENT_LIST_DIR}/main/voice/voice_wake_stub.c
    ${CMAKE_CURRENT_LIST_DIR}/main/channels/vector/mcp_client.c
    ${CMAKE_CURRENT_LIST_DIR}/main/channels/vector/vector_channel.c
    ${CMAKE_CURRENT_LIST_DIR}/main/channels/feishu/feishu_bot.c
    ${CMAKE_CURRENT_LIST_DIR}/main/channels/feishu/feishu_api.c
    ${CMAKE_CURRENT_LIST_DIR}/main/channels/feishu/feishu_http.c
    ${CMAKE_CURRENT_LIST_DIR}/main/channels/feishu/feishu_bot.c
    ${CMAKE_CURRENT_LIST_DIR}/main/channels/feishu/feishu_event_handler.c
    ${CMAKE_CURRENT_LIST_DIR}/main/channels/feishu/feishu_media.c
    ${CMAKE_CURRENT_LIST_DIR}/main/channels/feishu/feishu_message.c
    ${CMAKE_CURRENT_LIST_DIR}/main/channels/feishu/feishu_targets.c
    ${CMAKE_CURRENT_LIST_DIR}/main/channels/feishu/feishu_ws_transport.c
    ${CMAKE_CURRENT_LIST_DIR}/main/channels/feishu/feishu_ws_runtime.c
    ${CMAKE_CURRENT_LIST_DIR}/main/channels/feishu/feishu_ws_proto.c
    ${CMAKE_CURRENT_LIST_DIR}/main/vision/vision_capture_stub.c
    ${CMAKE_CURRENT_LIST_DIR}/third_party/cjson/cJSON.c
)

if(BUILD_FOR_MIPS)
    # MIPS 下替换音频/唤醒/图像抓拍实现，并加入 IMP 示例依赖
    list(REMOVE_ITEM SRC
        ${CMAKE_CURRENT_LIST_DIR}/main/audio/audio_io_stub.c
        ${CMAKE_CURRENT_LIST_DIR}/main/voice/voice_wake_stub.c
        ${CMAKE_CURRENT_LIST_DIR}/main/vision/vision_capture_stub.c
    )
    list(APPEND SRC
        ${CMAKE_CURRENT_LIST_DIR}/main/audio/audio_io_mips.c
        ${CMAKE_CURRENT_LIST_DIR}/main/voice/voice_wake_mips.c
        ${CMAKE_CURRENT_LIST_DIR}/main/vision/vision_capture_mips.c
        ${CMAKE_CURRENT_LIST_DIR}/third_party/media/samples/libimp-samples/common/sample-common.c
        ${CMAKE_CURRENT_LIST_DIR}/third_party/media/samples/libimp-samples/common/sample-common-framesource.c
        ${CMAKE_CURRENT_LIST_DIR}/third_party/media/samples/libimp-samples/common/sample-common-encoder.c
    )
endif()

add_executable(daima ${SRC})

target_include_directories(daima PRIVATE
    # 业务代码头文件
    ${CMAKE_CURRENT_LIST_DIR}/main
)

target_compile_definitions(daima PRIVATE
    # 编译宏：区分 Host 构建 & SPIFFS 目录
    DAIMA_HOST=1
    _GNU_SOURCE
)

if(BUILD_FOR_MIPS)
    target_compile_definitions(daima PRIVATE BUILD_FOR_MIPS=1)
endif()

if(BUILD_FOR_MIPS)
    # IMP 示例头文件仅 MIPS 需要
    target_include_directories(daima PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/third_party/media/samples/libimp-samples/common
    )
endif()

if(DAIMA_ENABLE_VISION)
    # 仅开启编译标志，具体功能取决于实现（Host 为空实现）
    target_compile_definitions(daima PRIVATE DAIMA_ENABLE_VISION=1)
    message(STATUS "Vision/Multimodal support enabled")
endif()

find_package(Threads REQUIRED)

if(LOCAL_THIRD_PARTY)
    # 交叉编译时优先从预编译目录加载依赖
    if(EXISTS "${MIPS_INCLUDE_DIR}")
        target_include_directories(daima PRIVATE ${MIPS_INCLUDE_DIR})
    endif()

    if(BUILD_FOR_MIPS AND EXISTS "${MIPS_MEDIA_INCLUDE_DIR}")
        target_include_directories(daima PRIVATE ${MIPS_MEDIA_INCLUDE_DIR})
    endif()

    # 静态库优先，其次 find_library 在指定路径内查找
    set(CURL_LIB "${MIPS_LIB_DIR}/libcurl.a")
    if(NOT EXISTS "${CURL_LIB}")
        find_library(CURL_LIB curl PATHS "${MIPS_LIB_DIR}" NO_DEFAULT_PATH)
    endif()

    set(CJSON_LIB "${MIPS_LIB_DIR}/libcjson.a")
    if(NOT EXISTS "${CJSON_LIB}")
        find_library(CJSON_LIB cjson PATHS "${MIPS_LIB_DIR}" NO_DEFAULT_PATH)
    endif()

    set(SSL_LIB "${MIPS_LIB_DIR}/libssl.a")
    if(NOT EXISTS "${SSL_LIB}")
        find_library(SSL_LIB ssl PATHS "${MIPS_LIB_DIR}" NO_DEFAULT_PATH)
    endif()

    set(CRYPTO_LIB "${MIPS_LIB_DIR}/libcrypto.a")
    if(NOT EXISTS "${CRYPTO_LIB}")
        find_library(CRYPTO_LIB crypto PATHS "${MIPS_LIB_DIR}" NO_DEFAULT_PATH)
    endif()

    # dl/atomic 可能是动态库或由工具链提供，找不到就回退到系统名
    find_library(DL_LIB dl PATHS "${MIPS_LIB_DIR}" NO_DEFAULT_PATH)
    if(NOT DL_LIB)
        set(DL_LIB dl)
    endif()

    find_library(ATOMIC_LIB atomic PATHS "${MIPS_LIB_DIR}" NO_DEFAULT_PATH)
    if(NOT ATOMIC_LIB)
        set(ATOMIC_LIB atomic)
    endif()

    if(BUILD_FOR_MIPS)
        set(IMP_LIB "${MIPS_MEDIA_LIB_DIR}/libimp.a")
        if(NOT EXISTS "${IMP_LIB}")
            find_library(IMP_LIB imp PATHS "${MIPS_MEDIA_LIB_DIR}" NO_DEFAULT_PATH)
        endif()

        set(SYSUTILS_LIB "${MIPS_MEDIA_LIB_DIR}/libsysutils.a")
        if(NOT EXISTS "${SYSUTILS_LIB}")
            find_library(SYSUTILS_LIB sysutils PATHS "${MIPS_MEDIA_LIB_DIR}" NO_DEFAULT_PATH)
        endif()

        set(ALOG_LIB "${MIPS_MEDIA_LIB_DIR}/libalog.a")
        if(NOT EXISTS "${ALOG_LIB}")
            find_library(ALOG_LIB alog PATHS "${MIPS_MEDIA_LIB_DIR}" NO_DEFAULT_PATH)
        endif()
    endif()
else()
    # Host 构建：直接使用系统库
    find_library(CURL_LIB curl)
    find_library(CJSON_LIB cjson)
    find_library(SSL_LIB ssl)
    find_library(CRYPTO_LIB crypto)
    find_library(DL_LIB dl)
    set(ATOMIC_LIB "")
    set(IMP_LIB "")
    set(SYSUTILS_LIB "")
    set(ALOG_LIB "")
endif()

if(BUILD_FOR_MIPS)
    set(LIB_HINT " (check ${MIPS_LIB_DIR})")
else()
    set(LIB_HINT "")
endif()

if (NOT CURL_LIB)
    # 依赖缺失时直接中止编译，避免运行期崩溃
    message(FATAL_ERROR "libcurl not found${LIB_HINT}")
endif()
if (NOT CJSON_LIB)
    # cJSON is compiled from third_party/cjson/cJSON.c
    message(STATUS "cJSON: using built-in source")
endif()
if (NOT SSL_LIB)
    message(FATAL_ERROR "libssl not found${LIB_HINT}")
endif()
if (NOT CRYPTO_LIB)
    message(FATAL_ERROR "libcrypto not found${LIB_HINT}")
endif()
if (NOT DL_LIB)
    # libdl is optional (built into libc on musl)
    message(STATUS "libdl not found (may be built into libc)")
endif()
if (BUILD_FOR_MIPS AND NOT ATOMIC_LIB)
    message(FATAL_ERROR "libatomic not found${LIB_HINT}")
endif()
if (BUILD_FOR_MIPS AND NOT IMP_LIB)
    message(FATAL_ERROR "libimp not found (check ${MIPS_MEDIA_LIB_DIR})")
endif()
if (BUILD_FOR_MIPS AND NOT SYSUTILS_LIB)
    message(FATAL_ERROR "libsysutils not found (check ${MIPS_MEDIA_LIB_DIR})")
endif()
if (BUILD_FOR_MIPS AND NOT ALOG_LIB)
    message(FATAL_ERROR "libalog not found (check ${MIPS_MEDIA_LIB_DIR})")
endif()


target_link_libraries(daima PRIVATE
    # 核心依赖（HTTP + JSON + TLS + 线程 + dl）
    ${CURL_LIB}
    ${SSL_LIB}
    ${CRYPTO_LIB}
    ${DL_LIB}
    Threads::Threads
)

if (ATOMIC_LIB)
    # 某些 MIPS 平台需要显式链接 libatomic
    target_link_libraries(daima PRIVATE ${ATOMIC_LIB})
endif()

if(BUILD_FOR_MIPS)
    # MIPS 媒体库与基础系统库
    target_link_libraries(daima PRIVATE
        ${IMP_LIB}
        ${SYSUTILS_LIB}
        ${ALOG_LIB}
        m
        rt
        stdc++
    )
endif()
