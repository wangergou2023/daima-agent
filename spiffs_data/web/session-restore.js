(function initSessionRestoreModule(global) {
  function createSessionRestore(config) {
    const api = config || {};

    async function fetchSessionHistoryMessages(targetChatId) {
      const requestedChatId = String(targetChatId || '').trim();
      if (!requestedChatId) return null;
      try {
        const unifiedResp = await api.fetchImpl?.(
          `/api/session_state?chat_id=${encodeURIComponent(requestedChatId)}`,
          { cache: 'no-store' }
        );
        if (unifiedResp?.ok) {
          const unifiedData = await unifiedResp.json();
          return Array.isArray(unifiedData?.history) ? unifiedData.history : [];
        }
      } catch (_) {}
      try {
        const resp = await api.fetchImpl?.(
          `/api/session_history?chat_id=${encodeURIComponent(requestedChatId)}`,
          { cache: 'no-store' }
        );
        if (!resp?.ok) return null;
        const data = await resp.json();
        return Array.isArray(data?.messages) ? data.messages : [];
      } catch (_) {
        return null;
      }
    }

    async function restoreSessionViewState(targetChatId, options) {
      const requestedChatId = String(targetChatId || '').trim();
      const opts = options && typeof options === 'object' ? options : {};
      if (!requestedChatId) {
        if (opts.renderEmptyHistory === true) {
          api.renderHistoryMessages?.([]);
        }
        api.replaceSubagentStateSnapshot?.({ coordinators: [] });
        return { history: [], restoredHistory: false, restoredSubagent: false, stale: false };
      }

      const requireCurrentChat = opts.requireCurrentChat !== false;
      const shouldApplyHistory = opts.applyHistory !== false;
      const shouldRestoreSubagent = opts.restoreSubagent !== false;
      const shouldRenderEmptyHistory = opts.renderEmptyHistory === true;
      const shouldRenderSessions = opts.renderSessions === true;
      const shouldSaveReconnect = opts.saveReconnect !== false;
      const shouldRefreshStats = opts.refreshContextStats === true;
      const shouldShowReconnectToast = opts.showReconnectToast === true;

      if (typeof api.restoreSessionState === 'function') {
        const restored = await api.restoreSessionState(requestedChatId, opts);
        const restoredHistory = Array.isArray(restored?.history) ? restored.history : null;
        const stale = restored?.stale === true;
        if (stale || (requireCurrentChat && api.isCurrentChatId?.(requestedChatId) !== true)) {
          return {
            history: restoredHistory || [],
            restoredHistory: false,
            restoredSubagent: false,
            stale: true,
          };
        }

        if (shouldApplyHistory) {
          if (restoredHistory) {
            api.renderHistoryMessages?.(restoredHistory);
          } else if (shouldRenderEmptyHistory) {
            api.renderHistoryMessages?.([]);
          }
        }

        if (api.isCurrentChatId?.(requestedChatId) === true) {
          api.setSelectedSessionId?.(requestedChatId);
        }
        if (shouldRenderSessions) {
          api.renderSessions?.();
        }
        if (shouldSaveReconnect) {
          api.saveReconnectSession?.();
        }
        if (shouldShowReconnectToast && restoredHistory && restoredHistory.length > 0) {
          api.showReconnectToast?.(requestedChatId, restoredHistory.length);
        }
        if (shouldRefreshStats) {
          api.refreshContextStats?.();
        }

        return {
          history: restoredHistory || [],
          restoredHistory: restored?.restoredHistory !== false && Array.isArray(restoredHistory),
          restoredSubagent: restored?.restoredSubagent !== false,
          stale: false,
        };
      }

      const historyPromise = fetchSessionHistoryMessages(requestedChatId);
      const subagentPromise = shouldRestoreSubagent
        ? api.loadSubagentStateSnapshot?.(requestedChatId)
        : Promise.resolve();

      const [history] = await Promise.all([historyPromise, subagentPromise]);
      if (requireCurrentChat && api.isCurrentChatId?.(requestedChatId) !== true) {
        return { history: Array.isArray(history) ? history : [], restoredHistory: false, restoredSubagent: false, stale: true };
      }

      const resolvedHistory = Array.isArray(history) ? history : null;
      if (shouldApplyHistory) {
        if (resolvedHistory) {
          api.renderHistoryMessages?.(resolvedHistory);
        } else if (shouldRenderEmptyHistory) {
          api.renderHistoryMessages?.([]);
        }
      }

      if (api.isCurrentChatId?.(requestedChatId) === true) {
        api.setSelectedSessionId?.(requestedChatId);
      }
      if (shouldRenderSessions) {
        api.renderSessions?.();
      }
      if (shouldSaveReconnect) {
        api.saveReconnectSession?.();
      }
      if (shouldShowReconnectToast && resolvedHistory && resolvedHistory.length > 0) {
        api.showReconnectToast?.(requestedChatId, resolvedHistory.length);
      }
      if (shouldRefreshStats) {
        api.refreshContextStats?.();
      }

      return {
        history: resolvedHistory || [],
        restoredHistory: Array.isArray(resolvedHistory),
        restoredSubagent: shouldRestoreSubagent,
        stale: false,
      };
    }

    return {
      fetchSessionHistoryMessages,
      restoreSessionViewState,
    };
  }

  global.AgentSessionRestore = {
    createSessionRestore,
  };
})(window);
