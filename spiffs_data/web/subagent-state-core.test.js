const assert = require('assert');
const fs = require('fs');
const path = require('path');
const vm = require('vm');

function loadBrowserScript(context, filename) {
  const fullPath = path.join(__dirname, filename);
  const source = fs.readFileSync(fullPath, 'utf8');
  vm.runInContext(source, context, { filename: fullPath });
}

function createBrowserContext() {
  const window = {};
  window.window = window;
  window.console = console;
  window.setTimeout = setTimeout;
  window.clearTimeout = clearTimeout;
  window.Date = Date;
  window.Map = Map;
  window.Array = Array;
  window.Object = Object;
  window.String = String;
  window.Number = Number;
  window.Boolean = Boolean;
  window.JSON = JSON;
  window.Math = Math;
  return vm.createContext(window);
}

function loadSubagentStateModules() {
  const context = createBrowserContext();
  loadBrowserScript(context, 'subagent-state-core.js');
  loadBrowserScript(context, 'subagent-state-selectors.js');
  loadBrowserScript(context, 'subagent-state-reducer.js');
  loadBrowserScript(context, 'subagent-state.js');
  loadBrowserScript(context, 'subagent-runtime.js');
  loadBrowserScript(context, 'subagent-app-bridge.js');
  loadBrowserScript(context, 'subagent-event-adapter.js');
  return context.window;
}

function reduce(browser, state, action) {
  return browser.AgentSubagentState.reduceSubagentUiEvent(state, action, {
    normalizeCoordinatorPayload: browser.AgentSubagentState.normalizeCoordinatorPayload,
  });
}

function testCoordinatorWithDuplicateNamesKeepsDistinctDetails() {
  const browser = loadSubagentStateModules();
  let state = browser.AgentSubagentState.createEmptySubagentUiState();

  state = reduce(browser, state, {
    kind: 'coordinator',
    payload: {
      coordinator_id: 'dc_1',
      visible_revision: 1,
      agents: [
        {
          task_id: '',
          session_id: '',
          coordinator_id: 'dc_1',
          task_key: 'scope_a',
          name: 'explore',
          description: 'explore',
          subagent_type: 'explore',
          status: 'running',
          scope_path: '/repo/a',
        },
        {
          task_id: '',
          session_id: '',
          coordinator_id: 'dc_1',
          task_key: 'scope_b',
          name: 'explore',
          description: 'explore',
          subagent_type: 'explore',
          status: 'running',
          scope_path: '/repo/b',
        },
      ],
    },
  });

  const details = browser.AgentSubagentState.orderedSubagentDetails(state);

  assert.strictEqual(details.length, 2, 'expected two distinct subagent details');
  assert.notStrictEqual(details[0].key, details[1].key, 'expected unique detail keys');
}

function testSubagentEventsMapToDistinctFallbackKeys() {
  const browser = loadSubagentStateModules();
  let state = browser.AgentSubagentState.createEmptySubagentUiState();

  state = reduce(browser, state, {
    kind: 'subagent_event',
    key: browser.AgentSubagentState.subagentEventKey({
      coordinator_id: 'dc_2',
      task_key: 'scope_a',
      task: 'explore',
      subagent_type: 'explore',
    }),
    payload: {
      type: 'subagent_progress',
      coordinator_id: 'dc_2',
      task_key: 'scope_a',
      task: 'explore',
      subagent_type: 'explore',
      status: 'running',
      detail: 'scan a',
    },
    entry: {
      type: 'subagent_progress',
      status: 'running',
      task: 'explore',
      detail: 'scan a',
      text: 'scan a',
      ts: 1,
    },
  });

  state = reduce(browser, state, {
    kind: 'subagent_event',
    key: browser.AgentSubagentState.subagentEventKey({
      coordinator_id: 'dc_2',
      task_key: 'scope_b',
      task: 'explore',
      subagent_type: 'explore',
    }),
    payload: {
      type: 'subagent_progress',
      coordinator_id: 'dc_2',
      task_key: 'scope_b',
      task: 'explore',
      subagent_type: 'explore',
      status: 'running',
      detail: 'scan b',
    },
    entry: {
      type: 'subagent_progress',
      status: 'running',
      task: 'explore',
      detail: 'scan b',
      text: 'scan b',
      ts: 2,
    },
  });

  const keys = [...state.details.keys()].sort();
  assert.deepStrictEqual(keys, ['dc_2:scope_a', 'dc_2:scope_b']);
}

