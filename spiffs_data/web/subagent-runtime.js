(function initSubagentRuntimeModule(global) {
  function createSubagentRuntime(config) {
    const api = config || {};
    const createEmptyState = typeof api.createEmptyState === 'function'
      ? api.createEmptyState
      : (() => ({}));
    const reduceState = typeof api.reduceState === 'function'
      ? api.reduceState
      : ((state) => state);
    const hydrateState = typeof api.hydrateState === 'function'
      ? api.hydrateState
      : ((snapshot) => snapshot);

    let state = createEmptyState();

    function currentState() {
      return state;
    }

    function reset() {
      state = createEmptyState();
      return state;
    }

    function dispatch(action, helpers) {
      state = reduceState(state, action, helpers);
      return state;
    }

    function replaceSnapshot(snapshot, helpers) {
      state = hydrateState(snapshot, {
        ...(helpers && typeof helpers === 'object' ? helpers : {}),
        previousState: state,
      });
      return state;
    }

    function select(selector, ...args) {
      if (typeof selector !== 'function') {
        return undefined;
      }
      return selector(state, ...args);
    }

    return {
      currentState,
      reset,
      dispatch,
      replaceSnapshot,
      select,
    };
  }

  global.AgentSubagentRuntime = {
    createSubagentRuntime,
  };
})(window);
