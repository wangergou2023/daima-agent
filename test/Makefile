CC ?= cc
CFLAGS = -Wall -Wextra -std=c11 -O0 -g -I.. -I../include -I../init -I../kernel -I../kernel/time -I../ipc -I../lib -I../net -I../fs -I../third_party -D_GNU_SOURCE
LDLIBS = -lm

.PHONY: test test_pptx_generator clean

PRINTK_SRC = ../kernel/printk/printk.c ../lib/log_file.c
SAFE_EDIT_SRC = ../drivers/tool/tool_safe_edit.c
HASHLINE_SRC = ../drivers/tool/tool_hashline.c

test: test_kernel_headers test_tts_player test_work_item test_webfetch test_pet_event test_safe_edit test_hashline test_apply_patch test_terminal test_runtime_config test_llm_openai_payload test_llm_anthropic_payload test_llm_http_async test_llm_async_chat test_model_fallback test_session_history_reasoning test_paths test_rules_injection test_tool_protocol_guard test_agent_hooks test_intent_gate test_agent_roles test_agent_coordinator test_category_router test_plan_review test_prometheus_interview test_team_mode test_files_tool test_cron_tool test_skills_tool test_skill_summary test_context_skills_summary test_tool_surface test_skill_scoped_tools test_tool_invocation_context test_json_helpers test_daima_text test_session_file_common test_compaction_recovery test_session_recovery test_todo_enforcer test_ralph_loop
	./test_kernel_headers
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

test_kernel_headers: test_kernel_headers.c ../kernel/irq/irq.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_work_item: test_work_item.c ../drivers/tool/tool_work_item.c ../kernel/work_item.c ../lib/json_helpers.c ../fs/paths.c ../fs/fs.c ../third_party/cjson/cJSON.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_webfetch: test_webfetch.c ../drivers/tool/tool_webfetch.c ../kernel/work_item.c ../lib/json_helpers.c ../fs/paths.c ../fs/fs.c ../net/http.c ../kernel/cancel.c ../net/tls.c ../net/proxy.c ../lib/env.c ../lib/base64.c ../lib/text.c ../third_party/cjson/cJSON.c $(PRINTK_SRC) ../drivers/platform/platform.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS) -lcurl -lssl -lcrypto

test_pet_event: test_pet_event.c ../drivers/pet/pet_event.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_safe_edit: test_safe_edit.c $(SAFE_EDIT_SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_hashline: test_hashline.c $(HASHLINE_SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_apply_patch: test_apply_patch.c ../drivers/tool/tool_file_mutations.c ../drivers/tool/tool_file_paths.c ../drivers/tool/tool_file_ops.c ../drivers/tool/tool_file_checkpoint.c $(SAFE_EDIT_SRC) $(HASHLINE_SRC) ../fs/paths.c ../fs/fs.c ../third_party/cjson/cJSON.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_terminal: test_terminal.c ../drivers/tool/tool_system.c ../drivers/tool/tool_terminal_exec.c ../fs/paths.c ../fs/fs.c ../kernel/runtime.c ../kernel/runtime_sections.c ../third_party/cjson/cJSON.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_runtime_config: test_runtime_config.c ../kernel/runtime.c ../kernel/runtime_sections.c ../fs/paths.c ../fs/fs.c ../third_party/cjson/cJSON.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_deepseek_thinking_runtime: test_deepseek_thinking_runtime.c ../kernel/runtime.c ../kernel/runtime_sections.c ../fs/paths.c ../fs/fs.c ../third_party/cjson/cJSON.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_llm_openai_payload: test_llm_openai_payload.c ../drivers/llm/llm_openai_payload.c ../third_party/cjson/cJSON.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_llm_anthropic_payload: test_llm_anthropic_payload.c ../drivers/llm/llm_anthropic_payload.c ../third_party/cjson/cJSON.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_llm_http_async: test_llm_http_async.c ../arch/host/llm_http_client_host.c ../net/http.c ../kernel/cancel.c ../net/tls.c ../net/proxy.c ../lib/env.c ../lib/base64.c ../fs/paths.c ../fs/fs.c $(PRINTK_SRC) ../drivers/platform/platform.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS) -lcurl -lssl -lcrypto

test_llm_async_chat: test_llm_async_chat.c ../arch/host/llm_proxy_host.c ../arch/host/llm_http_client_host.c ../drivers/llm/llm_openai_payload.c ../drivers/llm/llm_anthropic_payload.c ../kernel/runtime.c ../kernel/runtime_sections.c ../fs/paths.c ../fs/fs.c $(PRINTK_SRC) ../net/http.c ../kernel/cancel.c ../net/tls.c ../net/proxy.c ../lib/env.c ../lib/base64.c ../lib/text.c ../lib/json_helpers.c ../drivers/platform/platform.c ../third_party/cjson/cJSON.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS) -lcurl -lssl -lcrypto

