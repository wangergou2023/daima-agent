#!/usr/bin/env node
const fs = require('fs');
const path = require('path');
const vm = require('vm');

const ROOT = path.resolve(__dirname, '..', '..');
const KERNEL_SCOPE_PATH = path.join(ROOT, 'kernel');
const CORE_PATH = path.join(ROOT, 'spiffs_data/web/subagent-state-core.js');
const SELECTORS_PATH = path.join(ROOT, 'spiffs_data/web/subagent-state-selectors.js');
const REDUCER_PATH = path.join(ROOT, 'spiffs_data/web/subagent-state-reducer.js');

function fail(message) {
  console.error(`state-reducer-check failed: ${message}`);
  process.exit(1);
}

function expect(condition, message) {
  if (!condition) fail(message);
}

function loadStateModules() {
  const context = {
    console,
    window: {},
    globalThis: {},
    setTimeout,
    clearTimeout,
    setInterval,
    clearInterval,
    Date,
    Math,
  };
  context.window = context;
  context.globalThis = context;
  vm.createContext(context);
  for (const file of [CORE_PATH, SELECTORS_PATH, REDUCER_PATH]) {
    vm.runInContext(fs.readFileSync(file, 'utf8'), context);
  }
  return {
    core: context.AgentSubagentStateCore,
    selectors: context.AgentSubagentStateSelectors,
    reducer: context.AgentSubagentStateReducer,
  };
}

function reduce(stateApi, state, action) {
  return stateApi.reducer.reduceSubagentUiEvent(state, action, {
    normalizeCoordinatorPayload: stateApi.core.normalizeCoordinatorPayload,
  });
}

function makeSubagentEvent(payload) {
  return {
    kind: 'subagent_event',
    payload,
    key: stateApi.core.subagentEventKey(payload),
    entry: {
      type: String(payload.type || '').trim(),
      status: String(payload.status || '').trim(),
      task: String(payload.task || '').trim(),
      detail: String(payload.detail || '').trim(),
      blocker_kind: String(payload.blocker_kind || '').trim(),
      blocker_text: String(payload.blocker_text || '').trim(),
      text: String(payload.detail || payload.task || '').trim(),
      ts: Number(payload.ts) || Date.now(),
    },
  };
}

const stateApi = loadStateModules();

let state = stateApi.core.createEmptySubagentUiState();

state = reduce(stateApi, state, makeSubagentEvent({
  type: 'subagent_blocked',
  chat_id: 'web_test',
  task_id: 'dt_a',
  session_id: 'delegate_sync_a',
  coordinator_id: 'dc_ui',
  subagent_type: 'explore',
  status: 'blocked',
  task: '探索 kernel',
  detail: 'Waiting for sudo approval',
  output: 'kernel summary from event',
  scope_path: KERNEL_SCOPE_PATH,
  scope_kind: 'subsystem',
  analysis_focus: 'execution_kernel',
  blocker_kind: 'permission',
  blocker_text: 'Waiting for sudo approval',
  blocker_scope: 'task',
  ts: 1,
}));

