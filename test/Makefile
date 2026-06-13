CC ?= cc
CFLAGS = -Wall -Wextra -std=c11 -O0 -g -I../daima -I../daima/include -I../third_party -D_GNU_SOURCE
LDLIBS = -lm

.PHONY: test test_pptx_generator clean

SAFE_EDIT_SRC = ../daima/drivers/tool/tool_safe_edit.c
HASHLINE_SRC = ../daima/drivers/tool/tool_hashline.c

test: test_tts_player test_work_item test_webfetch test_pet_event test_safe_edit test_hashline test_apply_patch test_terminal test_runtime_config test_llm_openai_payload test_llm_anthropic_payload test_llm_http_async test_llm_async_chat test_model_fallback test_session_history_reasoning test_paths test_rules_injection test_tool_protocol_guard test_agent_hooks test_intent_gate test_agent_roles test_agent_coordinator test_category_router test_plan_review test_prometheus_interview test_team_mode test_files_tool test_cron_tool test_skills_tool test_skill_summary test_context_skills_summary test_tool_surface test_skill_scoped_tools test_tool_invocation_context test_json_helpers test_daima_text test_session_file_common test_compaction_recovery test_session_recovery test_todo_enforcer test_ralph_loop
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
	./test_agent_hooks
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

test_work_item: test_work_item.c ../daima/drivers/tool/tool_work_item.c ../daima/core/work_item.c ../daima/core/utils/json_helpers.c ../daima/core/paths.c ../daima/core/fs.c ../third_party/cjson/cJSON.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_webfetch: test_webfetch.c ../daima/drivers/tool/tool_webfetch.c ../daima/core/work_item.c ../daima/core/utils/json_helpers.c ../daima/core/paths.c ../daima/core/fs.c ../daima/core/http.c ../daima/core/agent_cancel.c ../daima/core/tls.c ../daima/core/proxy.c ../daima/core/env.c ../daima/core/base64.c ../daima/core/text.c ../third_party/cjson/cJSON.c ../daima/core/log_file.c ../daima/drivers/platform/platform.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS) -lcurl -lssl -lcrypto

test_pet_event: test_pet_event.c ../daima/drivers/pet/pet_event.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_safe_edit: test_safe_edit.c $(SAFE_EDIT_SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_hashline: test_hashline.c $(HASHLINE_SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_apply_patch: test_apply_patch.c ../daima/drivers/tool/tool_file_mutations.c ../daima/drivers/tool/tool_file_paths.c ../daima/drivers/tool/tool_file_ops.c ../daima/drivers/tool/tool_file_checkpoint.c $(SAFE_EDIT_SRC) $(HASHLINE_SRC) ../daima/core/paths.c ../daima/core/fs.c ../third_party/cjson/cJSON.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_terminal: test_terminal.c ../daima/drivers/tool/tool_system.c ../daima/drivers/tool/tool_terminal_exec.c ../daima/core/paths.c ../daima/core/fs.c ../daima/core/runtime.c ../daima/core/runtime_sections.c ../third_party/cjson/cJSON.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_runtime_config: test_runtime_config.c ../daima/core/runtime.c ../daima/core/runtime_sections.c ../daima/core/paths.c ../daima/core/fs.c ../third_party/cjson/cJSON.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_deepseek_thinking_runtime: test_deepseek_thinking_runtime.c ../daima/core/runtime.c ../daima/core/runtime_sections.c ../daima/core/paths.c ../daima/core/fs.c ../third_party/cjson/cJSON.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_llm_openai_payload: test_llm_openai_payload.c ../daima/drivers/llm/llm_openai_payload.c ../third_party/cjson/cJSON.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_llm_anthropic_payload: test_llm_anthropic_payload.c ../daima/drivers/llm/llm_anthropic_payload.c ../third_party/cjson/cJSON.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_llm_http_async: test_llm_http_async.c ../daima/arch/host/llm_http_client_host.c ../daima/core/http.c ../daima/core/agent_cancel.c ../daima/core/tls.c ../daima/core/proxy.c ../daima/core/env.c ../daima/core/base64.c ../daima/core/paths.c ../daima/core/fs.c ../daima/core/log_file.c ../daima/drivers/platform/platform.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS) -lcurl -lssl -lcrypto

test_llm_async_chat: test_llm_async_chat.c ../daima/arch/host/llm_proxy_host.c ../daima/arch/host/llm_http_client_host.c ../daima/drivers/llm/llm_openai_payload.c ../daima/drivers/llm/llm_anthropic_payload.c ../daima/core/runtime.c ../daima/core/runtime_sections.c ../daima/core/paths.c ../daima/core/fs.c ../daima/core/log_file.c ../daima/core/http.c ../daima/core/agent_cancel.c ../daima/core/tls.c ../daima/core/proxy.c ../daima/core/env.c ../daima/core/base64.c ../daima/core/text.c ../daima/core/utils/json_helpers.c ../daima/drivers/platform/platform.c ../third_party/cjson/cJSON.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS) -lcurl -lssl -lcrypto

