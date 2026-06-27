#!/usr/bin/env node
const fs = require('fs');
const vm = require('vm');
let JSDOM;
try {
  ({ JSDOM } = require('jsdom'));
} catch (_) {
  console.error('ui-check failed: missing `jsdom`. Install it first, for example:');
  console.error('  npm install jsdom');
  process.exit(1);
}

const ROOT = '/home/wangergou/code/github/daima-agent';
const HTML_PATH = `${ROOT}/spiffs_data/web/index.html`;
const SUBAGENT_STATE_CORE_JS_PATH = `${ROOT}/spiffs_data/web/subagent-state-core.js`;
const SUBAGENT_STATE_SELECTORS_JS_PATH = `${ROOT}/spiffs_data/web/subagent-state-selectors.js`;
const SUBAGENT_STATE_REDUCER_JS_PATH = `${ROOT}/spiffs_data/web/subagent-state-reducer.js`;
const SUBAGENT_STATE_JS_PATH = `${ROOT}/spiffs_data/web/subagent-state.js`;
const SUBAGENT_RUNTIME_JS_PATH = `${ROOT}/spiffs_data/web/subagent-runtime.js`;
const SUBAGENT_APP_BRIDGE_JS_PATH = `${ROOT}/spiffs_data/web/subagent-app-bridge.js`;
const SUBAGENT_PANEL_CONTROLLER_JS_PATH = `${ROOT}/spiffs_data/web/subagent-panel-controller.js`;
const SUBAGENT_DETAIL_VIEW_JS_PATH = `${ROOT}/spiffs_data/web/subagent-detail-view.js`;
const SUBAGENT_EVENT_ADAPTER_JS_PATH = `${ROOT}/spiffs_data/web/subagent-event-adapter.js`;
const SUBAGENT_INTERACTIVE_CONTROLLER_JS_PATH = `${ROOT}/spiffs_data/web/subagent-interactive-controller.js`;
const SUBAGENT_COORDINATOR_VIEW_JS_PATH = `${ROOT}/spiffs_data/web/subagent-coordinator-view.js`;
const SUBAGENT_COORDINATOR_CONTROLLER_JS_PATH = `${ROOT}/spiffs_data/web/subagent-coordinator-controller.js`;
const SUBAGENT_UI_ORCHESTRATOR_JS_PATH = `${ROOT}/spiffs_data/web/subagent-ui-orchestrator.js`;
const SUBAGENT_TRANSPORT_JS_PATH = `${ROOT}/spiffs_data/web/subagent-transport.js`;
const SUBAGENT_CHAT_TRANSPORT_JS_PATH = `${ROOT}/spiffs_data/web/subagent-chat-transport.js`;
const SUBAGENT_BOOTSTRAP_JS_PATH = `${ROOT}/spiffs_data/web/subagent-bootstrap.js`;
const APP_JS_PATH = `${ROOT}/spiffs_data/web/app.js`;

function fail(message) {
  console.error(`ui-check failed: ${message}`);
  process.exit(1);
}

function makeHistoryEntries(prefix, count) {
  return Array.from({ length: count }, (_, index) => ({
    id: `${prefix.replace(/\s+/g, '-')}-hist-${index + 1}`,
    seq: index + 1,
    role: index % 2 === 0 ? 'assistant' : 'user',
    content: `${prefix} history ${String(index).padStart(2, '0')}`,
    ...(index % 2 === 0 ? { reasoning: `${prefix} reasoning ${String(index).padStart(2, '0')}` } : {}),
  }));
}

function buildDom() {
  const html = fs.readFileSync(HTML_PATH, 'utf8')
    .replace(/<script src="\/pet\.js"><\/script>/g, '')
    .replace(/<script src="\/app\.js"><\/script>/g, '')
    .replace(/<script>[\s\S]*?<\/script>/g, '');

  const dom = new JSDOM(html, {
    url: 'http://127.0.0.1:1234/',
    pretendToBeVisual: true,
    runScripts: 'outside-only',
  });

  const { window } = dom;
  const { HTMLElement } = window;
  if (!HTMLElement.prototype.scrollTo) HTMLElement.prototype.scrollTo = function() {};
  if (!HTMLElement.prototype.scrollIntoView) HTMLElement.prototype.scrollIntoView = function() {};
  return dom;
}

