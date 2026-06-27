(function initSubagentBootstrapModule(global) {
  function createSubagentBootstrap(config) {
    const api = config || {};
    let runtime = null;
    let uiOrchestrator = null;
    let chatTransport = null;
    let transport = null;

    function ensureRuntime() {
      if (runtime) {
        return runtime;
      }
      const runtimeFactory = typeof api.createSubagentRuntime === 'function'
        ? api.createSubagentRuntime
        : function createFallbackSubagentRuntime(fallbackConfig) {
          const fallbackApi = fallbackConfig || {};
          const createEmptyState = typeof fallbackApi.createEmptyState === 'function'
            ? fallbackApi.createEmptyState
            : (() => ({}));
          const reduceState = typeof fallbackApi.reduceState === 'function'
            ? fallbackApi.reduceState
            : ((state) => state);
          const hydrateState = typeof fallbackApi.hydrateState === 'function'
            ? fallbackApi.hydrateState
            : ((snapshot) => snapshot);
          let state = createEmptyState();
          return {
            currentState() {
              return state;
            },
            reset() {
              state = createEmptyState();
              return state;
            },
            dispatch(action, helpers) {
              state = reduceState(state, action, helpers);
              return state;
            },
            replaceSnapshot(snapshot, helpers) {
              state = hydrateState(snapshot, helpers);
              return state;
            },
            select(selector, ...args) {
              if (typeof selector !== 'function') {
                return undefined;
              }
              return selector(state, ...args);
            },
          };
        };

      runtime = runtimeFactory({
        createEmptyState: api.createEmptySubagentUiState,
        reduceState(state, action, helpers) {
          return api.reduceSubagentUiEvent?.(state, action, helpers);
        },
        hydrateState(snapshot, helpers) {
          return api.hydrateStateFromSnapshot?.(snapshot, helpers);
        },
      });
      return runtime;
    }

    function ensureUiOrchestrator() {
      if (uiOrchestrator || typeof api.createSubagentUiOrchestrator !== 'function') {
        return uiOrchestrator;
      }
      uiOrchestrator = api.createSubagentUiOrchestrator({
        dispatch(action) {
          api.reduceSubagentUiEventInPlace?.(action);
        },
        replaceSnapshot(snapshot, helpers) {
          ensureRuntime()?.replaceSnapshot?.(snapshot, helpers);
        },
        makeHydrateInput: api.makeHydrateInput,
        currentInteractiveBlocker: api.currentInteractiveBlocker,
        makeInteractiveControllerState: api.makeInteractiveControllerState,
        applyInteractiveControllerState(state) {
          api.ensureInteractiveController?.()?.apply?.(state);
        },
        clearInteractiveController() {
          api.ensureInteractiveController?.()?.clear?.();
        },
        renderDetailPanel() {
          api.renderSubagentDetailPanel?.();
        },
        renderCoordinatorPanel() {
          api.renderCoordinatorPanelFromState?.();
        },
        makeCoordinatorAction: api.makeCoordinatorAction,
        makeSubagentSessionAction: api.makeSubagentSessionAction,
        normalizeCoordinatorPayload: api.normalizeCoordinatorPayload,
        markCoordinatorActive(coordinatorId) {
          api.ensureCoordinatorPanelController?.()?.markActive?.(coordinatorId);
        },
        makeInteractiveDismissActions: api.makeInteractiveDismissActions,
      });
      return uiOrchestrator;
    }

    function ensureChatTransport() {
      if (chatTransport || typeof api.createSubagentChatTransport !== 'function') {
        return chatTransport;
      }
      chatTransport = api.createSubagentChatTransport({
        getChatId: api.getChatId,
        getLastMessageSeq: api.getLastMessageSeq,
        setLastMessageSeq: api.setLastMessageSeq,
        getMessageCount: api.getMessageCount,
        isStopRequested: api.isStopRequested,
        setStatus: api.setStatus,
        clearAgentState: api.clearAgentState,
        onSocketOpenForPet: api.onSocketOpenForPet,
        saveReconnectSession: api.saveReconnectSession,
        loadSubagentStateSnapshot: api.loadSubagentStateSnapshot,
        refreshContextStats: api.refreshContextStats,
        showReconnectToast: api.showReconnectToast,
        hideReconnectToast: api.hideReconnectToast,
        syncSendState: api.syncSendState,
        setAssistantIdle: api.setAssistantIdle,
        appendHistoryMessage: api.appendHistoryMessage,
        applyUploadedImage: api.applyUploadedImage,
        clearPendingImage: api.clearPendingImage,
        clearPendingReasoningCard: api.clearPendingReasoningCard,
        addToolMessage: api.addToolMessage,
        onToolActivity: api.onToolActivity,
        onPetResponse: api.onPetResponse,
        resetCurrentToolGroup: api.resetCurrentToolGroup,
        setPendingReasoningCardFromContent: api.setPendingReasoningCardFromContent,
        consumeAssistantText: api.consumeAssistantText,
        commitPendingReasoningCard: api.commitPendingReasoningCard,
        appendAssistantMessage: api.appendAssistantMessage,
        handleAgentStateMessage: api.handleAgentStateMessage,
        handleSelfTestResult: api.handleSelfTestResult,
        startPingLoop() {
          ensureTransport()?.startPingLoop?.(() => {
            api.sendPing?.();
          }, 15000);
        },
        startStatsLoop() {
          ensureTransport()?.startStatsLoop?.(() => api.refreshContextStats?.(), 8000);
        },
        clearRuntimeTimers() {
          ensureTransport()?.clearRuntimeTimers?.();
        },
        scheduleReconnect() {
          ensureTransport()?.scheduleReconnect?.(() => api.ensureSocketConnection?.(), 1500);
        },
        handleParsedMessage(raw) {
          const liveTransport = ensureTransport();
          if (!liveTransport?.handleWebsocketMessage) {
            return false;
          }
          const result = liveTransport.handleWebsocketMessage(raw);
          return result?.handled === true;
        },
        handlePlaintextMessage: api.handlePlaintextMessage,
      });
      return chatTransport;
    }

    function ensureTransport() {
      if (transport || typeof api.createSubagentTransport !== 'function') {
        return transport;
      }
      const liveChatTransport = ensureChatTransport();
      const transportHandlers = liveChatTransport?.createHandlers?.() || {
        handleAgentStateMessage: api.handleAgentStateMessage,
      };
      const transportLifecycleActions = liveChatTransport?.createLifecycleActions?.() || {};

      transport = api.createSubagentTransport({
        parseIncomingPayload: api.parseIncomingPayload,
        getEventAdapter() {
          return api.ensureSubagentEventAdapter?.();
        },
        isSubagentPayload: api.isSubagentPayload,
        fetchImpl: api.fetchImpl,
        applySnapshot(snapshot, helpers) {
          ensureUiOrchestrator()?.applySnapshot?.(snapshot, helpers);
        },
        applySessionPayload(payload, helpers) {
          return ensureUiOrchestrator()?.applySessionPayload?.(payload, helpers);
        },
        getRuntimeState() {
          return ensureRuntime()?.currentState?.() || null;
        },
        ...transportHandlers,
        lifecycleActions: transportLifecycleActions,
      });
      return transport;
    }

    return {
      ensureRuntime,
      ensureUiOrchestrator,
      ensureChatTransport,
      ensureTransport,
    };
  }

  global.AgentSubagentBootstrap = {
    createSubagentBootstrap,
  };
})(window);
