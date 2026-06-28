(function initSessionStateRuntimeModule(global) {
  function createSessionStateRuntime(config) {
    const api = config || {};
    let sessionRestore = null;
    let snapshotToken = 0;

    function currentChatId() {
      return String(api.getChatId?.() || '').trim();
    }

    function isCurrentChatId(candidate) {
      return String(candidate || '').trim() === currentChatId();
    }

    function emptySnapshot() {
      return typeof api.emptySnapshot === 'function'
        ? api.emptySnapshot()
        : { coordinators: [] };
    }

    function extractLastSeqFromHistory(history) {
      const items = Array.isArray(history) ? history : [];
      let maxSeq = 0;
      for (const item of items) {
        const seq = Number(item?.seq) || 0;
        if (seq > maxSeq) {
          maxSeq = seq;
        }
      }
      return maxSeq;
    }

    function replaceSubagentStateSnapshot(snapshot, options) {
      return api.applySubagentSnapshot?.(snapshot, {
        chatId: currentChatId(),
        interactiveUiConfig: api.interactiveUiConfig,
        ...(options && typeof options === 'object' ? options : {}),
      });
    }

    async function loadSubagentStateSnapshot(targetChatId) {
      const requestedChatId = String(targetChatId || currentChatId()).trim();
      const transport = api.ensureSubagentTransport?.();
      const token = ++snapshotToken;
      const transportOptions = {
        interactiveUiConfig: api.interactiveUiConfig,
        emptySnapshot: emptySnapshot(),
        isCurrentChatId,
      };

      if (transport?.loadUnifiedSessionState) {
        const unified = await transport.loadUnifiedSessionState(requestedChatId, transportOptions);
        if (unified?.status === 'ok' || unified?.status === 'stale' || unified?.status === 'empty') {
          return unified;
        }
      }

      if (transport?.loadSnapshot) {
        return transport.loadSnapshot(requestedChatId, transportOptions);
      }

      if (!requestedChatId) {
        replaceSubagentStateSnapshot(emptySnapshot());
        return { chatId: requestedChatId, status: 'empty' };
      }

      try {
        const resp = await api.fetchImpl?.(
          `/api/subagent_state?chat_id=${encodeURIComponent(requestedChatId)}`,
          { cache: 'no-store' }
        );
        if (token !== snapshotToken || !isCurrentChatId(requestedChatId)) {
          return { chatId: requestedChatId, status: 'stale' };
        }
        if (resp?.status === 404) {
          replaceSubagentStateSnapshot(emptySnapshot());
          return { chatId: requestedChatId, status: 'empty' };
        }
        if (!resp?.ok) {
          return { chatId: requestedChatId, status: 'error' };
        }
        const data = await resp.json();
        if (token !== snapshotToken || !isCurrentChatId(requestedChatId)) {
          return { chatId: requestedChatId, status: 'stale' };
        }
        replaceSubagentStateSnapshot(data);
        return { chatId: requestedChatId, status: 'ok', data };
      } catch (_) {
        if (token !== snapshotToken || !isCurrentChatId(requestedChatId)) {
          return { chatId: requestedChatId, status: 'stale' };
        }
        return { chatId: requestedChatId, status: 'error' };
      }
    }

    function ensureSessionRestore() {
      if (sessionRestore || typeof api.createSessionRestore !== 'function') {
        return sessionRestore;
      }

      sessionRestore = api.createSessionRestore({
        fetchImpl: api.fetchImpl,
        async fetchUnifiedSessionState(targetChatId) {
          const requestedChatId = String(targetChatId || '').trim();
          const transport = api.ensureSubagentTransport?.();
          if (!requestedChatId || !transport?.loadUnifiedSessionState) {
            return null;
          }
          const result = await transport.loadUnifiedSessionState(requestedChatId, {
            interactiveUiConfig: api.interactiveUiConfig,
            emptySnapshot: emptySnapshot(),
            isCurrentChatId,
          });
          return result?.status === 'ok' ? result : null;
        },
        async fetchSessionHistory(targetChatId) {
          const requestedChatId = String(targetChatId || '').trim();
          const transport = api.ensureSubagentTransport?.();
          if (!requestedChatId || !transport?.loadUnifiedSessionState) {
            return null;
          }
          const result = await transport.loadUnifiedSessionState(requestedChatId, {
            interactiveUiConfig: api.interactiveUiConfig,
            emptySnapshot: emptySnapshot(),
            isCurrentChatId,
          });
          return Array.isArray(result?.history) ? result.history : null;
        },
        async restoreSessionState(targetChatId, options = {}) {
          const requestedChatId = String(targetChatId || '').trim();
          const opts = options && typeof options === 'object' ? options : {};
          const transport = api.ensureSubagentTransport?.();

          if (!requestedChatId) {
            if (opts.restoreSubagent !== false) {
              replaceSubagentStateSnapshot(emptySnapshot());
            }
            return {
              chatId: requestedChatId,
              status: 'empty',
              history: [],
              restoredHistory: false,
              restoredSubagent: opts.restoreSubagent !== false,
              stale: false,
            };
          }

          if (transport?.restoreSessionState) {
            return transport.restoreSessionState(requestedChatId, {
              ...opts,
              interactiveUiConfig: api.interactiveUiConfig,
              emptySnapshot: emptySnapshot(),
              fetchSessionHistory: fetchSessionHistoryMessages,
              isCurrentChatId,
            });
          }

          const history = await fetchSessionHistoryMessages(requestedChatId);
          return {
            chatId: requestedChatId,
            status: Array.isArray(history) ? 'ok' : 'partial',
            history: Array.isArray(history) ? history : [],
            restoredHistory: Array.isArray(history),
            restoredSubagent: false,
            stale: !isCurrentChatId(requestedChatId),
          };
        },
        loadSubagentStateSnapshot,
        renderHistoryMessages: api.renderHistoryMessages,
        replaceSubagentStateSnapshot,
        renderSessions: api.renderSessions,
        saveReconnectSession: api.saveReconnectSession,
        refreshContextStats: api.refreshContextStats,
        showReconnectToast: api.showReconnectToast,
        setSelectedSessionId(nextChatId) {
          api.setSelectedSessionId?.(nextChatId);
        },
        isCurrentChatId,
      });

      return sessionRestore;
    }

    async function fetchSessionHistoryMessages(targetChatId) {
      return ensureSessionRestore()?.fetchSessionHistoryMessages?.(targetChatId) || null;
    }

    async function restoreSessionViewState(targetChatId, options) {
      return ensureSessionRestore()?.restoreSessionViewState?.(targetChatId || currentChatId(), options) || {
        history: [],
        restoredHistory: false,
        restoredSubagent: false,
        stale: false,
      };
    }

    async function reconcileCurrentSessionHistory(targetChatId, options) {
      const requestedChatId = String(targetChatId || currentChatId()).trim();
      if (!requestedChatId) return false;

      const opts = options && typeof options === 'object' ? options : {};
      const minMessageCount = Number(opts.minMessageCount) || 0;
      const minLastSeq = Number(opts.minLastSeq) || 0;
      const history = await fetchSessionHistoryMessages(requestedChatId);
      if (!Array.isArray(history) || !isCurrentChatId(requestedChatId)) {
        return false;
      }

      const historyLastSeq = extractLastSeqFromHistory(history);
      if (history.length < minMessageCount) return false;
      if (historyLastSeq < minLastSeq || historyLastSeq < (Number(api.getLastMessageSeq?.()) || 0)) {
        return false;
      }

      api.renderHistoryMessages?.(history);
      api.setSelectedSessionId?.(requestedChatId);
      api.renderSessions?.();
      api.saveReconnectSession?.();
      return true;
    }

    function scheduleCurrentSessionHistoryReconcile(targetChatId, options) {
      const requestedChatId = String(targetChatId || currentChatId()).trim();
      if (!requestedChatId) return;

      const opts = options && typeof options === 'object' ? options : {};
      const attempt = Number(opts.attempt) || 0;
      const maxAttempts = Math.max(0, Number(opts.maxAttempts) || 6);
      const delayMs = Number(opts.delayMs) || 160;
      const minMessageCount = Number(opts.minMessageCount) || Number(api.getMessageCount?.()) || 0;
      const minLastSeq = Number(opts.minLastSeq) || Number(api.getLastMessageSeq?.()) || 0;

      setTimeout(() => {
        if (!isCurrentChatId(requestedChatId)) return;
        reconcileCurrentSessionHistory(requestedChatId, {
          minMessageCount,
          minLastSeq,
        }).then((ok) => {
          if (ok || !isCurrentChatId(requestedChatId) || attempt >= maxAttempts) {
            return;
          }
          scheduleCurrentSessionHistoryReconcile(requestedChatId, {
            attempt: attempt + 1,
            maxAttempts,
            minMessageCount,
            minLastSeq,
            delayMs: Math.min(delayMs * 2, 1200),
          });
        });
      }, delayMs);
    }

    return {
      ensureSessionRestore,
      fetchSessionHistoryMessages,
      restoreSessionViewState,
      reconcileCurrentSessionHistory,
      scheduleCurrentSessionHistoryReconcile,
      replaceSubagentStateSnapshot,
      loadSubagentStateSnapshot,
    };
  }

  global.AgentSessionStateRuntime = {
    createSessionStateRuntime,
  };
})(window);