test_model_fallback: test_model_fallback.c ../drivers/llm/model_fallback.c ../fs/paths.c ../kernel/runtime.c ../kernel/runtime_sections.c ../fs/fs.c ../third_party/cjson/cJSON.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_session_history_reasoning: test_session_history_reasoning.c ../drivers/memory/session_store.c ../drivers/memory/session_store_file.c ../drivers/memory/session_store_file_facts.c ../drivers/memory/session_store_file_summary.c ../drivers/memory/session_store_file_common.c ../fs/paths.c ../fs/fs.c ../kernel/runtime.c ../kernel/runtime_sections.c ../third_party/cjson/cJSON.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_paths: test_paths.c ../fs/paths.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_rules_injection: test_rules_injection.c ../kernel/rules.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_tool_protocol_guard: test_tool_protocol_guard.c ../kernel/tool_guard.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_agent_hooks: test_agent_hooks.c ../kernel/hooks.c
	$(CC) $(CFLAGS) -DAGENT_HOOKS_TEST_RESET=1 -o $@ $^ $(LDLIBS)

test_intent_gate: test_intent_gate.c ../kernel/intent.c ../lib/env.c
	$(CC) $(CFLAGS) -DINTENT_GATE_LLM_FALLBACK=0 -o $@ $^ $(LDLIBS)

test_agent_roles: test_agent_roles.c ../kernel/roles.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_agent_coordinator: test_agent_coordinator.c ../kernel/sched/core.c ../kernel/sched/class.c ../kernel/sched/agent.c ../kernel/roles.c ../third_party/cjson/cJSON.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)


test_category_router: test_category_router.c ../kernel/router.c ../kernel/roles.c ../fs/paths.c ../fs/fs.c $(PRINTK_SRC) ../kernel/runtime.c ../kernel/runtime_sections.c ../drivers/platform/platform.c ../third_party/cjson/cJSON.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_plan_review: test_plan_review.c ../kernel/plan.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_prometheus_interview: test_prometheus_interview.c ../kernel/interview.c ../third_party/cjson/cJSON.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_team_mode: test_team_mode.c ../kernel/team.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_files_tool: test_files_tool.c ../drivers/tool/tool_files.c ../drivers/tool/tool_file_read.c ../drivers/tool/tool_file_query.c ../drivers/tool/tool_file_list.c ../drivers/tool/tool_file_search.c ../drivers/tool/tool_file_mutations.c ../drivers/tool/tool_file_paths.c ../drivers/tool/tool_file_ops.c ../drivers/tool/tool_file_checkpoint.c $(SAFE_EDIT_SRC) $(HASHLINE_SRC) ../fs/paths.c ../fs/fs.c $(PRINTK_SRC) ../lib/text.c ../drivers/platform/platform.c ../third_party/cjson/cJSON.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_cron_tool: test_cron_tool.c ../drivers/tool/tool_cron.c ../kernel/time/timer.c ../fs/paths.c ../fs/fs.c ../kernel/runtime.c ../kernel/runtime_sections.c $(PRINTK_SRC) ../ipc/bus.c ../drivers/channel/feishu/feishu_targets.c ../drivers/channel/feishu/feishu_http.c ../lib/json_helpers.c ../lib/text.c ../drivers/platform/os.c ../drivers/platform/platform.c ../net/http.c ../kernel/cancel.c ../net/tls.c ../net/proxy.c ../lib/env.c ../lib/base64.c ../third_party/cjson/cJSON.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS) -lcurl -lssl -lcrypto

