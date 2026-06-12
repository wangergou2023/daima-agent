CC ?= cc
CFLAGS = -Wall -Wextra -std=c11 -O0 -g -I../main -I../third_party -D_GNU_SOURCE
LDLIBS = -lm

.PHONY: test test_pptx_generator clean

SAFE_EDIT_SRC = ../main/tools/tool_safe_edit.c
HASHLINE_SRC = ../main/tools/tool_hashline.c

test: test_tts_player test_work_item test_webfetch test_pet_event test_safe_edit test_hashline test_apply_patch test_terminal test_runtime_config test_llm_openai_payload test_llm_anthropic_payload test_llm_http_async test_llm_async_chat test_model_fallback test_session_history_reasoning test_paths test_rules_injection test_tool_protocol_guard test_intent_gate test_agent_roles test_agent_coordinator test_agent_status_push test_category_router test_plan_review test_prometheus_interview test_team_mode test_files_tool test_cron_tool test_skills_tool test_skill_summary test_context_skills_summary test_tool_surface test_skill_scoped_tools test_tool_invocation_context test_json_helpers test_daima_text test_session_file_common test_compaction_recovery test_session_recovery test_todo_enforcer test_ralph_loop
	./test_tts_player
	./test_work_item
	./test_webfetch
	./test_pet_event
	./test_safe_edit
	./test_hashline
	./test_apply_patch
	./test_terminal
	./test_runtime_config
	./test_llm_openai_payload
	./test_llm_anthropic_payload
	./test_llm_http_async
	./test_llm_async_chat
	./test_model_fallback
	./test_session_history_reasoning
	./test_paths
	./test_rules_injection
	./test_tool_protocol_guard
	./test_intent_gate
	./test_agent_roles
	./test_agent_coordinator
	./test_agent_status_push
	./test_category_router
	./test_plan_review
	./test_prometheus_interview
	./test_team_mode
	./test_files_tool
	./test_cron_tool
	./test_skills_tool
	./test_skill_summary
	./test_context_skills_summary
	./test_tool_surface
	./test_skill_scoped_tools
	./test_tool_invocation_context
	./test_json_helpers
	./test_daima_text
	./test_session_file_common
	./test_compaction_recovery
	./test_session_recovery
	./test_todo_enforcer
	./test_ralph_loop

test_pptx_generator:
	node ../spiffs_data/skills/pptx/scripts/test_generate_deck_smoke.js

test_tts_player: test_tts_player.c
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

test_work_item: test_work_item.c ../main/tools/tool_work_item.c ../main/work_items/work_item_store.c ../main/utils/json_helpers.c ../main/app/daima_paths.c ../main/app/daima_fs.c ../third_party/cjson/cJSON.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_webfetch: test_webfetch.c ../main/tools/tool_webfetch.c ../main/work_items/work_item_store.c ../main/utils/json_helpers.c ../main/app/daima_paths.c ../main/app/daima_fs.c ../main/host_http.c ../main/agent/agent_cancel.c ../main/host_tls.c ../main/proxy/http_proxy_host.c ../main/daima_env.c ../main/daima_base64.c ../main/daima_text.c ../third_party/cjson/cJSON.c ../main/app/daima_log_file.c ../main/platform/daima_platform.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS) -lcurl -lssl -lcrypto

