(function initSubagentAppBridgeModule(global) {
  function createSubagentAppBridge(config) {
    const api = config || {};

    function runtime() {
      return api.ensureSubagentRuntime?.() || null;
    }

    function select(selector, ...args) {
      return runtime()?.select?.(selector, ...args);
    }

    function currentSelectedSubagentKey() {
      return select(api.currentSelectedSubagentKeySelector) || '';
    }

    function effectiveSelectedSubagentKey() {
      return select(api.effectiveSelectedSubagentKeySelector) || '';
    }

    function orderedCoordinatorStates() {
      return select(api.orderedCoordinatorStatesSelector) || [];
    }

    function orderedSubagentDetails() {
      return api.orderedSubagentDetailsSelector
        ? (select(api.orderedSubagentDetailsSelector) || [])
        : [];
    }

    function subagentSummary() {
      return api.subagentSummarySelector
        ? (select(api.subagentSummarySelector) || { total: 0, blocked: 0, running: 0, done: 0, failed: 0 })
        : { total: 0, blocked: 0, running: 0, done: 0, failed: 0 };
    }

    function visibleSubagentTabs(limit) {
      return select(api.visibleSubagentTabsSelector, limit || 8) || [];
    }

    function selectedSubagentDetailView(chatId) {
      return api.selectedSubagentDetailViewSelector
        ? (select(api.selectedSubagentDetailViewSelector, chatId) || null)
        : null;
    }

    function currentInteractiveBlocker(chatId) {
      return api.currentInteractiveBlockerSelector
        ? (select(api.currentInteractiveBlockerSelector, chatId) || null)
        : null;
    }

    function dispatch(action) {
      runtime()?.dispatch?.(action, {
        normalizeCoordinatorPayload: api.normalizeCoordinatorPayload,
      });
    }

    function reduceSubagentUiEvent(action) {
      dispatch(action);
    }

    function makeSubagentEventAction(data) {
      return api.makeSubagentEventAction?.(data, {
        now: () => Date.now(),
        subagentEventKey: api.subagentEventKey,
        formatEventText: api.formatSubagentEvent,
      }) || null;
    }

    function pushSubagentEvent(data) {
      const action = makeSubagentEventAction(data);
      if (!action) {
        return;
      }
      dispatch(action);
    }

    function selectSubagentTab(detailKey) {
      const key = api.trimText?.(detailKey) || '';
      if (!key) {
        return;
      }
      dispatch({ kind: 'select_tab', key });
    }

    function subagentEventsForAgent(agent) {
      const state = runtime()?.currentState?.();
      return state && typeof api.subagentEventsForAgentSelector === 'function'
        ? api.subagentEventsForAgentSelector(agent, state)
        : [];
    }

    return {
      currentSelectedSubagentKey,
      currentInteractiveBlocker,
      dispatch,
      effectiveSelectedSubagentKey,
      orderedCoordinatorStates,
      orderedSubagentDetails,
      pushSubagentEvent,
      reduceSubagentUiEvent,
      selectSubagentTab,
      selectedSubagentDetailView,
      subagentEventsForAgent,
      subagentSummary,
      visibleSubagentTabs,
    };
  }

  global.AgentSubagentAppBridge = {
    createSubagentAppBridge,
  };
})(window);