state = reduce(stateApi, state, {
  kind: 'coordinator',
  payload: {
    coordinator_id: 'dc_ui',
    team_run_id: 'tr_ui',
    team_name: 'repo-map-team',
    dispatch_mode: 'staged',
    chat_id: 'web_test',
    status: 'running',
    agent_count: 1,
    completed_count: 0,
    queued_count: 0,
    running_count: 1,
    visible_revision: 1,
    completion_notified: false,
    parent_response_sent: true,
    wake_state: 'dispatched',
    agents: [
      {
        name: '探索 kernel',
        task_id: 'dt_a',
        session_id: 'delegate_sync_a',
        subagent_type: 'explore',
        description: '探索 kernel',
        status: 'blocked',
        model: 'deepseek-v4-pro',
        scope_path: KERNEL_SCOPE_PATH,
        scope_kind: 'subsystem',
        analysis_focus: 'execution_kernel',
        elapsed_ms: 1234,
        output: '',
        blocker_kind: 'permission',
        blocker_text: 'Waiting for sudo approval',
        child_session: {
          summary: 'waiting on sudo approval',
          history: [
            {
              role: 'assistant',
              content: 'child snapshot assistant before blocker',
              reasoning: 'child snapshot reasoning before blocker',
            },
          ],
          frames: [
            {
              type: 'subagent_blocked',
              phase: 'blocked',
              status: 'blocked',
              task: '探索 kernel',
              detail: 'Need sudo approval from child session snapshot',
              blocker_kind: 'permission',
              blocker_text: 'Need sudo approval from child session snapshot',
              ts: 2,
            },
          ],
          commits: [
            {
              kind: 'blocker',
              phase: 'blocked',
              status: 'blocked',
              label: '探索 kernel',
              text: 'Need sudo approval from child session snapshot',
              ts: 2,
            },
          ],
          pending_queue: {
            permissions: ['Need sudo approval from child session snapshot'],
            questions: [],
          },
        },
      },
    ],
  },
});

state = reduce(stateApi, state, makeSubagentEvent({
  type: 'subagent_unblocked',
  chat_id: 'web_test',
  task_id: 'dt_a',
  session_id: 'delegate_sync_a',
  coordinator_id: 'dc_ui',
  subagent_type: 'explore',
  status: 'running',
  task: '探索 kernel',
  detail: 'sudo granted',
  output: 'kernel summary after unblock',
  scope_path: KERNEL_SCOPE_PATH,
  scope_kind: 'subsystem',
  analysis_focus: 'execution_kernel',
  blocker_scope: 'task',
  ts: 10,
}));

state = reduce(stateApi, state, makeSubagentEvent({
  type: 'subagent_done',
  chat_id: 'web_test',
  task_id: 'dt_a',
  session_id: 'delegate_sync_a',
  coordinator_id: 'dc_ui',
  subagent_type: 'explore',
  status: 'done',
  task: '探索 kernel',
  detail: 'model=deepseek-v4-pro · elapsed_ms=2200',
  output: 'kernel final summary',
  scope_path: KERNEL_SCOPE_PATH,
  scope_kind: 'subsystem',
  analysis_focus: 'execution_kernel',
  blocker_scope: 'task',
  ts: 20,
}));

state = reduce(stateApi, state, {
  kind: 'coordinator',
  payload: {
    coordinator_id: 'dc_ui',
    team_run_id: 'tr_ui',
    team_name: 'repo-map-team',
    dispatch_mode: 'staged',
    chat_id: 'web_test',
    status: 'done',
    agent_count: 1,
    completed_count: 1,
    queued_count: 0,
    running_count: 0,
    visible_revision: 99,
    completion_notified: true,
    parent_response_sent: true,
    parent_resume_enqueued: true,
    wake_state: 'completed',
    agents: [
      {
        name: '探索 kernel',
        task_id: 'dt_a',
        session_id: 'delegate_sync_a',
        subagent_type: 'explore',
        description: '探索 kernel',
        status: 'done',
        model: 'deepseek-v4-pro',
        scope_path: KERNEL_SCOPE_PATH,
        scope_kind: 'subsystem',
        analysis_focus: 'execution_kernel',
        elapsed_ms: 2200,
        output: 'kernel final summary',
        child_session: {
          summary: 'kernel final summary',
          history: [{ role: 'assistant', content: 'kernel resumed after manual close' }],
          frames: [
            {
              type: 'subagent_done',
              phase: 'done',
              status: 'done',
              task: '探索 kernel',
              detail: 'kernel done after reopen',
              output_preview: 'kernel final summary',
              ts: 100,
            },
          ],
          commits: [
            {
              kind: 'result',
              phase: 'done',
              status: 'done',
              label: '探索 kernel',
              text: 'kernel final summary',
              ts: 100,
            },
          ],
          pending_queue: { permissions: [], questions: [] },
        },
      },
    ],
  },
});

