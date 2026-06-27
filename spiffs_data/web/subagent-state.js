(function initSubagentStateFacade(global) {
  const core = global.AgentSubagentStateCore || {};
  const reducer = global.AgentSubagentStateReducer || {};
  const selectors = global.AgentSubagentStateSelectors || {};

  global.AgentSubagentState = {
    createEmptySubagentUiState: core.createEmptySubagentUiState,
    currentSelectedSubagentKey: selectors.currentSelectedSubagentKey,
    effectiveSelectedSubagentKey: selectors.effectiveSelectedSubagentKey,
    orderedCoordinatorStates: selectors.orderedCoordinatorStates,
    orderedSubagentDetails: selectors.orderedSubagentDetails,
    subagentSummary: selectors.subagentSummary,
    coordinatorSummaryText: selectors.coordinatorSummaryText,
    coordinatorPanelViewModel: selectors.coordinatorPanelViewModel,
    detailPanelViewModel: selectors.detailPanelViewModel,
    resolveAgentRole: selectors.resolveAgentRole,
    formatElapsed: selectors.formatElapsed,
    subagentFocusLabel: selectors.subagentFocusLabel,
    coordinatorAgentHint: selectors.coordinatorAgentHint,
    visibleSubagentTabs: selectors.visibleSubagentTabs,
    subagentEventKey: core.subagentEventKey,
    detailKeyForAgent: core.detailKeyForAgent,
    subagentEventsForAgent: core.subagentEventsForAgent,
    interactiveBlockerKey: selectors.interactiveBlockerKey,
    blockerForDetail: selectors.blockerForDetail,
    currentInteractiveBlocker: selectors.currentInteractiveBlocker,
    selectedSubagentDetailView: selectors.selectedSubagentDetailView,
    normalizeCoordinatorPayload: core.normalizeCoordinatorPayload,
    reduceSubagentUiEvent: reducer.reduceSubagentUiEvent,
    hydrateStateFromSnapshot: reducer.hydrateStateFromSnapshot,
    selectedSubagentDetail: selectors.selectedSubagentDetail,
  };
})(window);