test_pet_event: test_pet_event.c ../main/pet/pet_event.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_safe_edit: test_safe_edit.c $(SAFE_EDIT_SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_hashline: test_hashline.c $(HASHLINE_SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_apply_patch: test_apply_patch.c ../main/tools/tool_file_mutations.c ../main/tools/tool_file_paths.c ../main/tools/tool_file_ops.c ../main/tools/tool_file_checkpoint.c $(SAFE_EDIT_SRC) $(HASHLINE_SRC) ../main/app/daima_paths.c ../main/app/daima_fs.c ../third_party/cjson/cJSON.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_terminal: test_terminal.c ../main/tools/tool_system.c ../main/tools/tool_terminal_exec.c ../main/app/daima_paths.c ../main/app/daima_fs.c ../main/app/runtime_config.c ../main/app/runtime_config_sections.c ../third_party/cjson/cJSON.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_runtime_config: test_runtime_config.c ../main/app/runtime_config.c ../main/app/runtime_config_sections.c ../main/app/daima_paths.c ../main/app/daima_fs.c ../third_party/cjson/cJSON.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_deepseek_thinking_runtime: test_deepseek_thinking_runtime.c ../main/app/runtime_config.c ../main/app/runtime_config_sections.c ../main/app/daima_paths.c ../main/app/daima_fs.c ../third_party/cjson/cJSON.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_llm_openai_payload: test_llm_openai_payload.c ../main/llm/llm_openai_payload.c ../third_party/cjson/cJSON.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_llm_anthropic_payload: test_llm_anthropic_payload.c ../main/llm/llm_anthropic_payload.c ../third_party/cjson/cJSON.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_llm_http_async: test_llm_http_async.c ../main/llm/llm_http_client_host.c ../main/host_http.c ../main/agent/agent_cancel.c ../main/host_tls.c ../main/proxy/http_proxy_host.c ../main/daima_env.c ../main/daima_base64.c ../main/app/daima_paths.c ../main/app/daima_fs.c ../main/app/daima_log_file.c ../main/platform/daima_platform.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS) -lcurl -lssl -lcrypto

test_llm_async_chat: test_llm_async_chat.c ../main/llm/llm_proxy_host.c ../main/llm/llm_http_client_host.c ../main/llm/llm_openai_payload.c ../main/llm/llm_anthropic_payload.c ../main/app/runtime_config.c ../main/app/runtime_config_sections.c ../main/app/daima_paths.c ../main/app/daima_fs.c ../main/app/daima_log_file.c ../main/host_http.c ../main/agent/agent_cancel.c ../main/host_tls.c ../main/proxy/http_proxy_host.c ../main/daima_env.c ../main/daima_base64.c ../main/daima_text.c ../main/utils/json_helpers.c ../main/platform/daima_platform.c ../third_party/cjson/cJSON.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS) -lcurl -lssl -lcrypto

test_model_fallback: test_model_fallback.c ../main/llm/model_fallback.c ../main/app/daima_paths.c ../main/app/runtime_config.c ../main/app/runtime_config_sections.c ../main/app/daima_fs.c ../main/app/daima_log_file.c ../third_party/cjson/cJSON.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_session_history_reasoning: test_session_history_reasoning.c ../main/memory/session_store.c ../main/memory/session_store_file.c ../main/memory/session_store_file_facts.c ../main/memory/session_store_file_summary.c ../main/memory/session_store_file_common.c ../main/app/daima_paths.c ../main/app/daima_fs.c ../main/app/runtime_config.c ../main/app/runtime_config_sections.c ../third_party/cjson/cJSON.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_paths: test_paths.c ../main/app/daima_paths.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_rules_injection: test_rules_injection.c ../main/agent/rules_injection.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_tool_protocol_guard: test_tool_protocol_guard.c ../main/agent/tool_protocol_guard.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_intent_gate: test_intent_gate.c ../main/agent/intent_gate.c ../main/daima_env.c
	$(CC) $(CFLAGS) -DDAIMA_INTENT_GATE_LLM_FALLBACK=0 -o $@ $^ $(LDLIBS)

test_agent_roles: test_agent_roles.c ../main/agent/agent_roles.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_agent_coordinator: test_agent_coordinator.c ../main/agent/agent_coordinator.c ../main/agent/agent_roles.c ../third_party/cjson/cJSON.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_agent_status_push: test_agent_status_push.c ../main/agent/agent_status_push.c ../main/agent/agent_roles.c ../third_party/cjson/cJSON.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_category_router: test_category_router.c ../main/agent/category_router.c ../main/agent/agent_roles.c ../main/app/daima_paths.c ../main/app/daima_fs.c ../main/app/daima_log_file.c ../main/app/runtime_config.c ../main/app/runtime_config_sections.c ../main/platform/daima_platform.c ../third_party/cjson/cJSON.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_plan_review: test_plan_review.c ../main/agent/plan_review.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_prometheus_interview: test_prometheus_interview.c ../main/agent/prometheus_interview.c ../third_party/cjson/cJSON.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_team_mode: test_team_mode.c ../main/agent/team_mode.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_files_tool: test_files_tool.c ../main/tools/tool_files.c ../main/tools/tool_file_read.c ../main/tools/tool_file_query.c ../main/tools/tool_file_list.c ../main/tools/tool_file_search.c ../main/tools/tool_file_mutations.c ../main/tools/tool_file_paths.c ../main/tools/tool_file_ops.c ../main/tools/tool_file_checkpoint.c $(SAFE_EDIT_SRC) $(HASHLINE_SRC) ../main/app/daima_paths.c ../main/app/daima_fs.c ../main/app/daima_log_file.c ../main/daima_text.c ../main/platform/daima_platform.c ../third_party/cjson/cJSON.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_cron_tool: test_cron_tool.c ../main/tools/tool_cron.c ../main/cron/cron_service.c ../main/app/daima_paths.c ../main/app/daima_fs.c ../main/app/runtime_config.c ../main/app/runtime_config_sections.c ../main/app/daima_log_file.c ../main/bus/message_bus.c ../main/channels/feishu/feishu_targets.c ../main/channels/feishu/feishu_http.c ../main/utils/json_helpers.c ../main/daima_text.c ../main/platform/daima_os.c ../main/platform/daima_platform.c ../main/host_http.c ../main/agent/agent_cancel.c ../main/host_tls.c ../main/proxy/http_proxy_host.c ../main/daima_env.c ../main/daima_base64.c ../third_party/cjson/cJSON.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS) -lcurl -lssl -lcrypto

