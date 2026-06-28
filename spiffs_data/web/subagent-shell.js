(function initSubagentShellModule(global) {
  function createSubagentShell(config) {
    const api = config || {};
    let subagentAppBridge = null;
    let subagentPanelController = null;
    let subagentEventAdapter = null;

    function ensureSubagentAppBridge() {
      if (subagentAppBridge || typeof api.createSubagentAppBridge !== 'function') {
        return subagentAppBridge;
      }
      subagentAppBridge = api.createSubagentAppBridge({
        ensureSubagentRuntime: api.ensureSubagentRuntime,
        currentSelectedSubagentKeySelector: api.currentSelectedSubagentKeySelector,
        currentInteractiveBlockerSelector: api.currentInteractiveBlockerSelector,
        effectiveSelectedSubagentKeySelector: api.effectiveSelectedSubagentKeySelector,
        orderedCoordinatorStatesSelector: api.orderedCoordinatorStatesSelector,
        orderedSubagentDetailsSelector: api.orderedSubagentDetailsSelector,
        selectedSubagentDetailViewSelector: api.selectedSubagentDetailViewSelector,
        subagentSummarySelector: api.subagentSummarySelector,
        visibleSubagentTabsSelector: api.visibleSubagentTabsSelector,
        subagentEventsForAgentSelector: api.subagentEventsForAgentSelector,
        normalizeSnapshot: api.normalizeSnapshot,
        normalizeCoordinatorPayload: api.normalizeCoordinatorPayload,
        makeSubagentEventAction: api.makeSubagentEventAction,
        makeCoordinatorAction: api.makeCoordinatorAction,
        subagentEventKey: api.subagentEventKey,
        formatSubagentEvent: api.formatSubagentEvent,
        markCoordinatorActive(coordinatorId) {
          api.ensureCoordinatorPanelController?.()?.markActive?.(coordinatorId);
        },
        trimText: api.trimText,
      });
      return subagentAppBridge;
    }

    function currentSelectedSubagentKey() {
      return ensureSubagentAppBridge()?.currentSelectedSubagentKey?.() || '';
    }

    function effectiveSelectedSubagentKey() {
      return ensureSubagentAppBridge()?.effectiveSelectedSubagentKey?.() || '';
    }

    function orderedCoordinatorStates() {
      return ensureSubagentAppBridge()?.orderedCoordinatorStates?.() || [];
    }

    function orderedSubagentDetails() {
      return ensureSubagentAppBridge()?.orderedSubagentDetails?.() || [];
    }

    function subagentSummary() {
      return ensureSubagentAppBridge()?.subagentSummary?.() || {
        total: 0,
        blocked: 0,
        running: 0,
        done: 0,
        failed: 0,
      };
    }

    function visibleSubagentTabs(limit) {
      return ensureSubagentAppBridge()?.visibleSubagentTabs?.(limit || 8) || [];
    }

    function subagentEventsForAgent(agent) {
      return ensureSubagentAppBridge()?.subagentEventsForAgent?.(agent) || [];
    }

    function pushSubagentEvent(data) {
      ensureSubagentAppBridge()?.pushSubagentEvent?.(data);
    }

    function reduceSubagentUiEvent(action) {
      ensureSubagentAppBridge()?.reduceSubagentUiEvent?.(action);
    }

    function selectSubagentTab(detailKey) {
      ensureSubagentAppBridge()?.selectSubagentTab?.(detailKey);
    }

    function selectedSubagentDetailView(chatId) {
      return ensureSubagentAppBridge()?.selectedSubagentDetailView?.(chatId) || null;
    }

    function currentInteractiveBlocker(chatId) {
      return ensureSubagentAppBridge()?.currentInteractiveBlocker?.(chatId) || null;
    }

    function ensureSubagentPanelController() {
      if (subagentPanelController || typeof api.createSubagentPanelController !== 'function') {
        return subagentPanelController;
      }
      subagentPanelController = api.createSubagentPanelController({
        panelEl: api.panelEl,
        titleEl: api.titleEl,
        metaEl: api.metaEl,
        blockersEl: api.blockersEl,
        framesEl: api.framesEl,
        outputEl: api.outputEl,
        detailPanelEl: api.detailPanelEl,
        sessionRailEl: api.sessionRailEl,
        detailTabsEl: api.detailTabsEl,
        coordinatorPanelEl: api.coordinatorPanelEl,
        coordinatorAgentsEl: api.coordinatorAgentsEl,
        ensureCoordinatorPanelController: api.ensureCoordinatorPanelController,
        coordinatorPanelState: api.coordinatorPanelState,
        ensureSubagentUiOrchestrator: api.ensureSubagentUiOrchestrator,
        renderSubagentDetailPanelView: api.renderSubagentDetailPanelView,
        renderCoordinatorPanelView: api.renderCoordinatorPanelView,
        hideCoordinatorPanelView: api.hideCoordinatorPanelView,
        renderCoordinatorAgent: api.renderCoordinatorAgent,
        coordinatorSummaryText: api.coordinatorSummaryText,
        makeReasoningNode: api.makeReasoningNode,
        renderAssistantMarkdown: api.renderAssistantMarkdown,
        subagentSummary,
        orderedCoordinatorStates,
        onSelectDetail(detailKey) {
          selectSubagentTab(detailKey);
          ensureSubagentPanelController()?.renderCoordinatorPanel?.();
        },
        selectDetailPanelView() {
          return api.detailPanelViewModelSelector
            ? (api.ensureSubagentRuntime?.()?.select?.(api.detailPanelViewModelSelector, api.getChatId?.(), { detailLimit: 8 }) || null)
            : {
              detailView: selectedSubagentDetailView(api.getChatId?.()),
              visibleTabs: visibleSubagentTabs(8),
              orderedDetails: orderedSubagentDetails().slice(0, 8),
              selectedKey: effectiveSelectedSubagentKey(),
            };
        },
        selectCoordinatorPanelView(panelState) {
          return api.coordinatorPanelViewModelSelector
            ? (api.ensureSubagentRuntime?.()?.select?.(api.coordinatorPanelViewModelSelector, panelState) || null)
            : {
              orderedStates: orderedCoordinatorStates(),
              detailStates: orderedSubagentDetails(),
              summary: subagentSummary(),
              coordinatorDismissed: panelState.dismissed === true,
              coordinatorVisible: panelState.visible === true,
            };
        },
        replaceSnapshotFallback(snapshot, helpers) {
          ensureSubagentAppBridge()?.replaceSnapshot?.(snapshot, {
            chatId: helpers?.chatId || api.getChatId?.(),
            interactiveUiConfig: helpers?.interactiveUiConfig || api.interactiveUiConfig,
          });
        },
        applyCoordinatorPayloadFallback(payload) {
          ensureSubagentAppBridge()?.applyCoordinatorPayload?.(payload, { markActive: true });
        },
      });
      return subagentPanelController;
    }

    function renderSubagentDetailPanel() {
      ensureSubagentPanelController()?.renderDetailPanel?.();
    }

    function syncSubagentDetailDockState() {
      ensureSubagentPanelController()?.syncDockState?.();
    }

    function renderCoordinatorPanelFromState() {
      ensureSubagentPanelController()?.renderCoordinatorPanel?.();
    }

    function handleCoordinatorOutput(payload) {
      ensureSubagentPanelController()?.applyCoordinatorPayload?.(payload, { markActive: true });
    }

    function updateCoordinatorStatus(payload) {
      ensureSubagentPanelController()?.applyCoordinatorPayload?.(payload, { markActive: true });
    }

    function hideCoordinatorPanel() {
      ensureSubagentPanelController()?.hideCoordinatorPanel?.();
    }

    function closeCoordinatorPanel() {
      ensureSubagentPanelController()?.closeCoordinatorPanel?.();
    }

    function openCoordinatorPanel() {
      ensureSubagentPanelController()?.openCoordinatorPanel?.();
    }

    function toggleCoordinatorPanel() {
      ensureSubagentPanelController()?.toggleCoordinatorPanel?.();
    }

    function closeInteractivePrompt() {
      const activeRequest = api.ensureInteractiveController?.()?.currentRequest?.() || null;
      if (!activeRequest) {
        api.ensureInteractiveController?.()?.clear?.();
        renderSubagentDetailPanel();
        return;
      }
      ensureSubagentPanelController()?.dismissInteractiveRequest?.(activeRequest, {
        now: () => Date.now(),
        interactiveBlockerKey: api.interactiveBlockerKey,
        subagentEventKey: api.subagentEventKey,
        formatEventText: api.formatSubagentEvent,
      });
    }

    function ensureSubagentEventAdapter() {
      if (subagentEventAdapter || typeof api.createSubagentEventAdapter !== 'function') {
        return subagentEventAdapter;
      }
      subagentEventAdapter = api.createSubagentEventAdapter({
        getChatId: api.getChatId,
        setInteractiveControllerState(next) {
          api.ensureInteractiveController?.()?.apply?.(next);
        },
        reduceSubagentUiEvent,
        addSystemNote: api.addSystemNote,
        renderCoordinatorPanel() {
          renderCoordinatorPanelFromState();
        },
        handlePetToolMessage() {
          api.handlePetToolMessage?.();
        },
        pushSubagentEvent,
        clearPendingReasoningCard: api.clearPendingReasoningCard,
        updateCoordinatorStatus,
        handleCoordinatorOutput,
        setAssistantIdle: api.setAssistantIdle,
        appendAssistantMessage: api.appendAssistantMessage,
        syncSendState: api.syncSendState,
        summarizeCoordinatorCompletion: api.summarizeCoordinatorCompletion,
      });
      return subagentEventAdapter;
    }

    function replaceSubagentStateSnapshot(snapshot) {
      return api.ensureSessionStateRuntime?.()?.replaceSubagentStateSnapshot?.(snapshot) ||
        ensureSubagentPanelController()?.replaceSnapshot?.(snapshot, {
          chatId: api.getChatId?.(),
          interactiveUiConfig: api.interactiveUiConfig,
        });
    }

    async function loadSubagentStateSnapshot(targetChatId) {
      return api.ensureSessionStateRuntime?.()?.loadSubagentStateSnapshot?.(targetChatId || api.getChatId?.()) || {
        chatId: String(targetChatId || api.getChatId?.() || '').trim(),
        status: 'error',
      };
    }

    function resetSubagentState() {
      api.ensureCoordinatorPanelController?.()?.reset?.();
      api.ensureSubagentRuntime?.()?.reset?.();
      api.ensureInteractiveController?.()?.clear?.();
      ensureSubagentPanelController()?.renderCoordinatorPanel?.();
    }

    return {
      closeCoordinatorPanel,
      closeInteractivePrompt,
      currentInteractiveBlocker,
      currentSelectedSubagentKey,
      effectiveSelectedSubagentKey,
      ensureEventAdapter: ensureSubagentEventAdapter,
      ensurePanelController: ensureSubagentPanelController,
      handleCoordinatorOutput,
      hideCoordinatorPanel,
      loadSubagentStateSnapshot,
      openCoordinatorPanel,
      orderedCoordinatorStates,
      orderedSubagentDetails,
      pushSubagentEvent,
      reduceSubagentUiEvent,
      resetSubagentState,
      renderCoordinatorPanelFromState,
      renderSubagentDetailPanel,
      replaceSubagentStateSnapshot,
      selectSubagentTab,
      selectedSubagentDetailView,
      subagentEventsForAgent,
      subagentSummary,
      syncSubagentDetailDockState,
      toggleCoordinatorPanel,
      updateCoordinatorStatus,
      visibleSubagentTabs,
    };
  }

  global.AgentSubagentShell = {
    createSubagentShell,
  };
})(window);