function createFetchStub() {
  const requests = [];
  const requestOptions = [];
  let sessionHistoryData = { messages: [] };
  let sessionHistoryQueue = [];
  let snapshotData = {
    chat_id: 'web_test',
    interactive: {
      blockers: [
        {
          chat_id: 'web_test',
          task_id: 'dt_boot_a',
          session_id: 'delegate_sync_boot_a',
          coordinator_id: 'dc_bootstrap',
          request_type: 'sudo_password',
          request_id: 'sudo_req_boot',
          prompt: 'Need sudo approval from bootstrap snapshot',
          blocker_kind: 'permission',
          label: '分析 kernel',
        },
      ],
    },
    coordinators: [
      {
        coordinator_id: 'dc_bootstrap',
        chat_id: 'web_test',
        team_run_id: 'tr_bootstrap',
        team_name: 'bootstrap-team',
        dispatch_mode: 'parallel',
        status: 'done',
        agent_count: 2,
        completed_count: 2,
        running_count: 0,
        queued_count: 0,
        blocked_count: 0,
        failed_count: 0,
        effective_output_count: 2,
        visible_revision: 7,
        replay_cursor: {
          visible_revision: 7,
        },
        completion_notified: true,
        parent_response_sent: true,
        parent_resume_enqueued: true,
        wake_state: 'completed',
        wake_retry_count: 0,
        wake_last_attempt_ms: 11,
        wake_last_success_ms: 12,
        agents: [
          {
            name: '分析 kernel',
            task_id: 'dt_boot_a',
            session_id: 'delegate_sync_boot_a',
            subagent_type: 'explore',
            description: '分析 kernel',
            task_key: 'bootstrap-kernel',
            status: 'done',
            model: 'deepseek-v4-pro',
            scope_path: '/home/wangergou/code/github/daima-agent/kernel',
            scope_kind: 'subsystem',
            analysis_focus: 'execution_kernel',
            elapsed_ms: 2300,
            output: 'kernel bootstrap final summary',
            target_files: 'kernel/loop.c,kernel/turn/turn_run.c',
          write_approved: true,
          parent_response_sent: true,
          parent_resume_enqueued: true,
          coordinator_status: 'done',
          coordinator_wake_state: 'completed',
          wake_retry_count: 0,
          pending_request: {
            request_type: 'sudo_password',
            request_id: 'sudo_req_boot',
            prompt: 'Need sudo approval from bootstrap snapshot',
          },
          child_session: {
            summary: 'kernel bootstrap final summary',
            history: makeHistoryEntries('kernel bootstrap', 20),
              frames: [
                {
                  id: 'kernel-bootstrap-frame-preflight',
                  seq: 1,
                  type: 'subagent_step',
                  phase: 'progress',
                  status: 'running',
                  task: '分析 kernel',
                  detail: 'preflight terminal',
                  output_preview: 'Need sudo approval from bootstrap snapshot',
                  blocker_kind: 'tool',
                  ts: 1,
                },
                {
                  id: 'kernel-bootstrap-frame-start',
                  seq: 2,
                  type: 'subagent_start',
                  phase: 'start',
                  status: 'running',
                  task: '分析 kernel',
                  detail: 'bootstrap start',
                  ts: 1,
                },
                {
                  id: 'kernel-bootstrap-frame-done',
                  seq: 3,
                  type: 'subagent_done',
                  phase: 'done',
                  status: 'done',
                  task: '分析 kernel',
                  detail: 'bootstrap done',
                  output_preview: 'kernel bootstrap final summary',
                  ts: 2,
                },
              ],
              commits: [
                {
                  id: 'kernel-bootstrap-commit-preflight',
                  seq: 1,
                  kind: 'tool',
                  phase: 'running',
                  status: 'running',
                  label: '分析 kernel',
                  text: 'preflight terminal',
                  ts: 1,
                },
                {
                  id: 'kernel-bootstrap-commit-start',
                  seq: 2,
                  kind: 'start',
                  phase: 'running',
                  status: 'running',
                  label: '分析 kernel',
                  text: 'bootstrap start',
                  ts: 1,
                },
                {
                  id: 'kernel-bootstrap-commit-result',
                  seq: 3,
                  kind: 'result',
                  phase: 'done',
                  status: 'done',
                  label: '分析 kernel',
                  text: 'kernel bootstrap final summary',
                  ts: 2,
                },
              ],
              pending_queue: {
                permissions: [
                  {
                    request_type: 'sudo_password',
                    request_id: 'sudo_req_boot',
                    prompt: 'Need sudo approval from bootstrap snapshot',
                  },
                ],
                questions: [],
              },
              window: {
                history_last_seq: 20,
                frame_last_seq: 3,
                commit_last_seq: 3,
              },
            },
          },
          {
            name: '分析 drivers/tool',
            task_id: 'dt_boot_b',
            session_id: 'delegate_sync_boot_b',
            subagent_type: 'explore',
            description: '分析 drivers/tool',
            task_key: 'bootstrap-tool',
            depends_on: 'bootstrap-kernel',
            status: 'done',
            model: 'deepseek-v4-pro',
            scope_path: '/home/wangergou/code/github/daima-agent/drivers/tool',
            scope_kind: 'subsystem',
            analysis_focus: 'tool_runtime',
            elapsed_ms: 1800,
            output: 'tool bootstrap final summary',
            target_files: 'drivers/tool/tool_delegate.c',
            write_approved: false,
            parent_response_sent: true,
            parent_resume_enqueued: true,
            coordinator_status: 'done',
            coordinator_wake_state: 'completed',
            wake_retry_count: 0,
            child_session: {
              summary: 'tool bootstrap final summary',
              history: makeHistoryEntries('tool bootstrap', 18),
              frames: [
                {
                  id: 'tool-bootstrap-frame-progress',
                  seq: 1,
                  type: 'subagent_progress',
                  phase: 'progress',
                  status: 'running',
                  task: '分析 drivers/tool',
                  detail: 'bootstrap in progress',
                  output_preview: 'tool bootstrap final summary',
                  ts: 3,
                },
                {
                  id: 'tool-bootstrap-frame-done',
                  seq: 2,
                  type: 'subagent_done',
                  phase: 'done',
                  status: 'done',
                  task: '分析 drivers/tool',
                  detail: 'bootstrap done',
                  output_preview: 'tool bootstrap final summary',
                  ts: 4,
                },
              ],
              commits: [
                {
                  id: 'tool-bootstrap-commit-progress',
                  seq: 1,
                  kind: 'progress',
                  phase: 'running',
                  status: 'running',
                  label: '分析 drivers/tool',
                  text: 'bootstrap in progress',
                  ts: 3,
                },
                {
                  id: 'tool-bootstrap-commit-result',
                  seq: 2,
                  kind: 'result',
                  phase: 'done',
                  status: 'done',
                  label: '分析 drivers/tool',
                  text: 'tool bootstrap final summary',
                  ts: 4,
                },
              ],
              pending_queue: {
                permissions: [],
                questions: [],
              },
              window: {
                history_last_seq: 18,
                frame_last_seq: 2,
                commit_last_seq: 2,
              },
            },
          },
        ],
      },
    ],
  };

  const fetchStub = async (url, options = {}) => {
    const text = String(url);
    requests.push(text);
    requestOptions.push({ url: text, options });
    return {
    ok: !text.includes('/api/subagent_state?chat_id=web_empty'),
    status: text.includes('/api/subagent_state?chat_id=web_empty') ? 404 : 200,
    async json() {
      if (text.includes('/api/ui_config')) {
        return {
          pet: { default_package_id: 'kitty.codex-pet', packages: [] },
          terminal: { security_level: 'build' },
        };
      }
      if (text.includes('/api/context_stats')) {
        return { model: 'deepseek-v4-pro', used_tokens: 0, context_limit_tokens: 1048576 };
      }
      if (text.includes('/api/sessions')) return { sessions: [] };
      if (text.includes('/api/session_history')) {
        if (sessionHistoryQueue.length) {
          sessionHistoryData = sessionHistoryQueue.shift() || { messages: [] };
        }
        return sessionHistoryData;
      }
      if (text.includes('/api/subagent_state_delta_chat')) {
        return {
          chat_id: 'web_test',
          after_visible_revision: 7,
          max_visible_revision: 12,
          replay_cursor: {
            after_visible_revision: 7,
            visible_revision: 12,
          },
          changed_count: 1,
          item_count: 2,
          coordinators: [
            {
              coordinator_id: 'dc_bootstrap',
              chat_id: 'web_test',
              team_run_id: 'tr_bootstrap',
              team_name: 'delegate-team',
              dispatch_mode: 'parallel',
              status: 'running',
              agent_count: 2,
              completed_count: 1,
              running_count: 1,
              queued_count: 0,
              blocked_count: 1,
              failed_count: 0,
              effective_output_count: 1,
              visible_revision: 12,
              replay_cursor: {
                visible_revision: 12,
              },
              wake_state: 'dispatched',
              wake_retry_count: 0,
              agents: [
                {
                  task_id: 'dt_boot_a',
                  session_id: 'delegate_sync_boot_a',
                  subagent_type: 'explore',
                  description: '分析 kernel',
                  status: 'running',
                  model: 'deepseek-v4-pro',
                },
                {
                  task_id: 'dt_boot_b',
                  session_id: 'delegate_sync_boot_b',
                  subagent_type: 'explore',
                  description: '分析 drivers/tool',
                  status: 'running',
                  model: 'deepseek-v4-pro',
                  blocker_kind: 'permission',
                  blocker_text: 'Need sudo approval from bootstrap snapshot',
                },
              ],
            },
          ],
          items: [
            {
              task_id: 'dt_boot_a',
              session_id: 'delegate_sync_boot_a',
              coordinator_id: 'dc_bootstrap',
              subagent_type: 'explore',
              status: 'running',
              child_session: {
                summary: 'kernel bootstrap delta summary',
                history: [
                  {
                    id: 'kernel-bootstrap-hist-21',
                    seq: 21,
                    role: 'assistant',
                    content: 'kernel bootstrap delta history',
                  },
                ],
                frames: [
                  {
                    id: 'kernel-bootstrap-frame-4',
                    seq: 4,
                    type: 'subagent_progress',
                    phase: 'progress',
                    status: 'running',
                    task: '分析 kernel',
                    detail: 'kernel delta frame',
                    output_preview: 'kernel bootstrap delta summary',
                    ts: 5,
                  },
                ],
                commits: [
                  {
                    id: 'kernel-bootstrap-commit-4',
                    seq: 4,
                    kind: 'progress',
                    phase: 'running',
                    status: 'running',
                    label: '分析 kernel',
                    text: 'kernel delta frame',
                    ts: 5,
                  },
                ],
                window: {
                  history_after_seq: 20,
                  frame_after_seq: 3,
                  commit_after_seq: 3,
                  history_last_seq: 21,
                  frame_last_seq: 4,
                  commit_last_seq: 4,
                  replay_reset: false,
                },
                pending_queue: { permissions: [], questions: [] },
              },
            },
            {
              task_id: 'dt_boot_b',
              session_id: 'delegate_sync_boot_b',
              coordinator_id: 'dc_bootstrap',
              subagent_type: 'explore',
              status: 'running',
              child_session: {
                summary: 'tool delta replay reset summary',
                history: [
                  {
                    id: 'tool-bootstrap-hist-31',
                    seq: 31,
                    role: 'assistant',
                    content: 'tool delta replay reset history',
                  },
                ],
                frames: [
                  {
                    id: 'tool-bootstrap-frame-31',
                    seq: 31,
                    type: 'subagent_progress',
                    phase: 'progress',
                    status: 'running',
                    task: '分析 drivers/tool',
                    detail: 'tool delta replay reset frame',
                    output_preview: 'tool delta replay reset summary',
                    ts: 6,
                  },
                ],
                commits: [
                  {
                    id: 'tool-bootstrap-commit-31',
                    seq: 31,
                    kind: 'progress',
                    phase: 'running',
                    status: 'running',
                    label: '分析 drivers/tool',
                    text: 'tool delta replay reset frame',
                    ts: 6,
                  },
                ],
                window: {
                  history_after_seq: 18,
                  frame_after_seq: 2,
                  commit_after_seq: 2,
                  history_last_seq: 31,
                  frame_last_seq: 31,
                  commit_last_seq: 31,
                  replay_reset: true,
                },
                pending_queue: { permissions: [], questions: [] },
              },
            },
          ],
        };
      }
      if (text.includes('/api/subagent_state_deltas')) {
        return {
          chat_id: 'web_test',
          item_count: 2,
          items: [
            {
              task_id: 'dt_boot_a',
              session_id: 'delegate_sync_boot_a',
              coordinator_id: 'dc_bootstrap',
              subagent_type: 'explore',
              status: 'running',
              child_session: {
                summary: 'kernel bootstrap delta summary',
                history: [
                  {
                    id: 'kernel-bootstrap-hist-21',
                    seq: 21,
                    role: 'assistant',
                    content: 'kernel bootstrap delta history',
                  },
                ],
                frames: [
                  {
                    id: 'kernel-bootstrap-frame-4',
                    seq: 4,
                    type: 'subagent_progress',
                    phase: 'progress',
                    status: 'running',
                    task: '分析 kernel',
                    detail: 'kernel delta frame',
                    output_preview: 'kernel bootstrap delta summary',
                    ts: 5,
                  },
                ],
                commits: [
                  {
                    id: 'kernel-bootstrap-commit-4',
                    seq: 4,
                    kind: 'progress',
                    phase: 'running',
                    status: 'running',
                    label: '分析 kernel',
                    text: 'kernel delta frame',
                    ts: 5,
                  },
                ],
                window: {
                  history_after_seq: 20,
                  frame_after_seq: 3,
                  commit_after_seq: 3,
                  history_last_seq: 21,
                  frame_last_seq: 4,
                  commit_last_seq: 4,
                  replay_reset: false,
                },
                pending_queue: { permissions: [], questions: [] },
              },
            },
            {
              task_id: 'dt_boot_b',
              session_id: 'delegate_sync_boot_b',
              coordinator_id: 'dc_bootstrap',
              subagent_type: 'explore',
              status: 'running',
              child_session: {
                summary: 'tool delta replay reset summary',
                history: [
                  {
                    id: 'tool-bootstrap-hist-31',
                    seq: 31,
                    role: 'assistant',
                    content: 'tool delta replay reset history',
                  },
                ],
                frames: [
                  {
                    id: 'tool-bootstrap-frame-31',
                    seq: 31,
                    type: 'subagent_progress',
                    phase: 'progress',
                    status: 'running',
                    task: '分析 drivers/tool',
                    detail: 'tool delta replay reset frame',
                    output_preview: 'tool delta replay reset summary',
                    ts: 6,
                  },
                ],
                commits: [
                  {
                    id: 'tool-bootstrap-commit-31',
                    seq: 31,
                    kind: 'progress',
                    phase: 'running',
                    status: 'running',
                    label: '分析 drivers/tool',
                    text: 'tool delta replay reset frame',
                    ts: 6,
                  },
                ],
                window: {
                  history_after_seq: 18,
                  frame_after_seq: 2,
                  commit_after_seq: 2,
                  history_last_seq: 31,
                  frame_last_seq: 31,
                  commit_last_seq: 31,
                  replay_reset: true,
                },
                pending_queue: { permissions: [], questions: [] },
              },
            },
          ],
        };
      }
      if (text.includes('/api/subagent_state_delta?')) {
        if (text.includes('task_id=dt_boot_a') &&
            text.includes('history_after_seq=20') &&
            text.includes('frame_after_seq=3') &&
            text.includes('commit_after_seq=3')) {
          return {
            task_id: 'dt_boot_a',
            session_id: 'delegate_sync_boot_a',
            coordinator_id: 'dc_bootstrap',
            subagent_type: 'explore',
            status: 'running',
            child_session: {
              summary: 'kernel bootstrap delta summary',
              history: [
                {
                  id: 'kernel-bootstrap-hist-21',
                  seq: 21,
                  role: 'assistant',
                  content: 'kernel bootstrap delta history',
                },
              ],
              frames: [
                {
                  id: 'kernel-bootstrap-frame-4',
                  seq: 4,
                  type: 'subagent_progress',
                  phase: 'progress',
                  status: 'running',
                  task: '分析 kernel',
                  detail: 'kernel delta frame',
                  output_preview: 'kernel bootstrap delta summary',
                  ts: 5,
                },
              ],
              commits: [
                {
                  id: 'kernel-bootstrap-commit-4',
                  seq: 4,
                  kind: 'progress',
                  phase: 'running',
                  status: 'running',
                  label: '分析 kernel',
                  text: 'kernel delta frame',
                  ts: 5,
                },
              ],
              window: {
                history_after_seq: 20,
                frame_after_seq: 3,
                commit_after_seq: 3,
                history_last_seq: 21,
                frame_last_seq: 4,
                commit_last_seq: 4,
                replay_reset: false,
              },
              pending_queue: { permissions: [], questions: [] },
            },
          };
        }
        if (text.includes('task_id=dt_boot_b') &&
            text.includes('history_after_seq=18') &&
            text.includes('frame_after_seq=2') &&
            text.includes('commit_after_seq=2')) {
          return {
            task_id: 'dt_boot_b',
            session_id: 'delegate_sync_boot_b',
            coordinator_id: 'dc_bootstrap',
            subagent_type: 'explore',
            status: 'running',
            child_session: {
              summary: 'tool delta replay reset summary',
              history: [
                {
                  id: 'tool-bootstrap-hist-31',
                  seq: 31,
                  role: 'assistant',
                  content: 'tool delta replay reset history',
                },
              ],
              frames: [
                {
                  id: 'tool-bootstrap-frame-31',
                  seq: 31,
                  type: 'subagent_progress',
                  phase: 'progress',
                  status: 'running',
                  task: '分析 drivers/tool',
                  detail: 'tool delta replay reset frame',
                  output_preview: 'tool delta replay reset summary',
                  ts: 6,
                },
              ],
              commits: [
                {
                  id: 'tool-bootstrap-commit-31',
                  seq: 31,
                  kind: 'progress',
                  phase: 'running',
                  status: 'running',
                  label: '分析 drivers/tool',
                  text: 'tool delta replay reset frame',
                  ts: 6,
                },
              ],
              window: {
                history_after_seq: 18,
                frame_after_seq: 2,
                commit_after_seq: 2,
                history_last_seq: 31,
                frame_last_seq: 31,
                commit_last_seq: 31,
                replay_reset: true,
              },
              pending_queue: { permissions: [], questions: [] },
            },
          };
        }
        return { error: 'subagent_state_delta_unavailable' };
      }
      if (text.includes('/api/subagent_state?chat_id=web_test')) return snapshotData;
      if (text.includes('/api/subagent_state?chat_id=web_question')) return snapshotData;
      if (text.includes('/api/subagent_state')) return { chat_id: 'unknown', coordinators: [] };
      return {};
    },
  };
  };
  fetchStub.setSnapshotData = (next) => {
    snapshotData = next;
  };
  fetchStub.setSessionHistoryData = (next) => {
    sessionHistoryData = next || { messages: [] };
    sessionHistoryQueue = [];
  };
  fetchStub.setSessionHistorySequence = (items) => {
    sessionHistoryQueue = Array.isArray(items) ? items.slice() : [];
    if (sessionHistoryQueue.length) {
      sessionHistoryData = sessionHistoryQueue[sessionHistoryQueue.length - 1] || { messages: [] };
    }
  };
  fetchStub.requests = requests;
  fetchStub.requestOptions = requestOptions;
  return fetchStub;
}