test_skills_tool: test_skills_tool.c ../main/tools/tool_skills.c ../main/skills/skill_meta.c ../main/app/daima_paths.c ../main/app/daima_fs.c ../main/app/daima_log_file.c ../main/daima_text.c ../main/platform/daima_platform.c ../third_party/cjson/cJSON.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_skill_summary: test_skill_summary.c ../main/skills/skill_loader.c ../main/skills/skill_meta.c ../main/app/daima_paths.c ../main/app/daima_fs.c ../main/app/daima_log_file.c ../main/daima_text.c ../main/platform/daima_platform.c ../third_party/cjson/cJSON.c
	$(CC) $(CFLAGS) -DDAIMA_SKILL_SCOPED_TOOLS_ENABLED=0 -o $@ $^ $(LDLIBS)

test_context_skills_summary: test_context_skills_summary.c ../main/agent/context_builder.c ../main/skills/skill_loader.c ../main/skills/skill_meta.c ../main/memory/memory_store.c ../main/app/daima_paths.c ../main/app/daima_fs.c ../main/app/daima_log_file.c ../main/daima_text.c ../main/platform/daima_platform.c ../third_party/cjson/cJSON.c
	$(CC) $(CFLAGS) -DDAIMA_SKILL_SCOPED_TOOLS_ENABLED=0 -o $@ $^ $(LDLIBS)

test_tool_surface: test_tool_surface.c test_voice_stub.c ../main/tools/tool_registry.c ../main/tools/tool_files.c ../main/tools/tool_file_read.c ../main/tools/tool_file_query.c ../main/tools/tool_file_list.c ../main/tools/tool_file_search.c ../main/tools/tool_file_mutations.c ../main/tools/tool_file_paths.c ../main/tools/tool_file_ops.c ../main/tools/tool_file_checkpoint.c $(SAFE_EDIT_SRC) $(HASHLINE_SRC) ../main/tools/tool_cron.c ../main/tools/tool_skills.c ../main/tools/tool_get_time_host.c ../main/tools/tool_weather_host.c ../main/tools/tool_system.c ../main/tools/tool_terminal_exec.c ../main/tools/tool_todo.c ../main/tools/tool_work_item.c ../main/tools/tool_webfetch.c ../main/tools/tool_daima_log.c ../main/tools/tool_session_search.c ../main/tools/tool_session_search_scan.c ../main/tools/tool_session_search_render.c ../main/tools/tool_vector_motion.c ../main/tools/tool_vector_body.c ../main/tools/tool_vector_audio.c ../main/tools/tool_vector_anim.c ../main/tools/tool_vector_status.c ../main/cron/cron_service.c ../main/skills/skill_meta.c ../main/app/daima_paths.c ../main/app/daima_fs.c ../main/app/runtime_config.c ../main/app/runtime_config_sections.c ../main/app/daima_log_file.c ../main/bus/message_bus.c ../main/channels/feishu/feishu_targets.c ../main/channels/feishu/feishu_http.c ../main/work_items/work_item_store.c ../main/utils/json_helpers.c ../main/memory/session_store.c ../main/memory/session_store_file.c ../main/memory/session_store_file_summary.c ../main/memory/session_store_file_facts.c ../main/memory/session_store_file_common.c ../main/host_http.c ../main/agent/agent_cancel.c ../main/host_tls.c ../main/proxy/http_proxy_host.c ../main/daima_env.c ../main/daima_base64.c ../main/daima_text.c ../main/platform/daima_platform.c ../main/platform/daima_os.c ../main/channels/vector/mcp_client.c ../main/channels/vector/vector_channel.c ../third_party/cjson/cJSON.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS) -lcurl -lssl -lcrypto

