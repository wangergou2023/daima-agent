(function initSubagentPanelControllerModule(global) {
  function createSubagentPanelController(config) {
    const api = config || {};

    function renderDetailPanel() {
      if (typeof api.renderSubagentDetailPanelView !== 'function') {
        return;
      }
      const detailPanelView = typeof api.selectDetailPanelView === 'function'
        ? api.selectDetailPanelView()
        : null;
      const resolvedDetailPanelView = detailPanelView || {
        detailView: null,
        visibleTabs: [],
        orderedDetails: [],
        selectedKey: '',
      };
      api.renderSubagentDetailPanelView({
        panelEl: api.panelEl,
        titleEl: api.titleEl,
        metaEl: api.metaEl,
        blockersEl: api.blockersEl,
        framesEl: api.framesEl,
        outputEl: api.outputEl,
        detailView: resolvedDetailPanelView.detailView,
        visibleTabs: resolvedDetailPanelView.visibleTabs || [],
        orderedDetails: resolvedDetailPanelView.orderedDetails || [],
        selectedKey: resolvedDetailPanelView.selectedKey || '',
        sessionRailEl: api.sessionRailEl,
        detailTabsEl: api.detailTabsEl,
        onSelectDetail(detailKey) {
          api.onSelectDetail?.(detailKey);
        },
        makeReasoningNode: api.makeReasoningNode,
        renderAssistantMarkdown: api.renderAssistantMarkdown,
      });
    }

    function syncDockState() {
      if (!api.detailPanelEl) {
        return;
      }
      const hasCoordinator = (api.orderedCoordinatorStates?.() || []).length > 0;
      const panelState = api.coordinatorPanelState?.() || { visible: false, dismissed: false };
      const dockWithCoordinator = hasCoordinator &&
        panelState.visible === true &&
        panelState.dismissed !== true;
      api.detailPanelEl.classList.toggle('docked-with-coordinator', dockWithCoordinator);
      api.detailPanelEl.classList.toggle('docked-standalone', !dockWithCoordinator);
    }

    function renderCoordinatorPanel() {
      const controller = api.ensureCoordinatorPanelController?.();
      if (!api.coordinatorPanelEl || !api.coordinatorAgentsEl || !api.renderCoordinatorPanelView || !controller) {
        return;
      }

      renderDetailPanel();
      const panelView = typeof api.selectCoordinatorPanelView === 'function'
        ? api.selectCoordinatorPanelView(controller.state?.() || { visible: false, dismissed: false })
        : null;
      const resolvedPanelView = panelView || {
        orderedStates: [],
        detailStates: [],
        summary: api.subagentSummary?.() || { total: 0, blocked: 0, running: 0, done: 0, failed: 0 },
        coordinatorDismissed: controller.state?.().dismissed === true,
        coordinatorVisible: controller.state?.().visible === true,
      };
      const orderedStates = resolvedPanelView.orderedStates || [];
      if (!orderedStates.length) {
        if (api.coordinatorAgentsEl) {
          api.coordinatorAgentsEl.innerHTML = '';
        }
        hideCoordinatorPanel();
        return;
      }

      const next = api.renderCoordinatorPanelView({
        panelEl: api.coordinatorPanelEl,
        agentsEl: api.coordinatorAgentsEl,
        orderedStates: resolvedPanelView.orderedStates || [],
        detailStates: resolvedPanelView.detailStates || [],
        summary: resolvedPanelView.summary || api.subagentSummary?.() || { total: 0, blocked: 0, running: 0, done: 0, failed: 0 },
        coordinatorDismissed: resolvedPanelView.coordinatorDismissed === true,
        coordinatorVisible: resolvedPanelView.coordinatorVisible === true,
        renderCoordinatorAgent: api.renderCoordinatorAgent,
        coordinatorSummaryText: api.coordinatorSummaryText,
      });
      controller.render(next?.coordinatorVisible === true);
    }

    function hideCoordinatorPanel() {
      const controller = api.ensureCoordinatorPanelController?.();
      if (!controller) {
        return;
      }
      const panelState = controller.state?.() || { visible: false };
      const next = api.hideCoordinatorPanelView
        ? api.hideCoordinatorPanelView({
          panelEl: api.coordinatorPanelEl,
          agentsEl: api.coordinatorAgentsEl,
          coordinatorVisible: panelState.visible === true,
          onBeforeHide() {
            controller.hide?.();
          },
        })
        : { coordinatorVisible: false };
      controller.render(next?.coordinatorVisible === true);
    }

    function closeCoordinatorPanel() {
      const controller = api.ensureCoordinatorPanelController?.();
      if (!controller) {
        return;
      }
      controller.close?.();
      if (api.hideCoordinatorPanelView) {
        api.hideCoordinatorPanelView({
          panelEl: api.coordinatorPanelEl,
          agentsEl: api.coordinatorAgentsEl,
          coordinatorVisible: true,
        });
      }
    }

    function openCoordinatorPanel() {
      const controller = api.ensureCoordinatorPanelController?.();
      if (!controller) {
        return;
      }
      controller.open?.();
      renderDetailPanel();
      const panelView = typeof api.selectCoordinatorPanelView === 'function'
        ? api.selectCoordinatorPanelView(controller.state?.() || { visible: true, dismissed: false })
        : null;
      const resolvedPanelView = panelView || {
        orderedStates: api.orderedCoordinatorStates?.() || [],
        detailStates: [],
        summary: api.subagentSummary?.() || { total: 0, blocked: 0, running: 0, done: 0, failed: 0 },
        coordinatorDismissed: false,
        coordinatorVisible: true,
      };
      const orderedStates = resolvedPanelView.orderedStates || [];
      if (!orderedStates.length) {
        hideCoordinatorPanel();
        return;
      }
      const next = api.renderCoordinatorPanelView?.({
        panelEl: api.coordinatorPanelEl,
        agentsEl: api.coordinatorAgentsEl,
        orderedStates,
        detailStates: resolvedPanelView.detailStates || [],
        summary: resolvedPanelView.summary || api.subagentSummary?.() || { total: 0, blocked: 0, running: 0, done: 0, failed: 0 },
        coordinatorDismissed: false,
        coordinatorVisible: true,
        renderCoordinatorAgent: api.renderCoordinatorAgent,
        coordinatorSummaryText: api.coordinatorSummaryText,
      }) || { coordinatorVisible: true };
      controller.render(next?.coordinatorVisible !== false);
    }

    function toggleCoordinatorPanel() {
      const controller = api.ensureCoordinatorPanelController?.();
      if (!controller) {
        return;
      }
      controller.toggle?.();
      renderCoordinatorPanel();
    }

    function replaceSnapshot(snapshot, helpers) {
      const hydrateInput = api.ensureSubagentUiOrchestrator?.()?.applySnapshot?.(snapshot, helpers);
      if (hydrateInput) {
        return hydrateInput;
      }
      api.replaceSnapshotFallback?.(snapshot, helpers);
      renderCoordinatorPanel();
      return null;
    }

    function dismissInteractiveRequest(request, helpers) {
      return api.ensureSubagentUiOrchestrator?.()?.dismissInteractiveRequest?.(request, helpers) || [];
    }

    function applyCoordinatorPayload(payload, helpers) {
      const orchestratorState = api.ensureSubagentUiOrchestrator?.()?.applyCoordinatorPayload?.(payload, helpers);
      if (orchestratorState) {
        return orchestratorState;
      }
      api.applyCoordinatorPayloadFallback?.(payload, helpers);
      renderCoordinatorPanel();
      return null;
    }

    return {
      applyCoordinatorPayload,
      closeCoordinatorPanel,
      dismissInteractiveRequest,
      hideCoordinatorPanel,
      openCoordinatorPanel,
      renderCoordinatorPanel,
      renderDetailPanel,
      replaceSnapshot,
      syncDockState,
      toggleCoordinatorPanel,
    };
  }

  global.AgentSubagentPanelController = {
    createSubagentPanelController,
  };
})(window);