test_skills_tool: test_skills_tool.c ../drivers/tool/tool_skills.c ../drivers/skill/skill_meta.c ../fs/paths.c ../fs/fs.c $(PRINTK_SRC) ../lib/text.c ../drivers/platform/platform.c ../third_party/cjson/cJSON.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_skill_summary: test_skill_summary.c ../drivers/skill/skill_loader.c ../drivers/skill/skill_meta.c ../fs/paths.c ../fs/fs.c $(PRINTK_SRC) ../lib/text.c ../drivers/platform/platform.c ../third_party/cjson/cJSON.c
	$(CC) $(CFLAGS) -DSKILL_SCOPED_TOOLS_ENABLED=0 -o $@ $^ $(LDLIBS)

test_context_skills_summary: test_context_skills_summary.c ../kernel/context_build.c ../drivers/skill/skill_loader.c ../drivers/skill/skill_meta.c ../drivers/memory/memory_store.c ../fs/paths.c ../fs/fs.c $(PRINTK_SRC) ../lib/text.c ../drivers/platform/platform.c ../third_party/cjson/cJSON.c
	$(CC) $(CFLAGS) -DSKILL_SCOPED_TOOLS_ENABLED=0 -o $@ $^ $(LDLIBS)

test_tool_surface: test_tool_surface.c test_voice_stub.c ../drivers/tool/tool_registry.c ../drivers/tool/tool_files.c ../drivers/tool/tool_file_read.c ../drivers/tool/tool_file_query.c ../drivers/tool/tool_file_list.c ../drivers/tool/tool_file_search.c ../drivers/tool/tool_file_mutations.c ../drivers/tool/tool_file_paths.c ../drivers/tool/tool_file_ops.c ../drivers/tool/tool_file_checkpoint.c $(SAFE_EDIT_SRC) $(HASHLINE_SRC) ../drivers/tool/tool_cron.c ../drivers/tool/tool_skills.c ../drivers/tool/tool_get_time_host.c ../drivers/tool/tool_weather_host.c ../drivers/tool/tool_system.c ../drivers/tool/tool_terminal_exec.c ../drivers/tool/tool_todo.c ../drivers/tool/tool_work_item.c ../drivers/tool/tool_webfetch.c ../drivers/tool/tool_log.c ../drivers/tool/tool_session_search.c ../drivers/tool/tool_session_search_scan.c ../drivers/tool/tool_session_search_render.c ../drivers/tool/tool_vector_motion.c ../drivers/tool/tool_vector_body.c ../drivers/tool/tool_vector_audio.c ../drivers/tool/tool_vector_anim.c ../drivers/tool/tool_vector_status.c ../kernel/time/timer.c ../drivers/skill/skill_meta.c ../fs/paths.c ../fs/fs.c ../kernel/runtime.c ../kernel/runtime_sections.c $(PRINTK_SRC) ../ipc/bus.c ../drivers/channel/feishu/feishu_targets.c ../drivers/channel/feishu/feishu_http.c ../kernel/work_item.c ../lib/json_helpers.c ../drivers/memory/session_store.c ../drivers/memory/session_store_file.c ../drivers/memory/session_store_file_summary.c ../drivers/memory/session_store_file_facts.c ../drivers/memory/session_store_file_common.c ../net/http.c ../kernel/cancel.c ../net/tls.c ../net/proxy.c ../lib/env.c ../lib/base64.c ../lib/text.c ../drivers/platform/platform.c ../drivers/platform/os.c ../drivers/channel/vector/mcp_client.c ../drivers/channel/vector/vector_channel.c ../third_party/cjson/cJSON.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS) -lcurl -lssl -lcrypto