test_skill_scoped_tools: test_skill_scoped_tools.c test_voice_stub.c ../main/skills/skill_tools.c ../main/tools/tool_registry.c ../main/tools/tool_files.c ../main/tools/tool_file_read.c ../main/tools/tool_file_query.c ../main/tools/tool_file_list.c ../main/tools/tool_file_search.c ../main/tools/tool_file_mutations.c ../main/tools/tool_file_paths.c ../main/tools/tool_file_ops.c ../main/tools/tool_file_checkpoint.c $(SAFE_EDIT_SRC) $(HASHLINE_SRC) ../main/tools/tool_cron.c ../main/tools/tool_skills.c ../main/tools/tool_get_time_host.c ../main/tools/tool_weather_host.c ../main/tools/tool_system.c ../main/tools/tool_terminal_exec.c ../main/tools/tool_todo.c ../main/tools/tool_work_item.c ../main/tools/tool_webfetch.c ../main/tools/tool_daima_log.c ../main/tools/tool_session_search.c ../main/tools/tool_session_search_scan.c ../main/tools/tool_session_search_render.c ../main/tools/tool_vector_motion.c ../main/tools/tool_vector_body.c ../main/tools/tool_vector_audio.c ../main/tools/tool_vector_anim.c ../main/tools/tool_vector_status.c ../main/cron/cron_service.c ../main/skills/skill_meta.c ../main/app/daima_paths.c ../main/app/daima_fs.c ../main/app/runtime_config.c ../main/app/runtime_config_sections.c ../main/app/daima_log_file.c ../main/bus/message_bus.c ../main/channels/feishu/feishu_targets.c ../main/channels/feishu/feishu_http.c ../main/work_items/work_item_store.c ../main/utils/json_helpers.c ../main/memory/session_store.c ../main/memory/session_store_file.c ../main/memory/session_store_file_summary.c ../main/memory/session_store_file_facts.c ../main/memory/session_store_file_common.c ../main/host_http.c ../main/agent/agent_cancel.c ../main/host_tls.c ../main/proxy/http_proxy_host.c ../main/daima_env.c ../main/daima_base64.c ../main/daima_text.c ../main/platform/daima_platform.c ../main/platform/daima_os.c ../main/channels/vector/mcp_client.c ../main/channels/vector/vector_channel.c ../third_party/cjson/cJSON.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS) -lcurl -lssl -lcrypto

test_tool_invocation_context: test_tool_invocation_context.c ../main/tools/tool_invocation_context.c ../main/platform/daima_platform.c ../main/app/daima_log_file.c ../main/app/daima_paths.c ../main/app/daima_fs.c ../third_party/cjson/cJSON.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_json_helpers: test_json_helpers.c ../main/utils/json_helpers.c ../third_party/cjson/cJSON.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_daima_text: test_daima_text.c ../main/daima_text.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_session_file_common: test_session_file_common.c ../main/memory/session_store_file_common.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_compaction_recovery: test_compaction_recovery.c ../main/agent/compaction_recovery.c ../main/memory/session_store.c ../main/memory/session_store_file.c ../main/memory/session_store_file_facts.c ../main/memory/session_store_file_summary.c ../main/memory/session_store_file_common.c ../main/app/daima_paths.c ../main/app/daima_fs.c ../main/app/runtime_config.c ../main/app/runtime_config_sections.c ../main/app/daima_log_file.c ../main/platform/daima_platform.c ../third_party/cjson/cJSON.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_session_recovery: test_session_recovery.c ../main/agent/session_recovery.c ../main/memory/session_store.c ../main/memory/session_store_file.c ../main/memory/session_store_file_facts.c ../main/memory/session_store_file_summary.c ../main/memory/session_store_file_common.c ../main/app/daima_paths.c ../main/app/daima_fs.c ../main/app/runtime_config.c ../main/app/runtime_config_sections.c ../main/app/daima_log_file.c ../main/platform/daima_platform.c ../third_party/cjson/cJSON.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_todo_enforcer: test_todo_enforcer.c ../main/agent/todo_enforcer.c ../main/app/daima_paths.c ../third_party/cjson/cJSON.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_ralph_loop: test_ralph_loop.c ../main/agent/ralph_loop.c ../main/app/daima_paths.c ../third_party/cjson/cJSON.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

clean:
	rm -f test_tts_player test_work_item test_webfetch test_pet_event test_safe_edit test_hashline test_apply_patch test_terminal test_runtime_config test_deepseek_thinking_runtime test_llm_openai_payload test_llm_anthropic_payload test_llm_http_async test_llm_async_chat test_model_fallback test_session_history_reasoning test_paths test_rules_injection test_tool_protocol_guard test_intent_gate test_agent_roles test_agent_coordinator test_agent_status_push test_category_router test_plan_review test_prometheus_interview test_team_mode test_files_tool test_cron_tool test_skills_tool test_skill_summary test_context_skills_summary test_tool_surface test_skill_scoped_tools test_tool_invocation_context test_json_helpers test_daima_text test_session_file_common test_compaction_recovery test_session_recovery test_todo_enforcer test_ralph_loop
