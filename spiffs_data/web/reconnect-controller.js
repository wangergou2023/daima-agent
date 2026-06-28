(function initReconnectControllerModule(global) {
  function createReconnectController(config) {
    const api = config || {};

    async function handleReconnect() {
      api.hideReconnectToast?.();
      api.setLastMessageSeq?.(0);
      await api.restoreSessionViewState?.(api.getChatId?.(), {
        requireCurrentChat: true,
        applyHistory: true,
        renderEmptyHistory: false,
        renderSessions: true,
        saveReconnect: true,
        refreshContextStats: true,
        restoreSubagent: true,
      });
    }

    return {
      handleReconnect,
    };
  }

  global.AgentReconnectController = {
    createReconnectController,
  };
})(window);
