(function initSubagentUiOrchestratorModule(global) {
  function createSubagentUiOrchestrator(config) {
    const api = config || {};

    function renderAll() {
      api.renderCoordinatorPanel?.();
    }

    function dismissInteractiveRequest(request, helpers) {
      if (!request || typeof request !== 'object') {
        return [];
      }
      const actions = typeof api.makeInteractiveDismissActions === 'function'
        ? api.makeInteractiveDismissActions(request, helpers)
        : [];
      for (const action of actions) {
        api.dispatch?.(action);
      }
      api.clearInteractiveController?.();
      api.renderDetailPanel?.();
      return actions;
    }

    function applySnapshot(snapshot, helpers) {
      api.clearInteractiveController?.();
      const hydrateInput = typeof api.makeHydrateInput === 'function'
        ? api.makeHydrateInput(snapshot, helpers)
        : {
          snapshot,
          chatId: helpers?.chatId,
          interactiveUiConfig: helpers?.interactiveUiConfig,
        };
      api.replaceSnapshot?.(hydrateInput.snapshot, {
        chatId: hydrateInput.chatId,
        interactiveUiConfig: hydrateInput.interactiveUiConfig,
      });

      const blocker = typeof api.currentInteractiveBlocker === 'function'
        ? api.currentInteractiveBlocker()
        : null;
      if (blocker?.request_type && blocker?.request_id) {
        const restoredState = typeof api.makeInteractiveControllerState === 'function'
          ? api.makeInteractiveControllerState(blocker)
          : null;
        if (restoredState) {
          api.applyInteractiveControllerState?.(restoredState);
        }
      }
      renderAll();
      return hydrateInput;
    }

    function applyCoordinatorPayload(payload, helpers) {
      const action = typeof api.makeCoordinatorAction === 'function'
        ? api.makeCoordinatorAction(payload)
        : { kind: 'coordinator', payload };
      api.dispatch?.(action);
      const state = typeof api.normalizeCoordinatorPayload === 'function'
        ? api.normalizeCoordinatorPayload(payload)
        : payload;
      if (helpers?.markActive !== false) {
        api.markCoordinatorActive?.(state?.coordinator_id);
      }
      renderAll();
      return state;
    }

    function applyCursorPayload(payload) {
      if (!payload || typeof payload !== 'object') {
        return null;
      }
      api.dispatch?.({ kind: 'cursor', payload });
      renderAll();
      return payload;
    }

    function applySessionPayload(payload, helpers) {
      const action = typeof api.makeSubagentSessionAction === 'function'
        ? api.makeSubagentSessionAction(payload, helpers)
        : { kind: 'subagent_session', payload: payload?.session || payload, replaceChildSession: helpers?.replaceChildSession === true };
      if (!action) {
        return null;
      }
      api.dispatch?.(action);
      renderAll();
      return action;
    }

    return {
      renderAll,
      dismissInteractiveRequest,
      applySnapshot,
      applyCoordinatorPayload,
      applyCursorPayload,
      applySessionPayload,
    };
  }

  global.AgentSubagentUiOrchestrator = {
    createSubagentUiOrchestrator,
  };
})(window);