test_model_fallback: test_model_fallback.c ../daima/drivers/llm/model_fallback.c ../daima/core/paths.c ../daima/core/runtime.c ../daima/core/runtime_sections.c ../daima/core/fs.c ../daima/core/log_file.c ../third_party/cjson/cJSON.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_session_history_reasoning: test_session_history_reasoning.c ../daima/drivers/memory/session_store.c ../daima/drivers/memory/session_store_file.c ../daima/drivers/memory/session_store_file_facts.c ../daima/drivers/memory/session_store_file_summary.c ../daima/drivers/memory/session_store_file_common.c ../daima/core/paths.c ../daima/core/fs.c ../daima/core/runtime.c ../daima/core/runtime_sections.c ../third_party/cjson/cJSON.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_paths: test_paths.c ../daima/core/paths.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_rules_injection: test_rules_injection.c ../daima/core/rules_injection.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_tool_protocol_guard: test_tool_protocol_guard.c ../daima/core/tool_protocol_guard.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_agent_hooks: test_agent_hooks.c ../daima/core/hooks.c
	$(CC) $(CFLAGS) -DDAIMA_AGENT_HOOKS_TEST_RESET=1 -o $@ $^ $(LDLIBS)

test_intent_gate: test_intent_gate.c ../daima/core/intent_gate.c ../daima/core/env.c
	$(CC) $(CFLAGS) -DDAIMA_INTENT_GATE_LLM_FALLBACK=0 -o $@ $^ $(LDLIBS)

test_agent_roles: test_agent_roles.c ../daima/core/roles.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_agent_coordinator: test_agent_coordinator.c ../daima/core/agent_coordinator.c ../daima/core/roles.c ../third_party/cjson/cJSON.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_agent_status_push: test_agent_status_push.c ../daima/core/agent_status_push.c ../daima/core/roles.c ../third_party/cjson/cJSON.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_category_router: test_category_router.c ../daima/core/category_router.c ../daima/core/roles.c ../daima/core/paths.c ../daima/core/fs.c ../daima/core/log_file.c ../daima/core/runtime.c ../daima/core/runtime_sections.c ../daima/drivers/platform/platform.c ../third_party/cjson/cJSON.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_plan_review: test_plan_review.c ../daima/core/plan_review.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_prometheus_interview: test_prometheus_interview.c ../daima/core/prometheus_interview.c ../third_party/cjson/cJSON.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_team_mode: test_team_mode.c ../daima/core/team_mode.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_files_tool: test_files_tool.c ../daima/drivers/tool/tool_files.c ../daima/drivers/tool/tool_file_read.c ../daima/drivers/tool/tool_file_query.c ../daima/drivers/tool/tool_file_list.c ../daima/drivers/tool/tool_file_search.c ../daima/drivers/tool/tool_file_mutations.c ../daima/drivers/tool/tool_file_paths.c ../daima/drivers/tool/tool_file_ops.c ../daima/drivers/tool/tool_file_checkpoint.c $(SAFE_EDIT_SRC) $(HASHLINE_SRC) ../daima/core/paths.c ../daima/core/fs.c ../daima/core/log_file.c ../daima/core/text.c ../daima/drivers/platform/platform.c ../third_party/cjson/cJSON.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_cron_tool: test_cron_tool.c ../daima/drivers/tool/tool_cron.c ../daima/core/cron.c ../daima/core/paths.c ../daima/core/fs.c ../daima/core/runtime.c ../daima/core/runtime_sections.c ../daima/core/log_file.c ../daima/core/bus.c ../daima/drivers/channel/feishu/feishu_targets.c ../daima/drivers/channel/feishu/feishu_http.c ../daima/core/utils/json_helpers.c ../daima/core/text.c ../daima/drivers/platform/daima_os.c ../daima/drivers/platform/platform.c ../daima/core/http.c ../daima/core/agent_cancel.c ../daima/core/tls.c ../daima/core/proxy.c ../daima/core/env.c ../daima/core/base64.c ../third_party/cjson/cJSON.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS) -lcurl -lssl -lcrypto