class MockWebSocket {
  static OPEN = 1;
  static CONNECTING = 0;
  static instance = null;

  constructor(url) {
    this.url = url;
    this.readyState = MockWebSocket.OPEN;
    this.sent = [];
    MockWebSocket.instance = this;
    setTimeout(() => {
      if (this.onopen) this.onopen();
    }, 0);
  }

  send(payload) {
    this.sent.push(payload);
  }

  close() {
    this.readyState = 3;
    if (this.onclose) this.onclose();
  }
}

function createPetStub() {
  return {
    init() {},
    destroy() {},
    attach() {},
    handleSocketOpen() {},
    handleSocketClose() {},
    handleToolMessage() {},
    handlePetResponse() {},
    handleAssistantComplete() {},
    consumeAssistantText(text) { return text; },
    markAssistantPending() {},
    noteDraftActivity() {},
    setPackage() {},
    setChatId() {},
  };
}

function bootstrapApp(dom) {
  const { window } = dom;
  const fetchStub = createFetchStub();
  window.fetch = fetchStub;
  window.WebSocket = MockWebSocket;
  window.requestAnimationFrame = (cb) => setTimeout(cb, 0);
  window.cancelAnimationFrame = clearTimeout;
  window.AgentPet = { createPetController: createPetStub };

  const context = dom.getInternalVMContext();
  context.fetch = fetchStub;
  context.WebSocket = MockWebSocket;
  context.console = console;
  context.setTimeout = setTimeout;
  context.clearTimeout = clearTimeout;
  context.setInterval = setInterval;
  context.clearInterval = clearInterval;

  const subagentStateCoreJs = fs.readFileSync(SUBAGENT_STATE_CORE_JS_PATH, 'utf8');
  vm.runInContext(subagentStateCoreJs, context);
  const subagentStateSelectorsJs = fs.readFileSync(SUBAGENT_STATE_SELECTORS_JS_PATH, 'utf8');
  vm.runInContext(subagentStateSelectorsJs, context);
  const subagentStateReducerJs = fs.readFileSync(SUBAGENT_STATE_REDUCER_JS_PATH, 'utf8');
  vm.runInContext(subagentStateReducerJs, context);
  const subagentStateJs = fs.readFileSync(SUBAGENT_STATE_JS_PATH, 'utf8');
  vm.runInContext(subagentStateJs, context);
  const subagentRuntimeJs = fs.readFileSync(SUBAGENT_RUNTIME_JS_PATH, 'utf8');
  vm.runInContext(subagentRuntimeJs, context);
  const subagentAppBridgeJs = fs.readFileSync(SUBAGENT_APP_BRIDGE_JS_PATH, 'utf8');
  vm.runInContext(subagentAppBridgeJs, context);
  const subagentPanelControllerJs = fs.readFileSync(SUBAGENT_PANEL_CONTROLLER_JS_PATH, 'utf8');
  vm.runInContext(subagentPanelControllerJs, context);
  const subagentDetailViewJs = fs.readFileSync(SUBAGENT_DETAIL_VIEW_JS_PATH, 'utf8');
  vm.runInContext(subagentDetailViewJs, context);
  const subagentEventAdapterJs = fs.readFileSync(SUBAGENT_EVENT_ADAPTER_JS_PATH, 'utf8');
  vm.runInContext(subagentEventAdapterJs, context);
  const subagentInteractiveControllerJs = fs.readFileSync(SUBAGENT_INTERACTIVE_CONTROLLER_JS_PATH, 'utf8');
  vm.runInContext(subagentInteractiveControllerJs, context);
  const subagentCoordinatorViewJs = fs.readFileSync(SUBAGENT_COORDINATOR_VIEW_JS_PATH, 'utf8');
  vm.runInContext(subagentCoordinatorViewJs, context);
  const subagentCoordinatorControllerJs = fs.readFileSync(SUBAGENT_COORDINATOR_CONTROLLER_JS_PATH, 'utf8');
  vm.runInContext(subagentCoordinatorControllerJs, context);
  const subagentUiOrchestratorJs = fs.readFileSync(SUBAGENT_UI_ORCHESTRATOR_JS_PATH, 'utf8');
  vm.runInContext(subagentUiOrchestratorJs, context);
  const subagentTransportJs = fs.readFileSync(SUBAGENT_TRANSPORT_JS_PATH, 'utf8');
  vm.runInContext(subagentTransportJs, context);
  const subagentChatTransportJs = fs.readFileSync(SUBAGENT_CHAT_TRANSPORT_JS_PATH, 'utf8');
  vm.runInContext(subagentChatTransportJs, context);
  const subagentBootstrapJs = fs.readFileSync(SUBAGENT_BOOTSTRAP_JS_PATH, 'utf8');
  vm.runInContext(subagentBootstrapJs, context);
  const appJs = fs.readFileSync(APP_JS_PATH, 'utf8');
  vm.runInContext(appJs, context);
  return { fetchStub };
}

function emit(data) {
  if (!MockWebSocket.instance || !MockWebSocket.instance.onmessage) {
    fail('websocket was not initialized');
  }
  MockWebSocket.instance.onmessage({ data: JSON.stringify(data) });
}

function expect(condition, message) {
  if (!condition) fail(message);
}