test_skill_scoped_tools: test_skill_scoped_tools.c test_voice_stub.c ../drivers/skill/skill_tools.c ../drivers/tool/tool_registry.c ../drivers/tool/tool_files.c ../drivers/tool/tool_file_read.c ../drivers/tool/tool_file_query.c ../drivers/tool/tool_file_list.c ../drivers/tool/tool_file_search.c ../drivers/tool/tool_file_mutations.c ../drivers/tool/tool_file_paths.c ../drivers/tool/tool_file_ops.c ../drivers/tool/tool_file_checkpoint.c $(SAFE_EDIT_SRC) $(HASHLINE_SRC) ../drivers/tool/tool_cron.c ../drivers/tool/tool_skills.c ../drivers/tool/tool_get_time_host.c ../drivers/tool/tool_weather_host.c ../drivers/tool/tool_system.c ../drivers/tool/tool_terminal_exec.c ../drivers/tool/tool_todo.c ../drivers/tool/tool_work_item.c ../drivers/tool/tool_webfetch.c ../drivers/tool/tool_log.c ../drivers/tool/tool_session_search.c ../drivers/tool/tool_session_search_scan.c ../drivers/tool/tool_session_search_render.c ../drivers/tool/tool_vector_motion.c ../drivers/tool/tool_vector_body.c ../drivers/tool/tool_vector_audio.c ../drivers/tool/tool_vector_anim.c ../drivers/tool/tool_vector_status.c ../kernel/time/timer.c ../drivers/skill/skill_meta.c ../fs/paths.c ../fs/fs.c ../kernel/runtime.c ../kernel/runtime_sections.c $(PRINTK_SRC) ../ipc/bus.c ../drivers/channel/feishu/feishu_targets.c ../drivers/channel/feishu/feishu_http.c ../kernel/work_item.c ../lib/json_helpers.c ../drivers/memory/session_store.c ../drivers/memory/session_store_file.c ../drivers/memory/session_store_file_summary.c ../drivers/memory/session_store_file_facts.c ../drivers/memory/session_store_file_common.c ../net/http.c ../kernel/cancel.c ../net/tls.c ../net/proxy.c ../lib/env.c ../lib/base64.c ../lib/text.c ../drivers/platform/platform.c ../drivers/platform/os.c ../drivers/channel/vector/mcp_client.c ../drivers/channel/vector/vector_channel.c ../third_party/cjson/cJSON.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS) -lcurl -lssl -lcrypto

test_tool_invocation_context: test_tool_invocation_context.c ../drivers/tool/tool_invocation_context.c ../drivers/platform/platform.c $(PRINTK_SRC) ../fs/paths.c ../fs/fs.c ../third_party/cjson/cJSON.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_json_helpers: test_json_helpers.c ../lib/json_helpers.c ../third_party/cjson/cJSON.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_daima_text: test_daima_text.c ../lib/text.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_session_file_common: test_session_file_common.c ../drivers/memory/session_store_file_common.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_compaction_recovery: test_compaction_recovery.c ../kernel/compaction.c ../drivers/memory/session_store.c ../drivers/memory/session_store_file.c ../drivers/memory/session_store_file_facts.c ../drivers/memory/session_store_file_summary.c ../drivers/memory/session_store_file_common.c ../fs/paths.c ../fs/fs.c ../kernel/runtime.c ../kernel/runtime_sections.c $(PRINTK_SRC) ../drivers/platform/platform.c ../third_party/cjson/cJSON.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_session_recovery: test_session_recovery.c ../kernel/recovery.c ../drivers/memory/session_store.c ../drivers/memory/session_store_file.c ../drivers/memory/session_store_file_facts.c ../drivers/memory/session_store_file_summary.c ../drivers/memory/session_store_file_common.c ../fs/paths.c ../fs/fs.c ../kernel/runtime.c ../kernel/runtime_sections.c $(PRINTK_SRC) ../drivers/platform/platform.c ../third_party/cjson/cJSON.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_todo_enforcer: test_todo_enforcer.c ../kernel/todo.c ../fs/paths.c ../third_party/cjson/cJSON.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_ralph_loop: test_ralph_loop.c ../kernel/ralph.c ../fs/paths.c ../third_party/cjson/cJSON.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

clean:
	rm -f test_kernel_headers test_tts_player test_work_item test_webfetch test_pet_event test_safe_edit test_hashline test_apply_patch test_terminal test_runtime_config test_deepseek_thinking_runtime test_llm_openai_payload test_llm_anthropic_payload test_llm_http_async test_llm_async_chat test_model_fallback test_session_history_reasoning test_paths test_rules_injection test_tool_protocol_guard test_agent_hooks test_intent_gate test_agent_roles test_agent_coordinator test_category_router test_plan_review test_prometheus_interview test_team_mode test_files_tool test_cron_tool test_skills_tool test_skill_summary test_context_skills_summary test_tool_surface test_skill_scoped_tools test_tool_invocation_context test_json_helpers test_daima_text test_session_file_common test_compaction_recovery test_session_recovery test_todo_enforcer test_ralph_loop