test_skills_tool: test_skills_tool.c ../daima/drivers/tool/tool_skills.c ../daima/drivers/skill/skill_meta.c ../daima/core/paths.c ../daima/core/fs.c ../daima/core/log_file.c ../daima/core/text.c ../daima/drivers/platform/platform.c ../third_party/cjson/cJSON.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_skill_summary: test_skill_summary.c ../daima/drivers/skill/skill_loader.c ../daima/drivers/skill/skill_meta.c ../daima/core/paths.c ../daima/core/fs.c ../daima/core/log_file.c ../daima/core/text.c ../daima/drivers/platform/platform.c ../third_party/cjson/cJSON.c
	$(CC) $(CFLAGS) -DDAIMA_SKILL_SCOPED_TOOLS_ENABLED=0 -o $@ $^ $(LDLIBS)

test_context_skills_summary: test_context_skills_summary.c ../daima/core/context_builder.c ../daima/drivers/skill/skill_loader.c ../daima/drivers/skill/skill_meta.c ../daima/drivers/memory/memory_store.c ../daima/core/paths.c ../daima/core/fs.c ../daima/core/log_file.c ../daima/core/text.c ../daima/drivers/platform/platform.c ../third_party/cjson/cJSON.c
	$(CC) $(CFLAGS) -DDAIMA_SKILL_SCOPED_TOOLS_ENABLED=0 -o $@ $^ $(LDLIBS)

test_tool_surface: test_tool_surface.c test_voice_stub.c ../daima/drivers/tool/tool_registry.c ../daima/drivers/tool/tool_files.c ../daima/drivers/tool/tool_file_read.c ../daima/drivers/tool/tool_file_query.c ../daima/drivers/tool/tool_file_list.c ../daima/drivers/tool/tool_file_search.c ../daima/drivers/tool/tool_file_mutations.c ../daima/drivers/tool/tool_file_paths.c ../daima/drivers/tool/tool_file_ops.c ../daima/drivers/tool/tool_file_checkpoint.c $(SAFE_EDIT_SRC) $(HASHLINE_SRC) ../daima/drivers/tool/tool_cron.c ../daima/drivers/tool/tool_skills.c ../daima/drivers/tool/tool_get_time_host.c ../daima/drivers/tool/tool_weather_host.c ../daima/drivers/tool/tool_system.c ../daima/drivers/tool/tool_terminal_exec.c ../daima/drivers/tool/tool_todo.c ../daima/drivers/tool/tool_work_item.c ../daima/drivers/tool/tool_webfetch.c ../daima/drivers/tool/tool_daima_log.c ../daima/drivers/tool/tool_session_search.c ../daima/drivers/tool/tool_session_search_scan.c ../daima/drivers/tool/tool_session_search_render.c ../daima/drivers/tool/tool_vector_motion.c ../daima/drivers/tool/tool_vector_body.c ../daima/drivers/tool/tool_vector_audio.c ../daima/drivers/tool/tool_vector_anim.c ../daima/drivers/tool/tool_vector_status.c ../daima/core/cron.c ../daima/drivers/skill/skill_meta.c ../daima/core/paths.c ../daima/core/fs.c ../daima/core/runtime.c ../daima/core/runtime_sections.c ../daima/core/log_file.c ../daima/core/bus.c ../daima/drivers/channel/feishu/feishu_targets.c ../daima/drivers/channel/feishu/feishu_http.c ../daima/core/work_item.c ../daima/core/utils/json_helpers.c ../daima/drivers/memory/session_store.c ../daima/drivers/memory/session_store_file.c ../daima/drivers/memory/session_store_file_summary.c ../daima/drivers/memory/session_store_file_facts.c ../daima/drivers/memory/session_store_file_common.c ../daima/core/http.c ../daima/core/agent_cancel.c ../daima/core/tls.c ../daima/core/proxy.c ../daima/core/env.c ../daima/core/base64.c ../daima/core/text.c ../daima/drivers/platform/platform.c ../daima/drivers/platform/daima_os.c ../daima/drivers/channel/vector/mcp_client.c ../daima/drivers/channel/vector/vector_channel.c ../third_party/cjson/cJSON.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS) -lcurl -lssl -lcrypto