function testWebsocketShapedFlowPreservesTwoSubagents() {
  const browser = loadSubagentStateModules();
  const runtime = browser.AgentSubagentRuntime.createSubagentRuntime({
    createEmptyState: browser.AgentSubagentState.createEmptySubagentUiState,
    reduceState(state, action, helpers) {
      return browser.AgentSubagentState.reduceSubagentUiEvent(state, action, helpers);
    },
    hydrateState(snapshot, helpers) {
      return browser.AgentSubagentState.hydrateStateFromSnapshot(snapshot, helpers);
    },
  });
  const bridge = browser.AgentSubagentAppBridge.createSubagentAppBridge({
    ensureSubagentRuntime: () => runtime,
    currentSelectedSubagentKeySelector: browser.AgentSubagentState.currentSelectedSubagentKey,
    orderedSubagentDetailsSelector: browser.AgentSubagentState.orderedSubagentDetails,
    subagentSummarySelector: browser.AgentSubagentState.subagentSummary,
    visibleSubagentTabsSelector: browser.AgentSubagentState.visibleSubagentTabs,
    selectedSubagentDetailViewSelector: browser.AgentSubagentState.selectedSubagentDetailView,
    currentInteractiveBlockerSelector: browser.AgentSubagentState.currentInteractiveBlocker,
    subagentEventsForAgentSelector: browser.AgentSubagentState.subagentEventsForAgent,
    normalizeCoordinatorPayload: browser.AgentSubagentState.normalizeCoordinatorPayload,
    subagentEventKey: browser.AgentSubagentState.subagentEventKey,
  });
  const adapter = browser.AgentSubagentEventAdapter.createSubagentEventAdapter({
    handlePetToolMessage() {},
    pushSubagentEvent(data) {
      bridge.pushSubagentEvent(data);
    },
    reduceSubagentUiEvent(action) {
      bridge.reduceSubagentUiEvent(action);
    },
    renderCoordinatorPanel() {},
    clearPendingReasoningCard() {},
    updateCoordinatorStatus(data) {
      bridge.applyCoordinatorPayload(data);
    },
    handleCoordinatorOutput() {},
    setAssistantIdle() {},
    syncSendState() {},
  });

  adapter.handle({
    type: 'subagent_progress',
    coordinator_id: 'dc_3',
    task_key: 'scope_a',
    task: 'explore',
    subagent_type: 'explore',
    status: 'running',
    detail: 'scan a',
  });
  adapter.handle({
    type: 'subagent_progress',
    coordinator_id: 'dc_3',
    task_key: 'scope_b',
    task: 'explore',
    subagent_type: 'explore',
    status: 'running',
    detail: 'scan b',
  });
  adapter.handle({
    type: 'coordinator_status',
    coordinator: {
      coordinator_id: 'dc_3',
      visible_revision: 2,
      agents: [
        {
          coordinator_id: 'dc_3',
          task_key: 'scope_a',
          description: 'explore',
          name: 'explore',
          subagent_type: 'explore',
          status: 'running',
          scope_path: '/repo/a',
        },
        {
          coordinator_id: 'dc_3',
          task_key: 'scope_b',
          description: 'explore',
          name: 'explore',
          subagent_type: 'explore',
          status: 'running',
          scope_path: '/repo/b',
        },
      ],
    },
  });

  const details = browser.AgentSubagentState.orderedSubagentDetails(runtime.currentState());
  const keys = details.map((detail) => detail.key).sort();

  assert.deepStrictEqual(keys, ['dc_3:scope_a', 'dc_3:scope_b']);
}

function run() {
  testCoordinatorWithDuplicateNamesKeepsDistinctDetails();
  testSubagentEventsMapToDistinctFallbackKeys();
  testWebsocketShapedFlowPreservesTwoSubagents();
  console.log('subagent-state-core tests: PASS');
}

run();
