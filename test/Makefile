CC ?= cc
CFLAGS = -Wall -Wextra -std=c11 -O0 -g -I../main -I../third_party -D_GNU_SOURCE
LDLIBS = -lm

.PHONY: test test_pptx_generator clean

test: test_tts_player test_work_item test_webfetch test_pet_event test_apply_patch test_terminal test_runtime_config test_paths test_tool_protocol_guard test_files_tool test_cron_tool test_skills_tool test_tool_surface test_tool_invocation_context
	./test_tts_player
	./test_work_item
	./test_webfetch
	./test_pet_event
	./test_apply_patch
	./test_terminal
	./test_runtime_config
	./test_paths
	./test_tool_protocol_guard
	./test_files_tool
	./test_cron_tool
	./test_skills_tool
	./test_tool_surface
	./test_tool_invocation_context

test_pptx_generator:
	node ../spiffs_data/skills/pptx/scripts/test_generate_deck_smoke.js

test_tts_player: test_tts_player.c
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

test_work_item: test_work_item.c ../main/tools/tool_work_item.c ../main/work_items/work_item_store.c ../main/app/daima_paths.c ../main/app/daima_fs.c ../third_party/cjson/cJSON.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_webfetch: test_webfetch.c ../main/tools/tool_webfetch.c ../main/work_items/work_item_store.c ../main/app/daima_paths.c ../main/app/daima_fs.c ../main/host_http.c ../main/host_tls.c ../main/proxy/http_proxy_host.c ../main/daima_env.c ../main/daima_base64.c ../main/daima_text.c ../third_party/cjson/cJSON.c ../main/app/daima_log_file.c ../main/platform/daima_platform.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS) -lcurl -lssl -lcrypto

test_pet_event: test_pet_event.c ../main/pet/pet_event.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_apply_patch: test_apply_patch.c ../main/tools/tool_file_mutations.c ../main/tools/tool_file_paths.c ../main/tools/tool_file_ops.c ../main/tools/tool_file_checkpoint.c ../main/app/daima_paths.c ../main/app/daima_fs.c ../third_party/cjson/cJSON.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_terminal: test_terminal.c ../main/tools/tool_system.c ../main/tools/tool_terminal_exec.c ../main/app/daima_paths.c ../main/app/daima_fs.c ../main/app/runtime_config.c ../main/app/runtime_config_sections.c ../third_party/cjson/cJSON.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_runtime_config: test_runtime_config.c ../main/app/runtime_config.c ../main/app/runtime_config_sections.c ../main/app/daima_paths.c ../main/app/daima_fs.c ../third_party/cjson/cJSON.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_paths: test_paths.c ../main/app/daima_paths.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_tool_protocol_guard: test_tool_protocol_guard.c ../main/agent/tool_protocol_guard.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_files_tool: test_files_tool.c ../main/tools/tool_files.c ../main/tools/tool_file_read.c ../main/tools/tool_file_query.c ../main/tools/tool_file_list.c ../main/tools/tool_file_search.c ../main/tools/tool_file_mutations.c ../main/tools/tool_file_paths.c ../main/tools/tool_file_ops.c ../main/tools/tool_file_checkpoint.c ../main/app/daima_paths.c ../main/app/daima_fs.c ../main/app/daima_log_file.c ../main/daima_text.c ../main/platform/daima_platform.c ../third_party/cjson/cJSON.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_cron_tool: test_cron_tool.c ../main/tools/tool_cron.c ../main/cron/cron_service.c ../main/app/daima_paths.c ../main/app/daima_fs.c ../main/app/runtime_config.c ../main/app/runtime_config_sections.c ../main/app/daima_log_file.c ../main/bus/message_bus.c ../main/channels/feishu/feishu_targets.c ../main/daima_text.c ../main/platform/daima_os.c ../main/platform/daima_platform.c ../third_party/cjson/cJSON.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_skills_tool: test_skills_tool.c ../main/tools/tool_skills.c ../main/skills/skill_meta.c ../main/app/daima_paths.c ../main/app/daima_fs.c ../main/app/daima_log_file.c ../main/daima_text.c ../main/platform/daima_platform.c ../third_party/cjson/cJSON.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_tool_surface: test_tool_surface.c test_voice_stub.c ../main/tools/tool_registry.c ../main/tools/tool_files.c ../main/tools/tool_file_read.c ../main/tools/tool_file_query.c ../main/tools/tool_file_list.c ../main/tools/tool_file_search.c ../main/tools/tool_file_mutations.c ../main/tools/tool_file_paths.c ../main/tools/tool_file_ops.c ../main/tools/tool_file_checkpoint.c ../main/tools/tool_cron.c ../main/tools/tool_skills.c ../main/tools/tool_get_time_host.c ../main/tools/tool_weather_host.c ../main/tools/tool_system.c ../main/tools/tool_terminal_exec.c ../main/tools/tool_todo.c ../main/tools/tool_work_item.c ../main/tools/tool_webfetch.c ../main/tools/tool_daima_log.c ../main/tools/tool_session_search.c ../main/tools/tool_session_search_scan.c ../main/tools/tool_session_search_render.c ../main/tools/tool_vector_motion.c ../main/tools/tool_vector_body.c ../main/tools/tool_vector_audio.c ../main/tools/tool_vector_anim.c ../main/tools/tool_vector_status.c ../main/cron/cron_service.c ../main/skills/skill_meta.c ../main/app/daima_paths.c ../main/app/daima_fs.c ../main/app/runtime_config.c ../main/app/runtime_config_sections.c ../main/app/daima_log_file.c ../main/bus/message_bus.c ../main/channels/feishu/feishu_targets.c ../main/work_items/work_item_store.c ../main/memory/session_store.c ../main/memory/session_store_file.c ../main/memory/session_store_file_summary.c ../main/memory/session_store_file_facts.c ../main/host_http.c ../main/host_tls.c ../main/proxy/http_proxy_host.c ../main/daima_env.c ../main/daima_base64.c ../main/daima_text.c ../main/platform/daima_platform.c ../main/platform/daima_os.c ../main/channels/vector/mcp_client.c ../main/channels/vector/vector_channel.c ../third_party/cjson/cJSON.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS) -lcurl -lssl -lcrypto

test_tool_invocation_context: test_tool_invocation_context.c ../main/tools/tool_invocation_context.c ../main/platform/daima_platform.c ../main/app/daima_log_file.c ../main/app/daima_paths.c ../main/app/daima_fs.c ../third_party/cjson/cJSON.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

clean:
	rm -f test_tts_player test_work_item test_webfetch test_pet_event test_apply_patch test_terminal test_runtime_config test_paths test_tool_protocol_guard test_files_tool test_cron_tool test_skills_tool test_tool_surface test_tool_invocation_context