test_skill_scoped_tools: test_skill_scoped_tools.c test_voice_stub.c ../daima/drivers/skill/skill_tools.c ../daima/drivers/tool/tool_registry.c ../daima/drivers/tool/tool_files.c ../daima/drivers/tool/tool_file_read.c ../daima/drivers/tool/tool_file_query.c ../daima/drivers/tool/tool_file_list.c ../daima/drivers/tool/tool_file_search.c ../daima/drivers/tool/tool_file_mutations.c ../daima/drivers/tool/tool_file_paths.c ../daima/drivers/tool/tool_file_ops.c ../daima/drivers/tool/tool_file_checkpoint.c $(SAFE_EDIT_SRC) $(HASHLINE_SRC) ../daima/drivers/tool/tool_cron.c ../daima/drivers/tool/tool_skills.c ../daima/drivers/tool/tool_get_time_host.c ../daima/drivers/tool/tool_weather_host.c ../daima/drivers/tool/tool_system.c ../daima/drivers/tool/tool_terminal_exec.c ../daima/drivers/tool/tool_todo.c ../daima/drivers/tool/tool_work_item.c ../daima/drivers/tool/tool_webfetch.c ../daima/drivers/tool/tool_daima_log.c ../daima/drivers/tool/tool_session_search.c ../daima/drivers/tool/tool_session_search_scan.c ../daima/drivers/tool/tool_session_search_render.c ../daima/drivers/tool/tool_vector_motion.c ../daima/drivers/tool/tool_vector_body.c ../daima/drivers/tool/tool_vector_audio.c ../daima/drivers/tool/tool_vector_anim.c ../daima/drivers/tool/tool_vector_status.c ../daima/core/cron.c ../daima/drivers/skill/skill_meta.c ../daima/core/paths.c ../daima/core/fs.c ../daima/core/runtime.c ../daima/core/runtime_sections.c ../daima/core/log_file.c ../daima/core/bus.c ../daima/drivers/channel/feishu/feishu_targets.c ../daima/drivers/channel/feishu/feishu_http.c ../daima/core/work_item.c ../daima/core/utils/json_helpers.c ../daima/drivers/memory/session_store.c ../daima/drivers/memory/session_store_file.c ../daima/drivers/memory/session_store_file_summary.c ../daima/drivers/memory/session_store_file_facts.c ../daima/drivers/memory/session_store_file_common.c ../daima/core/http.c ../daima/core/agent_cancel.c ../daima/core/tls.c ../daima/core/proxy.c ../daima/core/env.c ../daima/core/base64.c ../daima/core/text.c ../daima/drivers/platform/platform.c ../daima/drivers/platform/daima_os.c ../daima/drivers/channel/vector/mcp_client.c ../daima/drivers/channel/vector/vector_channel.c ../third_party/cjson/cJSON.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS) -lcurl -lssl -lcrypto

test_tool_invocation_context: test_tool_invocation_context.c ../daima/drivers/tool/tool_invocation_context.c ../daima/drivers/platform/platform.c ../daima/core/log_file.c ../daima/core/paths.c ../daima/core/fs.c ../third_party/cjson/cJSON.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_json_helpers: test_json_helpers.c ../daima/core/utils/json_helpers.c ../third_party/cjson/cJSON.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_daima_text: test_daima_text.c ../daima/core/text.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_session_file_common: test_session_file_common.c ../daima/drivers/memory/session_store_file_common.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_compaction_recovery: test_compaction_recovery.c ../daima/core/compaction_recovery.c ../daima/drivers/memory/session_store.c ../daima/drivers/memory/session_store_file.c ../daima/drivers/memory/session_store_file_facts.c ../daima/drivers/memory/session_store_file_summary.c ../daima/drivers/memory/session_store_file_common.c ../daima/core/paths.c ../daima/core/fs.c ../daima/core/runtime.c ../daima/core/runtime_sections.c ../daima/core/log_file.c ../daima/drivers/platform/platform.c ../third_party/cjson/cJSON.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_session_recovery: test_session_recovery.c ../daima/core/session_recovery.c ../daima/drivers/memory/session_store.c ../daima/drivers/memory/session_store_file.c ../daima/drivers/memory/session_store_file_facts.c ../daima/drivers/memory/session_store_file_summary.c ../daima/drivers/memory/session_store_file_common.c ../daima/core/paths.c ../daima/core/fs.c ../daima/core/runtime.c ../daima/core/runtime_sections.c ../daima/core/log_file.c ../daima/drivers/platform/platform.c ../third_party/cjson/cJSON.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_todo_enforcer: test_todo_enforcer.c ../daima/core/todo_enforcer.c ../daima/core/paths.c ../third_party/cjson/cJSON.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_ralph_loop: test_ralph_loop.c ../daima/core/ralph_loop.c ../daima/core/paths.c ../third_party/cjson/cJSON.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

clean:
	rm -f test_tts_player test_work_item test_webfetch test_pet_event test_safe_edit test_hashline test_apply_patch test_terminal test_runtime_config test_deepseek_thinking_runtime test_llm_openai_payload test_llm_anthropic_payload test_llm_http_async test_llm_async_chat test_model_fallback test_session_history_reasoning test_paths test_rules_injection test_tool_protocol_guard test_agent_hooks test_intent_gate test_agent_roles test_agent_coordinator test_category_router test_plan_review test_prometheus_interview test_team_mode test_files_tool test_cron_tool test_skills_tool test_skill_summary test_context_skills_summary test_tool_surface test_skill_scoped_tools test_tool_invocation_context test_json_helpers test_daima_text test_session_file_common test_compaction_recovery test_session_recovery test_todo_enforcer test_ralph_loop