state = reduce(stateApi, state, makeSubagentEvent({
  type: 'subagent_progress',
  chat_id: 'web_test',
  coordinator_id: 'dc_ui',
  task_id: 'dt_a',
  session_id: 'delegate_sync_a',
  subagent_type: 'explore',
  status: 'running',
  task: '探索 kernel',
  detail: 'live cursor advance',
  visible_revision: 123,
  ts: 110,
}));

state = reduce(stateApi, state, {
  kind: 'coordinator',
  payload: {
    coordinator_id: 'dc_ui',
    team_run_id: 'tr_ui',
    team_name: 'repo-map-team',
    dispatch_mode: 'staged',
    chat_id: 'web_test',
    status: 'running',
    agent_count: 1,
    completed_count: 0,
    queued_count: 0,
    running_count: 1,
    visible_revision: 2,
    completion_notified: false,
    parent_response_sent: true,
    wake_state: 'dispatched',
    agents: [
      {
        name: '探索 kernel',
        task_id: 'dt_a',
        session_id: 'delegate_sync_a',
        subagent_type: 'explore',
        description: '探索 kernel',
        status: 'running',
        model: 'deepseek-v4-pro',
        scope_path: KERNEL_SCOPE_PATH,
        scope_kind: 'subsystem',
        analysis_focus: 'execution_kernel',
        elapsed_ms: 1235,
        output: '',
        blocker_kind: 'permission',
        blocker_text: 'Need sudo approval from stale coordinator snapshot',
        child_session: {
          summary: 'stale blocked snapshot',
          history: [
            {
              role: 'assistant',
              content: 'stale coordinator child history',
              reasoning: 'stale coordinator reasoning',
            },
          ],
          frames: [
            {
              type: 'subagent_blocked',
              phase: 'blocked',
              status: 'blocked',
              task: '探索 kernel',
              detail: 'stale coordinator blocked frame',
              blocker_kind: 'permission',
              blocker_text: 'Need sudo approval from stale coordinator snapshot',
              ts: 1,
            },
          ],
          commits: [
            {
              kind: 'blocker',
              phase: 'blocked',
              status: 'blocked',
              label: '探索 kernel',
              text: 'stale coordinator blocked frame',
              ts: 1,
            },
          ],
          pending_queue: {
            permissions: ['Need sudo approval from stale coordinator snapshot'],
            questions: [],
          },
        },
      },
    ],
  },
});

state = reduce(stateApi, state, makeSubagentEvent({
  type: 'subagent_progress',
  chat_id: 'web_test',
  coordinator_id: 'dc_ui',
  task_id: 'dt_a',
  session_id: 'delegate_sync_a',
  subagent_type: 'explore',
  status: 'running',
  task: '探索 kernel',
  detail: 'live cursor advance',
  visible_revision: 123,
  ts: 123,
}));

const detail = state.details.get('dt_a');
expect(!!detail, 'expected dt_a detail to exist');
expect((Number(state.liveCursor?.visibleRevision) || 0) === 123, 'expected live cursor to retain newer revision');
expect(String(detail.status || '') === 'done', 'expected stale coordinator snapshot not to downgrade done detail');
expect(String(detail.output || '').includes('kernel final summary'), 'expected stale coordinator snapshot not to erase final output');
expect(String(detail.session_summary || '').includes('kernel final summary'), 'expected stale coordinator snapshot not to erase session summary');
expect(!String(detail.blocker_text || '').includes('stale coordinator'), 'expected stale coordinator snapshot not to revive stale blocker text');
expect((detail.pending_queue?.permissions?.length || 0) === 0, 'expected stale coordinator snapshot not to revive resolved permission queue');
expect(!String(detail.pending_request?.prompt || '').includes('stale coordinator'), 'expected stale coordinator snapshot not to revive resolved pending request');
expect(
  Array.isArray(detail.timeline) && !detail.timeline.some((item) => String(item?.detail || '').includes('stale coordinator blocked frame')),
  'expected stale coordinator snapshot not to replace newer timeline with stale blocked frame',
);

console.log('state-reducer-check ok');