async function main() {
  const appSource = fs.readFileSync(APP_JS_PATH, 'utf8');
  expect(!/let coordinatorStates = new Map\(\);/.test(appSource), 'expected app.js to stop owning coordinator state');
  expect(!/function normalizeCoordinatorAgent\(/.test(appSource), 'expected app.js to stop normalizing coordinator agents');
  expect(!/function normalizeCoordinatorPayload\(/.test(appSource), 'expected app.js to stop normalizing coordinator payloads');
  expect(!/function mergeCoordinatorState\(/.test(appSource), 'expected app.js to stop merging coordinator state');
  expect(!/const orderedStates = \[\.\.\.coordinatorStates\.values\(\)\]\.sort/.test(appSource), 'expected app.js to stop owning coordinator ordering logic');
  expect(!/if \(!currentSelectedSubagentKey\(\)\)/.test(appSource), 'expected app.js to stop owning selected-subagent fallback logic');
  expect(!/let pendingSudoRequestId = '';/ .test(appSource), 'expected app.js to stop owning separate pendingSudoRequestId mirror state');

  const dom = buildDom();
  const { fetchStub } = bootstrapApp(dom);
  const interactiveControllerApi = dom.window.AgentSubagentInteractiveController;
  expect(interactiveControllerApi && typeof interactiveControllerApi.createInteractiveController === 'function',
    'expected subagent interactive controller API to be available');
  const standaloneInteractiveController = interactiveControllerApi.createInteractiveController({});
  expect(typeof standaloneInteractiveController.openPromptUi === 'function',
    'expected interactive controller to expose prompt UI open helper');
  expect(typeof standaloneInteractiveController.clearPromptUi === 'function',
    'expected interactive controller to expose prompt UI clear helper');
  const eventAdapterApi = dom.window.AgentSubagentEventAdapter;
  expect(eventAdapterApi && typeof eventAdapterApi.createSubagentEventAdapter === 'function',
    'expected subagent event adapter API to be available');
  const transportAdapter = eventAdapterApi.createSubagentEventAdapter({});
  expect(typeof transportAdapter.parseIncomingPayload === 'function',
    'expected subagent event adapter to expose inbound payload parser');
  expect(typeof transportAdapter.isSubagentPayload === 'function',
    'expected subagent event adapter to expose subagent payload classifier');
  expect(typeof transportAdapter.normalizeSnapshot === 'function',
    'expected subagent event adapter to expose snapshot normalizer');
  expect(typeof transportAdapter.makeSubagentEventAction === 'function',
    'expected subagent event adapter to expose subagent event action builder');
  expect(typeof transportAdapter.makeCoordinatorAction === 'function',
    'expected subagent event adapter to expose coordinator action builder');
  expect(typeof transportAdapter.makeInteractiveBlockerSetAction === 'function',
    'expected subagent event adapter to expose interactive blocker set action builder');
  expect(typeof transportAdapter.makeInteractiveBlockerClearAction === 'function',
    'expected subagent event adapter to expose interactive blocker clear action builder');
  expect(typeof transportAdapter.makeHydrateInput === 'function',
    'expected subagent event adapter to expose hydrate input builder');
  expect(typeof transportAdapter.makeSubagentSessionAction === 'function',
    'expected subagent event adapter to expose subagent session action builder');
  expect(typeof transportAdapter.makeInteractivePromptState === 'function',
    'expected subagent event adapter to expose interactive prompt state builder');
  expect(typeof transportAdapter.resolveRestoredInteractivePrompt === 'function',
    'expected subagent event adapter to expose restored interactive prompt resolver');
  expect(typeof transportAdapter.makeInteractiveReplyPayload === 'function',
    'expected subagent event adapter to expose interactive reply payload builder');
  expect(typeof transportAdapter.makeInteractiveUnblockedEvent === 'function',
    'expected subagent event adapter to expose interactive unblocked event builder');
  expect(typeof transportAdapter.makeInteractiveControllerState === 'function',
    'expected subagent event adapter to expose interactive controller state builder');

  const parsedWrapped = transportAdapter.parseIncomingPayload({
    text: JSON.stringify({ type: 'coordinator_status', coordinator: { coordinator_id: 'dc_parse' } }),
  });
  expect(parsedWrapped?.type === 'coordinator_status',
    'expected adapter inbound payload parser to unwrap websocket text payload');
  expect(transportAdapter.isSubagentPayload(parsedWrapped) === true,
    'expected coordinator payload to be classified as subagent transport data');
  expect(transportAdapter.isSubagentPayload({ type: 'response', content: 'plain assistant reply' }) === false,
    'expected normal assistant response to stay outside subagent transport adapter');
  const normalizedEmptySnapshot = transportAdapter.normalizeSnapshot(null);
  expect(normalizedEmptySnapshot && normalizedEmptySnapshot.coordinators && Array.isArray(normalizedEmptySnapshot.coordinators),
    'expected adapter snapshot normalizer to coerce empty payload into subagent snapshot shape');
  const subagentEventAction = transportAdapter.makeSubagentEventAction({
    type: 'subagent_progress',
    task_id: 'dt_parse',
    session_id: 'delegate_sync_parse',
    subagent_type: 'explore',
    task: 'parse adapter',
    detail: 'adapter event action',
  }, { now: () => 123, formatEventText: () => 'formatted event text' });
  expect(subagentEventAction?.kind === 'subagent_event' && subagentEventAction?.entry?.text === 'formatted event text',
    'expected adapter subagent event action builder to produce reducer-ready event action');
  const coordinatorAction = transportAdapter.makeCoordinatorAction({ coordinator_id: 'dc_parse', status: 'running' });
  expect(coordinatorAction?.kind === 'coordinator' && coordinatorAction?.payload?.coordinator_id === 'dc_parse',
    'expected adapter coordinator action builder to produce reducer-ready coordinator action');
  const blockerSetAction = transportAdapter.makeInteractiveBlockerSetAction({
    task_id: 'dt_parse',
    session_id: 'delegate_sync_parse',
    coordinator_id: 'dc_parse',
    request_type: 'question_text',
    request_id: 'req_parse',
    prompt: 'Need more info',
    blocker_kind: 'question',
    label: 'parse label',
  });
  expect(blockerSetAction?.kind === 'interactive_blocker_set' && blockerSetAction?.blocker?.request_id === 'req_parse',
    'expected adapter interactive blocker set builder to produce reducer-ready action');
  const blockerClearAction = transportAdapter.makeInteractiveBlockerClearAction({
    task_id: 'dt_parse',
    session_id: 'delegate_sync_parse',
    request_id: 'req_parse',
  }, {
    interactiveBlockerKey(blocker) {
      return `${blocker.task_id}:${blocker.request_id}`;
    },
  });
  expect(blockerClearAction?.kind === 'interactive_blocker_clear' && blockerClearAction?.key === 'dt_parse:req_parse',
    'expected adapter interactive blocker clear builder to produce reducer-ready clear key');
  const hydrateInput = transportAdapter.makeHydrateInput(null, {
    chatId: 'web_test',
    interactiveUiConfig() {
      return { blockerKind: 'permission', label: 'sudo permission', prompt: 'Need sudo approval' };
    },
  });
  expect(hydrateInput && hydrateInput.snapshot && Array.isArray(hydrateInput.snapshot.coordinators),
    'expected adapter hydrate input builder to normalize snapshot for reducer hydrate');
  const subagentSessionAction = transportAdapter.makeSubagentSessionAction({
    session: {
      task_id: 'dt_parse',
      session_id: 'delegate_sync_parse',
      agent: { task_id: 'dt_parse', session_id: 'delegate_sync_parse' },
    },
  });
  expect(subagentSessionAction?.kind === 'subagent_session' && subagentSessionAction?.payload?.task_id === 'dt_parse',
    'expected adapter subagent session builder to produce reducer-ready session action');
  const interactivePromptState = transportAdapter.makeInteractivePromptState({
    request_type: 'question_text',
    request_id: 'req_parse',
    prompt: 'Need more info',
  });
  expect(interactivePromptState?.ui?.inputType === 'text' && interactivePromptState?.requestId === 'req_parse',
    'expected adapter interactive prompt builder to produce UI-ready prompt state');
  const restoredPrompt = transportAdapter.resolveRestoredInteractivePrompt({
    request_type: 'question_text',
    request_id: 'req_parse',
    prompt: 'Need more info',
  });
  expect(restoredPrompt?.requestId === 'req_parse' && restoredPrompt?.prompt === 'Need more info',
    'expected adapter restored prompt resolver to reuse structured prompt state');
  const interactiveReplyPayload = transportAdapter.makeInteractiveReplyPayload({
    chat_id: 'web_test',
    task_id: 'dt_parse',
    session_id: 'delegate_sync_parse',
    coordinator_id: 'dc_parse',
    request_type: 'question_text',
    request_id: 'req_parse',
  }, {
    chatId: 'web_test',
    value: 'continue',
    cancelled: false,
  });
  expect(interactiveReplyPayload?.type === 'interactive_reply' &&
      interactiveReplyPayload?.request_id === 'req_parse' &&
      interactiveReplyPayload?.value === 'continue',
    'expected adapter interactive reply payload builder to produce websocket-ready reply payload');
  const interactiveUnblockedEvent = transportAdapter.makeInteractiveUnblockedEvent({
    task_id: 'dt_parse',
    session_id: 'delegate_sync_parse',
    coordinator_id: 'dc_parse',
    label: 'parse label',
  });
  expect(interactiveUnblockedEvent?.type === 'subagent_unblocked' &&
      interactiveUnblockedEvent?.task === 'parse label',
    'expected adapter interactive unblocked event builder to produce reducer-ready resume event');
  const interactiveControllerState = transportAdapter.makeInteractiveControllerState({
    chat_id: 'web_test',
    task_id: 'dt_parse',
    session_id: 'delegate_sync_parse',
    coordinator_id: 'dc_parse',
    request_type: 'question_text',
    request_id: 'req_parse',
    prompt: 'Need more info',
  });
  expect(interactiveControllerState?.request?.request_id === 'req_parse' &&
      interactiveControllerState?.prompt?.requestId === 'req_parse',
    'expected adapter interactive controller state builder to produce request + prompt state together');

  await new Promise((resolve) => setTimeout(resolve, 120));

  const currentChatId = dom.window.localStorage.getItem('agent_chat_id') || '';
  expect(currentChatId.startsWith('web_'),
    `expected runtime to allocate websocket chat id, got ${currentChatId}`);
  expect(dom.window.document.getElementById('status')?.textContent?.includes('已连接'),
    'expected websocket open to move connection badge to connected');

  emit({
    type: 'session_sync',
    chat_id: currentChatId,
    last_seq: 1,
    messages: [
      { seq: 1, role: 'user', content: '帮我分析目录结构' },
    ],
  });

  fetchStub.setSessionHistorySequence([
    {
      messages: [
        { seq: 1, role: 'user', content: '帮我分析目录结构' },
      ],
    },
    {
      messages: [
        { seq: 1, role: 'user', content: '帮我分析目录结构' },
        { seq: 2, role: 'assistant', content: '已启动后台子任务，coordinator_id=dc_ui。' },
      ],
    },
  ]);
  emit({ type: 'response', chat_id: currentChatId, content: '已启动后台子任务，coordinator_id=dc_ui。' });
  await new Promise((resolve) => setTimeout(resolve, 520));
  expect(
    fetchStub.requests.some((url) => String(url).includes(`/api/session_history?chat_id=${currentChatId}`)),
    'expected assistant response to trigger current session history reconcile',
  );
  expect(
    fetchStub.requests.filter((url) => String(url).includes(`/api/session_history?chat_id=${currentChatId}`)).length >= 2,
    'expected reconcile retry when early session history response is stale',
  );
  expect(
    dom.window.document.getElementById('messages')?.textContent?.includes('已启动后台子任务，coordinator_id=dc_ui。'),
    'expected assistant response to remain visible after automatic history reconcile',
  );
  emit({
    type: 'subagent_start',
    chat_id: 'web_test',
    task_id: 'dt_a',
    session_id: 'delegate_sync_a',
    coordinator_id: 'dc_ui',
    subagent_type: 'explore',
    status: 'running',
    task: '探索 kernel',
    detail: 'local_overview',
    scope_path: '/home/wangergou/code/github/daima-agent/kernel',
    scope_kind: 'subsystem',
    analysis_focus: 'execution_kernel',
    blocker_scope: 'task',
  });
  emit({
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
    scope_path: '/home/wangergou/code/github/daima-agent/kernel',
    scope_kind: 'subsystem',
    analysis_focus: 'execution_kernel',
    blocker_kind: 'permission',
    blocker_text: 'Waiting for sudo approval',
    blocker_scope: 'task',
  });
  emit({
    type: 'subagent_start',
    chat_id: 'web_test',
    task_id: 'dt_b',
    session_id: 'delegate_sync_b',
    coordinator_id: 'dc_ui',
    subagent_type: 'explore',
    status: 'running',
    task: '探索 drivers',
    detail: 'local_overview',
    output: 'drivers summary from event',
    scope_path: '/home/wangergou/code/github/daima-agent/drivers/tool',
    scope_kind: 'subsystem',
    analysis_focus: 'tool_runtime',
    blocker_scope: 'task',
  });
  emit({
    type: 'coordinator_status',
    chat_id: 'web_test',
    coordinator: {
      coordinator_id: 'dc_ui',
      team_run_id: 'tr_ui',
      team_name: 'repo-map-team',
      dispatch_mode: 'staged',
      chat_id: 'web_test',
      status: 'running',
      agent_count: 2,
      completed_count: 0,
      queued_count: 1,
      running_count: 1,
      visible_revision: 1,
      completion_notified: false,
      parent_response_sent: true,
      wake_state: 'dispatched',
      wake_retry_count: 0,
      wake_last_attempt_ms: 1,
      wake_last_success_ms: 1,
      agents: [
        {
          name: '探索 kernel',
          task_id: 'dt_a',
          session_id: 'delegate_sync_a',
          subagent_type: 'explore',
          description: '探索 kernel',
          status: 'blocked',
          model: 'deepseek-v4-pro',
          scope_path: '/home/wangergou/code/github/daima-agent/kernel',
          scope_kind: 'subsystem',
          analysis_focus: 'execution_kernel',
          elapsed_ms: 1234,
          summary: '',
          output: '',
          write_approved: false,
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
                ts: 1,
              },
            ],
            commits: [
              {
                kind: 'blocker',
                phase: 'blocked',
                status: 'blocked',
                label: '探索 kernel',
                text: 'Need sudo approval from child session snapshot',
                ts: 1,
              },
            ],
            pending_queue: {
              permissions: ['Need sudo approval from child session snapshot'],
              questions: [],
            },
          },
          parent_response_sent: true,
          coordinator_wake_state: 'dispatched',
          wake_retry_count: 0,
        },
        {
          name: '探索 drivers',
          task_id: 'dt_b',
          task_key: 'map-drivers',
          depends_on: 'map-kernel',
          session_id: 'delegate_sync_b',
          subagent_type: 'explore',
          description: '探索 drivers',
          status: 'queued',
          model: 'deepseek-v4-pro',
          scope_path: '/home/wangergou/code/github/daima-agent/drivers/tool',
          scope_kind: 'subsystem',
          analysis_focus: 'tool_runtime',
          elapsed_ms: 456,
          summary: '',
          output: 'drivers summary',
          write_approved: false,
          child_session: {
            summary: 'drivers summary from child session snapshot',
            history: [
              {
                role: 'assistant',
                content: 'drivers child snapshot assistant history',
              },
            ],
            frames: [
              {
                type: 'subagent_progress',
                phase: 'progress',
                status: 'running',
                task: '探索 drivers',
                detail: 'drivers summary from child session snapshot',
                output_preview: 'drivers summary from child session snapshot',
                ts: 2,
              },
            ],
            commits: [
              {
                kind: 'progress',
                phase: 'running',
                status: 'running',
                label: '探索 drivers',
                text: 'drivers summary from child session snapshot',
                ts: 2,
              },
            ],
            pending_queue: {
              permissions: [],
              questions: [],
            },
          },
          parent_response_sent: true,
          coordinator_wake_state: 'dispatched',
          wake_retry_count: 0,
        },
      ],
    },
  });

  const { document } = dom.window;
  const tabs = [...document.querySelectorAll('.subagent-detail-tab')];
  const blockers = [...document.querySelectorAll('.subagent-blocker')];
  const agents = [...document.querySelectorAll('.coordinator-agent')];
  const detailPanel = document.getElementById('subagentDetailPanel');
  const title = document.getElementById('subagentDetailTitle');
  const kernelTab = tabs.find((node) => (node.textContent || '').includes('探索 kernel'));

  expect(tabs.length === 2, `expected 2 subagent tabs, got ${tabs.length}`);
  expect(detailPanel && detailPanel.hidden === false, 'expected subagent detail panel to be visible');
  expect(detailPanel && detailPanel.parentElement?.id !== 'coordinatorPanel', 'expected subagent detail panel to live outside coordinator panel DOM');
  expect(title && title.textContent.includes('探索 kernel'), 'expected selected detail title to reference kernel subagent');
  expect(kernelTab && kernelTab.className.includes('blocked'), 'expected blocked tab styling for kernel subagent');
  expect(kernelTab && kernelTab.className.includes('active'), 'expected kernel tab to be active');
  expect(blockers.some((node) => node.textContent.includes('Waiting for sudo approval')), 'expected permission blocker to render');
  expect(
    agents.some((node) => node.className.includes('selected')),
    `expected coordinator card selection to sync with detail tab; selectedKey=${dom.window.AgentSubagentState?.effectiveSelectedSubagentKey?.(dom.window.agentDebugSubagentUiState || {}) || ''}; coordinatorCount=${dom.window.agentDebugSubagentUiState?.coordinators?.size || 0}; detailCount=${dom.window.agentDebugSubagentUiState?.details?.size || 0}; orderedCoordinatorCount=${dom.window.AgentSubagentState?.orderedCoordinatorStates?.(dom.window.agentDebugSubagentUiState || {})?.length || 0}; cards=${[...document.querySelectorAll('.coordinator-card')].length}; classes=${agents.map((node) => node.className).join(' | ')}; panel=${document.getElementById('coordinatorPanel')?.textContent || ''}`,
  );
  expect(agents.some((node) => node.textContent.includes('execution kernel')), 'expected coordinator card to render analysis focus');
  expect(agents.some((node) => node.textContent.includes('after: map-kernel')), 'expected coordinator card to render dependency hint');
  expect(detailPanel.textContent.includes('/home/wangergou/code/github/daima-agent/kernel'), 'expected detail panel to render scope path');
  expect(
    detailPanel.textContent.includes('Need sudo approval from child session snapshot'),
    'expected detail panel to prefer child session frames for blocked subagent timeline',
  );
  expect(document.body.textContent.includes('staged'), 'expected coordinator summary to expose staged dispatch mode');

  emit({
    type: 'interactive_request',
    chat_id: 'web_test',
    request_type: 'sudo_password',
    task_id: 'dt_a',
    session_id: 'delegate_sync_a',
    coordinator_id: 'dc_ui',
    request_id: 'sudo_req_1',
    prompt: 'Need sudo approval for delegated task',
  });
  const interactiveBlockers = [...document.querySelectorAll('.subagent-blocker')];
  expect(
    interactiveBlockers.some((node) => node.textContent.includes('Need sudo approval for delegated task')),
    'expected interactive blocker container to surface sudo_request prompt',
  );

  emit({
    type: 'interactive_request',
    chat_id: 'web_test',
    request_type: 'question_text',
    task_id: 'dt_b',
    session_id: 'delegate_sync_b',
    coordinator_id: 'dc_ui',
    request_id: 'question_req_1',
    prompt: 'Which output format do you want for this delegated task?',
  });
  expect(
    document.getElementById('interactiveTitle')?.textContent?.includes('需要补充信息'),
    'expected question request to switch modal title',
  );
  expect(
    document.getElementById('interactiveInput')?.getAttribute('type') === 'text',
    'expected question request to use text input',
  );
  const questionBlockers = [...document.querySelectorAll('.subagent-blocker')];
  expect(
    questionBlockers.some((node) => node.textContent.includes('Which output format do you want for this delegated task?')),
    'expected question interactive blocker to render prompt text',
  );
  expect(
    [...document.querySelectorAll('.subagent-detail-frame')].some((node) =>
      (node.textContent || '').includes('Which output format do you want for this delegated task?')),
    'expected detail timeline to render structured child-session question request event',
  );
  expect(
    [...document.querySelectorAll('.subagent-commit-row')].some((node) =>
      (node.textContent || '').includes('Which output format do you want for this delegated task?')),
    'expected session commits to retain structured question request event',
  );
  expect(
    [...document.querySelectorAll('.subagent-queue-row')].some((node) => node.textContent.includes('Which output format do you want for this delegated task?')),
    'expected pending queue to surface question request for the selected blocked subagent',
  );
  expect(
    ![...document.querySelectorAll('.subagent-queue-row')].some((node) => node.textContent.includes('Need sudo approval for delegated task')),
    'expected pending queue to avoid leaking another subagent permission request',
  );

  emit({
    type: 'subagent_session',
    chat_id: 'web_test',
    session: {
      coordinator_id: 'dc_ui',
      task_id: 'dt_b',
      session_id: 'delegate_sync_b',
      subagent_type: 'explore',
      status: 'running',
      task: '探索 drivers',
      task_key: 'map-drivers',
      depends_on: 'map-kernel',
      agent: {
        task_id: 'dt_b',
        session_id: 'delegate_sync_b',
        subagent_type: 'explore',
        description: '探索 drivers',
        status: 'running',
        model: 'deepseek-v4-pro',
        scope_path: '/home/wangergou/code/github/daima-agent/drivers/tool',
        scope_kind: 'subsystem',
        analysis_focus: 'tool_runtime',
        output: 'drivers streaming summary',
        child_session: {
          status: 'running',
          summary: 'drivers streaming summary',
          pending_request: {
            request_type: 'question_text',
            request_id: 'drivers_live_q_1',
            prompt: '请确认 drivers 子任务是否继续展开到 ws_client',
          },
          history: [
            {
              role: 'assistant',
              content: 'drivers live assistant history',
              reasoning: 'drivers live reasoning history',
            },
          ],
          frames: [
            {
              type: 'subagent_progress',
              phase: 'progress',
              status: 'running',
              task: '探索 drivers',
              detail: 'drivers live stream frame',
              output_preview: 'drivers streaming summary',
              ts: 3,
            },
          ],
          commits: [
            {
              kind: 'progress',
              phase: 'running',
              status: 'running',
              label: '探索 drivers',
              text: 'drivers live stream frame',
              ts: 3,
            },
          ],
          pending_queue: {
            permissions: [],
            questions: [
              {
                request_type: 'question_text',
                request_id: 'drivers_live_q_1',
                prompt: '请确认 drivers 子任务是否继续展开到 ws_client',
              },
            ],
          },
          latest_frame: {
            type: 'subagent_request',
            phase: 'blocked',
            status: 'blocked',
            task: '探索 drivers',
            detail: '请确认 drivers 子任务是否继续展开到 ws_client',
            blocker_kind: 'question',
            blocker_text: '请确认 drivers 子任务是否继续展开到 ws_client',
            ts: 4,
          },
        },
      },
    },
  });
  const driverTab = [...document.querySelectorAll('.subagent-detail-tab')].find((node) => (node.textContent || '').includes('探索 drivers'));
  if (driverTab) {
    driverTab.click();
  }
  expect(
    document.getElementById('subagentDetailPanel')?.textContent?.includes('Which output format do you want for this delegated task?'),
    'expected drivers detail to retain its own question blocker',
  );
  expect(
    !document.getElementById('subagentDetailPanel')?.textContent?.includes('Need sudo approval for delegated task'),
    'expected drivers detail to avoid leaking kernel permission blocker',
  );
  expect(
    document.getElementById('subagentDetailPanel')?.textContent?.includes('drivers live stream frame'),
    'expected subagent_session stream payload to refresh selected detail timeline text',
  );
  expect(
    document.getElementById('subagentDetailPanel')?.textContent?.includes('drivers live assistant history'),
    'expected subagent detail to render child session assistant history',
  );
  expect(
    document.getElementById('subagentDetailPanel')?.textContent?.includes('drivers live reasoning history'),
    'expected subagent detail to render child session reasoning history',
  );
  expect(
    document.getElementById('subagentDetailPanel')?.textContent?.includes('请确认 drivers 子任务是否继续展开到 ws_client'),
    'expected normalized child session pending request/latest frame to render in selected detail',
  );
  expect(
    [...document.querySelectorAll('.subagent-commit-row')].some((node) => (node.textContent || '').includes('Which output format do you want for this delegated task?')),
    'expected drivers detail to preserve question request commit after session refresh',
  );
  const kernelTabAfterSession = [...document.querySelectorAll('.subagent-detail-tab')].find((node) => (node.textContent || '').includes('探索 kernel'));
  if (kernelTabAfterSession) {
    kernelTabAfterSession.click();
  }
  expect(
    document.getElementById('subagentDetailPanel')?.textContent?.includes('Need sudo approval for delegated task'),
    'expected kernel detail to retain its own permission blocker',
  );
  expect(
    !document.getElementById('subagentDetailPanel')?.textContent?.includes('Which output format do you want for this delegated task?'),
    'expected kernel detail to avoid leaking drivers question blocker',
  );

  emit({
    type: 'subagent_session',
    chat_id: 'web_test',
    session: {
      coordinator_id: 'dc_ui',
      task_id: 'dt_b',
      session_id: 'delegate_sync_b',
      subagent_type: 'explore',
      status: 'running',
      task: '探索 drivers',
      task_key: 'map-drivers',
      depends_on: 'map-kernel',
      agent: {
        task_id: 'dt_b',
        session_id: 'delegate_sync_b',
        subagent_type: 'explore',
        description: '探索 drivers',
        status: 'running',
        model: 'deepseek-v4-pro',
        scope_path: '/home/wangergou/code/github/daima-agent/drivers/tool',
        scope_kind: 'subsystem',
        analysis_focus: 'tool_runtime',
        child_session: {
          summary: '',
          history: [
            {
              role: 'user',
              content: '帮我分析 drivers/tool 的结构和关键模块',
            },
            {
              role: 'assistant',
              content: 'drivers history-only assistant summary',
              reasoning: 'drivers history-only reasoning',
            },
          ],
          frames: [],
          commits: [],
          pending_queue: {
            permissions: [],
            questions: [],
          },
        },
      },
    },
  });
  const driverTabHistoryOnly = [...document.querySelectorAll('.subagent-detail-tab')].find((node) => (node.textContent || '').includes('探索 drivers'));
  if (driverTabHistoryOnly) {
    driverTabHistoryOnly.click();
  }
  expect(
    document.getElementById('subagentDetailPanel')?.textContent?.includes('drivers history-only assistant summary'),
    'expected detail to render history-only assistant transcript',
  );
  expect(
    document.getElementById('subagentDetailPanel')?.textContent?.includes('drivers history-only reasoning'),
    'expected detail to render history-only reasoning transcript',
  );
  expect(
    !document.getElementById('subagentDetailPanel')?.textContent?.includes('暂无事件帧'),
    'expected history-only child session to avoid misleading empty frame placeholder',
  );
  const kernelTabBeforeResume = [...document.querySelectorAll('.subagent-detail-tab')].find((node) => (node.textContent || '').includes('探索 kernel'));
  if (kernelTabBeforeResume) {
    kernelTabBeforeResume.click();
  }

  emit({
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
    scope_path: '/home/wangergou/code/github/daima-agent/kernel',
    scope_kind: 'subsystem',
    analysis_focus: 'execution_kernel',
    blocker_scope: 'task',
  });
  emit({
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
    scope_path: '/home/wangergou/code/github/daima-agent/kernel',
    scope_kind: 'subsystem',
    analysis_focus: 'execution_kernel',
    blocker_scope: 'task',
  });

  const frameTexts = [...document.querySelectorAll('.subagent-detail-frame')].map((node) => node.textContent || '');
  expect(
    [...document.querySelectorAll('.subagent-detail-frame-badge')].some((node) => node.textContent.includes('恢复')),
    'expected timeline badge to show resumed state',
  );
  expect(
    [...document.querySelectorAll('.subagent-blocker-history-row')].some((node) => node.textContent.includes('Waiting for sudo approval')),
    'expected blocker history to retain prior blocked reason',
  );
  expect(
    document.querySelector('.subagent-commit-row')?.textContent !== undefined,
    'expected commit rows to remain rendered after subagent_session update',
  );
  expect(
    document.querySelector('.subagent-detail-summary-body')?.textContent?.includes('kernel final summary'),
    'expected session summary panel to render latest child summary',
  );
  expect(
    [...document.querySelectorAll('.subagent-commit-row')].some((node) => node.textContent.includes('kernel final summary')),
    'expected session commits to surface final child result',
  );

  const closeButton = document.getElementById('coordinatorClose');
  if (!closeButton) {
    fail('expected coordinator close button to exist');
  }
  closeButton.click();
  await new Promise((resolve) => setTimeout(resolve, 450));
  expect(
    document.getElementById('coordinatorPanel')?.hidden === true,
    'expected coordinator panel to hide after manual close',
  );
  expect(
    document.getElementById('subagentDetailPanel')?.hidden === false,
    'expected detail panel to remain visible after manual close of coordinator panel',
  );
  expect(
    dom.window.agentDebugSubagentUiState?.details?.size >= 2,
    'expected manual close to preserve subagent session/detail state',
  );

  emit({
    type: 'coordinator_status',
    chat_id: 'web_test',
    coordinator: {
      coordinator_id: 'dc_ui',
      team_run_id: 'tr_ui',
      team_name: 'repo-map-team',
      dispatch_mode: 'staged',
      chat_id: 'web_test',
      status: 'done',
      agent_count: 2,
      completed_count: 2,
      queued_count: 0,
      running_count: 0,
      blocked_count: 0,
      failed_count: 0,
      effective_output_count: 2,
      visible_revision: 99,
      completion_notified: true,
      parent_response_sent: true,
      parent_resume_enqueued: true,
      wake_state: 'completed',
      wake_retry_count: 0,
      wake_last_attempt_ms: 99,
      wake_last_success_ms: 99,
      agents: [
        {
          name: '探索 kernel',
          task_id: 'dt_a',
          session_id: 'delegate_sync_a',
          subagent_type: 'explore',
          description: '探索 kernel',
          status: 'done',
          model: 'deepseek-v4-pro',
          scope_path: '/home/wangergou/code/github/daima-agent/kernel',
          scope_kind: 'subsystem',
          analysis_focus: 'execution_kernel',
          elapsed_ms: 2200,
          output: 'kernel final summary',
          write_approved: false,
          child_session: {
            summary: 'kernel final summary',
            history: [
              { role: 'assistant', content: 'kernel resumed after manual close' },
            ],
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
  expect(
    document.getElementById('coordinatorPanel')?.hidden === false,
    'expected coordinator panel to reopen when fresh coordinator update arrives',
  );
  expect(
    document.querySelectorAll('.subagent-queue-row').length === 0,
    'expected pending queue to clear after subagent resumes',
  );
  expect(
    document.getElementById('subagentDetailPanel')?.textContent?.includes('child snapshot assistant before blocker'),
    'expected kernel detail to retain child session assistant history',
  );
  emit({
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
  });
  expect(
    (dom.window.agentDebugSubagentUiState?.liveCursor?.visibleRevision || 0) === 123,
    'expected live websocket subagent event to advance runtime visible cursor',
  );
  emit({
    type: 'coordinator_status',
    chat_id: 'web_test',
    coordinator: {
      coordinator_id: 'dc_ui',
      team_run_id: 'tr_ui',
      team_name: 'repo-map-team',
      dispatch_mode: 'staged',
      chat_id: 'web_test',
      status: 'running',
      agent_count: 2,
      completed_count: 1,
      queued_count: 0,
      running_count: 1,
      visible_revision: 2,
      completion_notified: false,
      parent_response_sent: true,
      wake_state: 'dispatched',
      wake_retry_count: 0,
      wake_last_attempt_ms: 2,
      wake_last_success_ms: 2,
      agents: [
        {
          name: '探索 kernel',
          task_id: 'dt_a',
          session_id: 'delegate_sync_a',
          subagent_type: 'explore',
          description: '探索 kernel',
          status: 'running',
          model: 'deepseek-v4-pro',
          scope_path: '/home/wangergou/code/github/daima-agent/kernel',
          scope_kind: 'subsystem',
          analysis_focus: 'execution_kernel',
          elapsed_ms: 1235,
          output: '',
          write_approved: false,
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
          parent_response_sent: true,
          coordinator_wake_state: 'dispatched',
          wake_retry_count: 0,
        },
      ],
    },
  });
  expect(
    document.getElementById('subagentDetailPanel')?.textContent?.includes('kernel final summary'),
    'expected stale coordinator snapshot to avoid erasing newer final result',
  );
  expect(
    !document.getElementById('subagentDetailPanel')?.textContent?.includes('stale coordinator blocked frame'),
    'expected stale coordinator snapshot to avoid replacing newer session timeline with older blocked frame',
  );
  expect(
    !document.getElementById('subagentDetailPanel')?.textContent?.includes('Need sudo approval from stale coordinator snapshot'),
    'expected stale coordinator snapshot to avoid reviving resolved blocker queue',
  );
  emit({
    type: 'subagent_session',
    chat_id: 'web_test',
    session: {
      coordinator_id: 'dc_ui',
      task_id: 'dt_a',
      session_id: 'delegate_sync_a',
      subagent_type: 'explore',
      status: 'running',
      task: '探索 kernel',
      task_key: 'map-kernel',
      agent: {
        task_id: 'dt_a',
        session_id: 'delegate_sync_a',
        subagent_type: 'explore',
        description: '探索 kernel',
        status: 'running',
        model: 'deepseek-v4-pro',
        scope_path: '/home/wangergou/code/github/daima-agent/kernel',
        scope_kind: 'subsystem',
        analysis_focus: 'execution_kernel',
        output: '',
        child_session: {
          status: 'running',
          summary: 'kernel stale session snapshot',
          history: [
            {
              id: 'kernel-stale-hist-1',
              seq: 1,
              role: 'assistant',
              content: 'kernel stale child history',
              reasoning: 'kernel stale child reasoning',
            },
          ],
          frames: [
            {
              id: 'kernel-stale-frame-1',
              seq: 1,
              type: 'subagent_blocked',
              phase: 'blocked',
              status: 'blocked',
              task: '探索 kernel',
              detail: 'kernel stale blocked frame',
              blocker_kind: 'permission',
              blocker_text: 'Need sudo approval from stale subagent session',
              ts: 1,
            },
          ],
          commits: [
            {
              id: 'kernel-stale-commit-1',
              seq: 1,
              kind: 'blocker',
              phase: 'blocked',
              status: 'blocked',
              label: '探索 kernel',
              text: 'kernel stale blocked frame',
              ts: 1,
            },
          ],
          pending_queue: {
            permissions: [
              {
                request_type: 'sudo_password',
                request_id: 'stale_subagent_req',
                prompt: 'Need sudo approval from stale subagent session',
              },
            ],
            questions: [],
          },
          latest_frame: {
            id: 'kernel-stale-frame-1',
            seq: 1,
            type: 'subagent_blocked',
            phase: 'blocked',
            status: 'blocked',
            task: '探索 kernel',
            detail: 'kernel stale blocked frame',
            blocker_kind: 'permission',
            blocker_text: 'Need sudo approval from stale subagent session',
            ts: 1,
          },
        },
      },
    },
  });
  expect(
    document.getElementById('subagentDetailPanel')?.textContent?.includes('kernel final summary'),
    'expected stale subagent_session snapshot to avoid erasing newer final result',
  );
  expect(
    !document.getElementById('subagentDetailPanel')?.textContent?.includes('kernel stale blocked frame'),
    'expected stale subagent_session snapshot to avoid replacing newer session timeline with older blocked frame',
  );
  expect(
    !document.getElementById('subagentDetailPanel')?.textContent?.includes('Need sudo approval from stale subagent session'),
    'expected stale subagent_session snapshot to avoid reviving resolved blocker queue',
  );
  emit({
    type: 'coordinator_done',
    chat_id: 'web_test',
    coordinator: {
      coordinator_id: 'dc_ui',
      chat_id: 'web_test',
      team_run_id: 'tr_ui',
      team_name: 'repo-map-team',
      dispatch_mode: 'staged',
      status: 'failed',
      agent_count: 2,
      completed_count: 2,
      running_count: 0,
      queued_count: 0,
      blocked_count: 0,
      failed_count: 1,
      effective_output_count: 1,
      agents: [
        {
          name: '探索 kernel',
          task_id: 'dt_a',
          session_id: 'delegate_sync_a',
          subagent_type: 'explore',
          description: '探索 kernel',
          status: 'done',
          model: 'deepseek-v4-pro',
          scope_path: '/home/wangergou/code/github/daima-agent/kernel',
          scope_kind: 'subsystem',
          analysis_focus: 'execution_kernel',
          elapsed_ms: 2200,
          summary: 'kernel final summary',
          output: 'kernel final summary',
        },
        {
          name: '探索 drivers',
          task_id: 'dt_b',
          session_id: 'delegate_sync_b',
          subagent_type: 'explore',
          description: '探索 drivers',
          status: 'error',
          model: 'deepseek-v4-pro',
          scope_path: '/home/wangergou/code/github/daima-agent/drivers/tool',
          scope_kind: 'subsystem',
          analysis_focus: 'tool_runtime',
          elapsed_ms: 456,
          summary: 'drivers streaming summary',
          output: 'drivers streaming summary',
        },
      ],
    },
  });
  expect(
    document.getElementById('subagentDetailPanel')?.textContent?.includes('kernel final summary'),
    'expected completed subagent detail to remain visible after coordinator_done',
  );
  expect(
    [...document.querySelectorAll('.coordinator-card')].some((node) => (node.textContent || '').includes('dc_ui')),
    'expected completed coordinator card to remain visible after coordinator_done',
  );
  const state = dom.window.AgentSubagentState;
  expect(!!state, 'expected subagent state module to be present');
  expect(!!dom.window.AgentSubagentCoordinatorController, 'expected coordinator controller module to be present');
  const uiState = dom.window.agentDebugSubagentUiState;
  const selectedDetailView = state?.selectedSubagentDetailView?.(uiState || {}, 'web_test');
  expect(!!selectedDetailView, 'expected selected detail view-model to be exposed');
  const selectedKey = state.currentSelectedSubagentKey(uiState || {});
  if (selectedKey) {
    const detail = uiState?.details?.get?.(selectedKey);
    expect(selectedDetailView?.summaryText?.includes('kernel final summary'), 'expected detail view-model to expose summary text');
    expect(selectedDetailView?.outputText?.includes('kernel final summary'), 'expected detail view-model to expose final output text');
    expect(Array.isArray(selectedDetailView?.history) && selectedDetailView.history.length > 0, 'expected detail view-model to expose session history items');
    expect(selectedDetailView?.framesEmptyText === '暂无事件帧', 'expected detail view-model to expose empty timeline text');
    expect(selectedDetailView?.outputEmptyText === '暂无最终输出', 'expected detail view-model to expose empty output text');
    expect(selectedDetailView?.commitTitle === 'Session commits', 'expected detail view-model to expose commit section title');
    expect(selectedDetailView?.historyTitle === 'Session history', 'expected detail view-model to expose history section title');
    expect(
      Array.isArray(selectedDetailView?.historyItems) &&
        selectedDetailView.historyItems.length > 0 &&
        selectedDetailView.historyItems.some((item) => item.role === 'assistant' && item.content.includes('kernel bootstrap history 00') === false),
      'expected detail view-model to expose normalized history render items',
    );
    expect(detail?.session_summary?.includes('kernel final summary'), 'expected reducer state to persist session_summary');
    expect(detail?.latest_frame?.output_preview?.includes('kernel final summary'), 'expected reducer state to persist latest_frame');
    expect(Array.isArray(detail?.commits) && detail.commits.some((item) => String(item?.text || '').includes('kernel final summary')), 'expected reducer state to persist child commits');
    expect((detail?.pending_queue?.permissions?.length || 0) === 0, 'expected reducer state to clear pending permission queue after resume');
  }

  await dom.window.eval('switchSession("web_test")');
  expect(
    [...document.querySelectorAll('.coordinator-card')].some((node) => (node.textContent || '').includes('dc_bootstrap')),
    'expected HTTP subagent bootstrap to render coordinator card after snapshot load',
  );
  expect(
    document.getElementById('subagentDetailPanel')?.textContent?.includes('kernel bootstrap final summary'),
    'expected HTTP subagent bootstrap to restore selected detail final summary',
  );
  const deltaChatBodies = fetchStub.requestOptions
    .filter((entry) => entry.url.includes('/api/subagent_state_delta_chat'))
    .map((entry) => String(entry.options?.body || ''));
  const matchedDeltaChatReplay = fetchStub.requestOptions.some((entry) =>
    entry.url.includes('/api/subagent_state_delta_chat') &&
    entry.options?.method === 'POST' &&
    String(entry.options?.body || '').includes('"after_visible_revision":7') &&
    String(entry.options?.body || '').includes('"task_id":"dt_boot_a"') &&
    String(entry.options?.body || '').includes('"history_after_seq":20') &&
    String(entry.options?.body || '').includes('"task_id":"dt_boot_b"') &&
    String(entry.options?.body || '').includes('"commit_after_seq":2'));
  if (!matchedDeltaChatReplay) {
    console.error('delta_chat request bodies:', deltaChatBodies);
  }
  expect(
    matchedDeltaChatReplay,
    'expected HTTP subagent bootstrap to request chat delta replay with coordinator revision and child session cursors',
  );
  expect(
    deltaChatBodies.some((body) => body.includes('"after_visible_revision":7')),
    'expected HTTP subagent bootstrap to use explicit replay cursor visible revision',
  );
  expect(
    document.getElementById('subagentDetailPanel')?.textContent?.includes('Need sudo approval from bootstrap snapshot'),
    'expected HTTP subagent bootstrap to restore pending interactive blocker text',
  );
  expect(
    document.getElementById('subagentDetailPanel')?.textContent?.includes('kernel delta frame'),
    'expected HTTP subagent bootstrap to merge incremental kernel delta after snapshot restore',
  );
  expect(
    [...document.querySelectorAll('.subagent-detail-frame-badge')].some((node) => (node.textContent || '').includes('工具')),
    'expected bootstrap detail timeline to label child-session tool step explicitly',
  );
  expect(
    [...document.querySelectorAll('.subagent-commit-row')].some((node) => (node.textContent || '').includes('preflight terminal')),
    'expected bootstrap session commits to retain child-session tool step',
  );
  const bootstrapUiState = dom.window.agentDebugSubagentUiState;
  const bootstrapSelectedKey = state.currentSelectedSubagentKey(bootstrapUiState || {});
  if (bootstrapSelectedKey) {
    const bootstrapDetail = bootstrapUiState?.details?.get?.(bootstrapSelectedKey);
    const bootstrapSelectedDetailView = state?.selectedSubagentDetailView?.(bootstrapUiState || {}, 'web_test');
    expect(
      Array.isArray(bootstrapDetail?.history) &&
        bootstrapDetail.history.length > 0 &&
        bootstrapDetail.history.every((item) => typeof item?.id === 'string' && item.id.length > 0) &&
        bootstrapDetail.history.every((item, index) => Number(item?.seq) === index + 1) &&
        bootstrapDetail.history[0]?.id === 'kernel-bootstrap-hist-1',
      'expected reducer state to preserve backend-provided history ids and seq',
    );
    expect(
      Array.isArray(bootstrapSelectedDetailView?.historyItems) &&
        bootstrapSelectedDetailView.historyItems.some((item) => item.id === 'kernel-bootstrap-hist-1'),
      'expected detail view-model to expose backend-provided history ids',
    );
    expect(
      Array.isArray(bootstrapDetail?.frames) &&
        bootstrapDetail.frames.some((item) => item.id === 'kernel-bootstrap-frame-preflight' && item.seq === 1) &&
        bootstrapDetail.frames.some((item) => item.id === 'kernel-bootstrap-frame-done' && item.seq === 3),
      'expected reducer state to preserve backend-provided frame ids and seq',
    );
    expect(
      Array.isArray(bootstrapDetail?.commits) &&
        bootstrapDetail.commits.some((item) => item.id === 'kernel-bootstrap-commit-preflight' && item.seq === 1) &&
        bootstrapDetail.commits.some((item) => item.id === 'kernel-bootstrap-commit-result' && item.seq === 3),
      'expected reducer state to preserve backend-provided commit ids and seq',
    );
  }
  const bootstrapKernelDetail = bootstrapUiState?.details?.get?.('dt_boot_a');
  expect(
    bootstrapUiState?.interactiveBlockers?.get?.('dt_boot_a')?.request_id === 'sudo_req_boot',
    'expected HTTP subagent bootstrap to hydrate interactive blockers from snapshot protocol',
  );
  expect(
    bootstrapKernelDetail?.pending_queue?.permissions?.[0]?.request_type === 'sudo_password' &&
      bootstrapKernelDetail?.pending_queue?.permissions?.[0]?.request_id === 'sudo_req_boot' &&
      bootstrapKernelDetail?.pending_queue?.permissions?.[0]?.prompt?.includes('Need sudo approval from bootstrap snapshot'),
    'expected HTTP subagent bootstrap to retain structured pending permission request',
  );
  expect(
    Array.isArray(bootstrapKernelDetail?.history) &&
      bootstrapKernelDetail.history.length === 21 &&
      String(bootstrapKernelDetail.history[0]?.content || '').includes('kernel bootstrap history 00') &&
      bootstrapKernelDetail.history.some((item) => String(item?.content || '').includes('kernel bootstrap history 19')) &&
      bootstrapKernelDetail.history.some((item) => String(item?.content || '').includes('kernel bootstrap delta history')),
    'expected HTTP subagent bootstrap to retain snapshot child session history and append incremental delta history',
  );
  expect(
    bootstrapKernelDetail.history.some((item) => String(item?.reasoning || '').includes('kernel bootstrap reasoning 00')),
    'expected HTTP subagent bootstrap to retain child session reasoning entries',
  );
  expect(
    document.getElementById('interactiveModal')?.hidden === false,
    'expected HTTP subagent bootstrap to reopen interactive modal for pending request',
  );
  expect(
    document.getElementById('interactiveTitle')?.textContent?.includes('需要授权'),
    'expected HTTP subagent bootstrap to restore sudo authorization modal title',
  );
  expect(
    document.getElementById('interactiveInput')?.getAttribute('type') === 'password',
    'expected HTTP subagent bootstrap to restore password input mode',
  );
  expect(
    document.getElementById('subagentDetailPanel')?.textContent?.includes('/home/wangergou/code/github/daima-agent/kernel'),
    'expected HTTP subagent bootstrap to restore scope path',
  );
  const bootstrapToolTab = [...document.querySelectorAll('.subagent-detail-tab')]
    .find((node) => (node.textContent || '').includes('分析 drivers/tool'));
  if (bootstrapToolTab) {
    bootstrapToolTab.click();
  }
  expect(
    [...document.querySelectorAll('.subagent-commit-row')].some((node) => (node.textContent || '').includes('tool delta replay reset frame')) &&
      ![...document.querySelectorAll('.subagent-commit-row')].some((node) => (node.textContent || '').includes('tool bootstrap final summary')),
    'expected replay_reset delta to replace stale tool child session commits after bootstrap restore',
  );
  expect(
    document.getElementById('subagentDetailPanel')?.textContent?.includes('tool delta replay reset frame'),
    'expected replay_reset delta to replace stale tool session timeline after snapshot restore',
  );
  expect(
    !document.getElementById('subagentDetailPanel')?.textContent?.includes('tool bootstrap final summary'),
    'expected replay_reset delta to replace old tool commit/timeline content instead of merging stale snapshot rows',
  );

  fetchStub.setSnapshotData({
    chat_id: 'web_question',
    interactive: {
      blockers: [
        {
          chat_id: 'web_question',
          task_id: '',
          session_id: '',
          coordinator_id: '',
          request_type: 'question_text',
          request_id: 'question_bootstrap_root_1',
          prompt: 'Need root-level interview answer from bootstrap snapshot',
          blocker_kind: 'question',
          label: '需要补充信息',
        },
      ],
    },
    pending_request: {
      request_type: 'question_text',
      request_id: 'question_bootstrap_root_1',
      prompt: 'Need root-level interview answer from bootstrap snapshot',
    },
    coordinators: [],
  });
  await dom.window.eval('switchSession("web_question")');
  await new Promise((resolve) => setTimeout(resolve, 450));
  expect(
    document.getElementById('interactiveModal')?.hidden === false,
    'expected root-level pending interview snapshot to reopen interactive modal after reload',
  );
  expect(
    document.getElementById('interactiveTitle')?.textContent?.includes('需要补充信息'),
    'expected root-level pending interview snapshot to restore question modal title',
  );
  expect(
    document.getElementById('interactivePrompt')?.textContent?.includes('Need root-level interview answer from bootstrap snapshot'),
    'expected root-level pending interview snapshot to restore interview prompt text',
  );
  expect(
    dom.window.agentDebugSubagentUiState?.interactiveBlockers?.get?.('web_question')?.request_id === 'question_bootstrap_root_1',
    'expected root-level pending interview snapshot to hydrate root interactive blocker from snapshot protocol',
  );
  document.getElementById('interactiveModal')?.classList?.remove('show');
  document.getElementById('interactiveModal').hidden = true;

  await dom.window.eval('switchSession("web_empty")');
  await new Promise((resolve) => setTimeout(resolve, 450));
  expect(
    document.querySelectorAll('.coordinator-card').length === 0,
    'expected empty subagent snapshot response to clear coordinator cards',
  );
  expect(
    document.getElementById('subagentDetailPanel')?.hidden === true,
    'expected empty subagent snapshot response to hide detail panel',
  );
  expect(
    !document.getElementById('interactiveModal')?.classList?.contains('show'),
    'expected empty subagent snapshot response to clear pending interactive modal state',
  );

  console.log('ui-check ok: multi-subagent tabs/detail/timeline/blocker rendered as expected');
}

main().catch((error) => {
  fail(error instanceof Error ? error.stack || error.message : String(error));
});
