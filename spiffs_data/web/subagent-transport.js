(function initSubagentTransportModule(global) {
  function createSubagentTransport(config) {
    const api = config || {};
    let snapshotToken = 0;
    let reconnectTimer = null;
    let pingTimer = null;
    let statsTimer = null;

    function parse(raw) {
      if (typeof api.parseIncomingPayload === 'function') {
        return api.parseIncomingPayload(raw);
      }
      return raw;
    }

    function handleIncoming(raw) {
      const data = parse(raw);
      const adapter = typeof api.getEventAdapter === 'function'
        ? api.getEventAdapter()
        : null;
      const handled = !!(
        data &&
        (adapter?.isSubagentPayload?.(data) || api.isSubagentPayload?.(data)) &&
        adapter?.handle?.(data)
      );
      return {
        handled,
        data,
      };
    }

    function messageContext(override) {
      return override && typeof override === 'object' ? override : api;
    }

    function handleWebsocketMessage(raw, override) {
      const context = messageContext(override);
      const incoming = handleIncoming(raw);
      const data = incoming?.data;
      if (!data || typeof data !== 'object') {
        return { handled: false, data };
      }
      if (data.type === 'pong') {
        return { handled: true, data };
      }
      if (incoming.handled) {
        return { handled: true, data };
      }
      if (data.type === 'agent_state') {
        context?.handleAgentStateMessage?.(data);
        return { handled: true, data };
      }
      if (data.type === 'session_sync') {
        context?.handleSessionSync?.(data);
        return { handled: true, data };
      }
      if (data.type === 'upload_done') {
        context?.handleUploadDone?.(data);
        return { handled: true, data };
      }
      if (data.type === 'upload_error') {
        context?.handleUploadError?.(data);
        return { handled: true, data };
      }
      if (data.type === 'self_test_result') {
        context?.handleSelfTestResult?.(data);
        return { handled: true, data };
      }
      if (data.type === 'stopped') {
        context?.handleStopped?.(data);
        return { handled: true, data };
      }
      if (data.type === 'tool' && data.content) {
        context?.handleToolMessage?.(data);
        return { handled: true, data };
      }
      if (data.type === 'pet_response') {
        context?.handlePetResponse?.(data);
        return { handled: true, data };
      }
      if (data.type === 'reasoning' && data.content) {
        context?.handleReasoningMessage?.(data);
        return { handled: true, data };
      }
      if (data.type === 'response' && data.content) {
        context?.handleAssistantResponse?.(data);
        return { handled: true, data };
      }
      return { handled: false, data };
    }

    function collectDeltaTargets(state) {
      const details = state?.details;
      if (!details || typeof details.values !== 'function') {
        return [];
      }
      const targets = [];
      for (const detail of details.values()) {
        const taskId = String(detail?.task_id || '').trim();
        if (!taskId) {
          continue;
        }
        const cursorMeta = detail?.cursor && typeof detail.cursor === 'object' ? detail.cursor : {};
        const windowMeta = detail?.window && typeof detail.window === 'object' ? detail.window : {};
        const historyCursor = cursorMeta.history && typeof cursorMeta.history === 'object' ? cursorMeta.history : {};
        const frameCursor = cursorMeta.frames && typeof cursorMeta.frames === 'object' ? cursorMeta.frames : {};
        const commitCursor = cursorMeta.commits && typeof cursorMeta.commits === 'object' ? cursorMeta.commits : {};
        targets.push({
          taskId,
          historyAfterSeq: Number(historyCursor.visible_seq) || Number(windowMeta.history_last_seq) || 0,
          frameAfterSeq: Number(frameCursor.visible_seq) || Number(windowMeta.frame_last_seq) || 0,
          commitAfterSeq: Number(commitCursor.visible_seq) || Number(windowMeta.commit_last_seq) || 0,
        });
      }
      return targets;
    }

    function collectCoordinatorRevision(state) {
      const liveRevision = Number(state?.liveCursor?.visibleRevision) || 0;
      const explicitCursorRevision = Math.max(
        liveRevision,
        Number(state?.liveCursor?.afterVisibleRevision) || 0,
      );
      const coordinators = state?.coordinators;
      if (!coordinators || typeof coordinators.values !== 'function') {
        return explicitCursorRevision;
      }
      let maxRevision = explicitCursorRevision;
      for (const coordinator of coordinators.values()) {
        const revision = Number(coordinator?.replay_cursor?.visible_revision) ||
          Number(coordinator?.visible_revision) ||
          0;
        if (revision > maxRevision) {
          maxRevision = revision;
        }
      }
      return maxRevision;
    }

    async function loadTaskDelta(taskId, cursors, options) {
      const nextTaskId = String(taskId || '').trim();
      if (!nextTaskId) {
        return { taskId: '', status: 'empty' };
      }
      const historyAfterSeq = Number(cursors?.historyAfterSeq) || 0;
      const frameAfterSeq = Number(cursors?.frameAfterSeq) || 0;
      const commitAfterSeq = Number(cursors?.commitAfterSeq) || 0;
      const query = new URLSearchParams({
        task_id: nextTaskId,
        history_after_seq: String(historyAfterSeq),
        frame_after_seq: String(frameAfterSeq),
        commit_after_seq: String(commitAfterSeq),
      });

      try {
        const resp = await api.fetchImpl?.(
          `/api/subagent_state_delta?${query.toString()}`,
          { cache: 'no-store' }
        );
        if (!resp?.ok) {
          return { taskId: nextTaskId, status: resp?.status === 404 ? 'unavailable' : 'error' };
        }
        const data = await resp.json();
        if (!data || typeof data !== 'object' || !data.child_session) {
          return { taskId: nextTaskId, status: 'empty' };
        }
        const sessionPayload = {
          session: {
            coordinator_id: data.coordinator_id || '',
            task_id: data.task_id || nextTaskId,
            session_id: data.session_id || '',
            subagent_type: data.subagent_type || '',
            status: data.status || '',
            agent: {
              task_id: data.task_id || nextTaskId,
              session_id: data.session_id || '',
              subagent_type: data.subagent_type || '',
              status: data.status || '',
              child_session: data.child_session,
            },
          },
        };
        const replaceChildSession = data.child_session?.window?.replay_reset === true;
        api.applySessionPayload?.(sessionPayload, { replaceChildSession, reason: options?.reason || 'delta_recovery' });
        return { taskId: nextTaskId, status: 'ok', data };
      } catch (_) {
        return { taskId: nextTaskId, status: 'error' };
      }
    }

    async function loadTaskDeltas(chatId, targets, options) {
      const nextChatId = String(chatId || '').trim();
      const items = Array.isArray(targets) ? targets.filter((item) => item && String(item.taskId || '').trim()) : [];
      if (!nextChatId || !items.length) {
        return { chatId: nextChatId, status: 'empty', itemCount: 0 };
      }

      try {
        const resp = await api.fetchImpl?.('/api/subagent_state_deltas', {
          method: 'POST',
          cache: 'no-store',
          headers: {
            'Content-Type': 'application/json',
          },
          body: JSON.stringify({
            chat_id: nextChatId,
            tasks: items.map((item) => ({
              task_id: item.taskId,
              history_after_seq: Number(item.historyAfterSeq) || 0,
              frame_after_seq: Number(item.frameAfterSeq) || 0,
              commit_after_seq: Number(item.commitAfterSeq) || 0,
            })),
          }),
        });
        if (!resp?.ok) {
          return { chatId: nextChatId, status: 'error', itemCount: 0 };
        }

        const data = await resp.json();
        const deltas = Array.isArray(data?.items) ? data.items : [];
        for (const delta of deltas) {
          if (!delta || typeof delta !== 'object' || !delta.child_session) {
            continue;
          }
          const sessionPayload = {
            session: {
              coordinator_id: delta.coordinator_id || '',
              task_id: delta.task_id || '',
              session_id: delta.session_id || '',
              subagent_type: delta.subagent_type || '',
              status: delta.status || '',
              agent: {
                task_id: delta.task_id || '',
                session_id: delta.session_id || '',
                subagent_type: delta.subagent_type || '',
                status: delta.status || '',
                child_session: delta.child_session,
              },
            },
          };
          api.applySessionPayload?.(sessionPayload, {
            replaceChildSession: delta.child_session?.window?.replay_reset === true,
            reason: options?.reason || 'delta_recovery_batch',
          });
        }
        return { chatId: nextChatId, status: 'ok', itemCount: deltas.length, data };
      } catch (_) {
        return { chatId: nextChatId, status: 'error', itemCount: 0 };
      }
    }

    async function loadChatDelta(chatId, afterVisibleRevision, targets, options) {
      const nextChatId = String(chatId || '').trim();
      const items = Array.isArray(targets) ? targets.filter((item) => item && String(item.taskId || '').trim()) : [];
      if (!nextChatId) {
        return { chatId: nextChatId, status: 'empty', changedCount: 0, itemCount: 0 };
      }

      try {
        const resp = await api.fetchImpl?.('/api/subagent_state_delta_chat', {
          method: 'POST',
          cache: 'no-store',
          headers: {
            'Content-Type': 'application/json',
          },
          body: JSON.stringify({
            chat_id: nextChatId,
            after_visible_revision: Number(afterVisibleRevision) || 0,
            tasks: items.map((item) => ({
              task_id: item.taskId,
              history_after_seq: Number(item.historyAfterSeq) || 0,
              frame_after_seq: Number(item.frameAfterSeq) || 0,
              commit_after_seq: Number(item.commitAfterSeq) || 0,
            })),
          }),
        });
        if (!resp?.ok) {
          return { chatId: nextChatId, status: 'error', changedCount: 0, itemCount: 0 };
        }

        const data = await resp.json();
        const coordinators = Array.isArray(data?.coordinators) ? data.coordinators : [];
        const deltas = Array.isArray(data?.items) ? data.items : [];

        for (const coordinator of coordinators) {
          if (!coordinator || typeof coordinator !== 'object') {
            continue;
          }
          api.applyCoordinatorPayload?.(coordinator, { markActive: false, reason: options?.reason || 'delta_recovery_chat' });
        }

        for (const delta of deltas) {
          if (!delta || typeof delta !== 'object' || !delta.child_session) {
            continue;
          }
          const sessionPayload = {
            session: {
              coordinator_id: delta.coordinator_id || '',
              task_id: delta.task_id || '',
              session_id: delta.session_id || '',
              subagent_type: delta.subagent_type || '',
              status: delta.status || '',
              agent: {
                task_id: delta.task_id || '',
                session_id: delta.session_id || '',
                subagent_type: delta.subagent_type || '',
                status: delta.status || '',
                child_session: delta.child_session,
              },
            },
          };
          api.applySessionPayload?.(sessionPayload, {
            replaceChildSession: delta.child_session?.window?.replay_reset === true,
            reason: options?.reason || 'delta_recovery_chat',
          });
        }

        return {
          chatId: nextChatId,
          status: 'ok',
          changedCount: Number(data?.changed_count) || coordinators.length,
          itemCount: deltas.length,
          maxVisibleRevision: Number(data?.max_visible_revision) || 0,
          data,
        };
      } catch (_) {
        return { chatId: nextChatId, status: 'error', changedCount: 0, itemCount: 0 };
      }
    }

    async function loadSnapshot(targetChatId, options) {
      const chatId = String(targetChatId || '').trim();
      const token = ++snapshotToken;
      const emptySnapshot = options?.emptySnapshot || { coordinators: [] };
      if (!chatId) {
        api.applySnapshot?.(emptySnapshot, { chatId: '', interactiveUiConfig: options?.interactiveUiConfig });
        return { token, chatId: '', status: 'empty' };
      }

      try {
        const resp = await api.fetchImpl?.(
          `/api/subagent_state?chat_id=${encodeURIComponent(chatId)}`,
          { cache: 'no-store' }
        );
        if (token !== snapshotToken || (typeof options?.isCurrentChatId === 'function' && !options.isCurrentChatId(chatId))) {
          return { token, chatId, status: 'stale' };
        }
        if (resp?.status === 404) {
          api.applySnapshot?.(emptySnapshot, { chatId, interactiveUiConfig: options?.interactiveUiConfig });
          return { token, chatId, status: 'empty' };
        }
        if (!resp?.ok) {
          return { token, chatId, status: 'error' };
        }
        const data = await resp.json();
        if (token !== snapshotToken || (typeof options?.isCurrentChatId === 'function' && !options.isCurrentChatId(chatId))) {
          return { token, chatId, status: 'stale' };
        }
        api.applySnapshot?.(data, { chatId, interactiveUiConfig: options?.interactiveUiConfig });
        const runtimeState = typeof api.getRuntimeState === 'function' ? api.getRuntimeState() : null;
        const afterVisibleRevision = collectCoordinatorRevision(runtimeState);
        const deltaTargets = collectDeltaTargets(runtimeState);
        if (deltaTargets.length) {
          if (token !== snapshotToken || (typeof options?.isCurrentChatId === 'function' && !options.isCurrentChatId(chatId))) {
            return { token, chatId, status: 'stale' };
          }
          const chatDeltaResult = await loadChatDelta(chatId, afterVisibleRevision, deltaTargets, { reason: 'snapshot_recovery_chat' });
          if (chatDeltaResult?.status !== 'ok') {
            const batchResult = await loadTaskDeltas(chatId, deltaTargets, { reason: 'snapshot_recovery_batch' });
            if (batchResult?.status !== 'ok') {
              for (const target of deltaTargets) {
                if (token !== snapshotToken || (typeof options?.isCurrentChatId === 'function' && !options.isCurrentChatId(chatId))) {
                  return { token, chatId, status: 'stale' };
                }
                await loadTaskDelta(target.taskId, target, { reason: 'snapshot_recovery_fallback' });
              }
            }
          } else if (chatDeltaResult.itemCount < deltaTargets.length) {
            for (const target of deltaTargets) {
              if (token !== snapshotToken || (typeof options?.isCurrentChatId === 'function' && !options.isCurrentChatId(chatId))) {
                return { token, chatId, status: 'stale' };
              }
              const present = Array.isArray(chatDeltaResult?.data?.items) &&
                chatDeltaResult.data.items.some((item) => String(item?.task_id || '') === target.taskId);
              if (!present) {
                await loadTaskDelta(target.taskId, target, { reason: 'snapshot_recovery_fallback' });
              }
            }
          }
        }
        return { token, chatId, status: 'ok', data };
      } catch (_) {
        if (token !== snapshotToken || (typeof options?.isCurrentChatId === 'function' && !options.isCurrentChatId(chatId))) {
          return { token, chatId, status: 'stale' };
        }
        return { token, chatId, status: 'error' };
      }
    }

    function bindSocket(socket, handlers) {
      if (!socket || typeof socket !== 'object') {
        return socket;
      }
      const context = messageContext(handlers);
      socket.onopen = () => context?.onOpen?.(socket);
      socket.onclose = () => context?.onClose?.(socket);
      socket.onerror = () => context?.onError?.(socket);
      socket.onmessage = (evt) => context?.onMessage?.(evt, socket);
      return socket;
    }

    function connectSocket(url, handlers) {
      const socket = new WebSocket(url);
      bindSocket(socket, handlers);
      return socket;
    }

    function clearRuntimeTimers() {
      if (pingTimer) {
        clearInterval(pingTimer);
        pingTimer = null;
      }
      if (statsTimer) {
        clearInterval(statsTimer);
        statsTimer = null;
      }
    }

    function startPingLoop(sendPing, intervalMs) {
      if (pingTimer) {
        clearInterval(pingTimer);
      }
      pingTimer = setInterval(() => {
        sendPing?.();
      }, intervalMs);
      return pingTimer;
    }

    function startStatsLoop(refreshStats, intervalMs) {
      if (statsTimer) {
        clearInterval(statsTimer);
      }
      statsTimer = setInterval(() => {
        refreshStats?.();
      }, intervalMs);
      return statsTimer;
    }

    function scheduleReconnect(reconnectFn, delayMs) {
      if (reconnectTimer) {
        return reconnectTimer;
      }
      reconnectTimer = setTimeout(() => {
        reconnectTimer = null;
        reconnectFn?.();
      }, delayMs);
      return reconnectTimer;
    }

    function clearReconnectTimer() {
      if (reconnectTimer) {
        clearTimeout(reconnectTimer);
        reconnectTimer = null;
      }
    }

    function createLifecycle(actions) {
      const ctx = actions && typeof actions === 'object' ? actions : {};
      return {
        onOpen(socket) {
          ctx.onOpen?.(socket);
        },
        onClose(socket) {
          ctx.onClose?.(socket);
        },
        onError(socket) {
          ctx.onError?.(socket);
        },
        onMessage(evt, socket) {
          ctx.onMessage?.(evt, socket);
        },
      };
    }

    const lifecycle = api.lifecycle && typeof api.lifecycle === 'object'
      ? api.lifecycle
      : createLifecycle(api.lifecycleActions);

    return {
      lifecycle,
      createLifecycle,
      parse,
      handleIncoming,
      handleWebsocketMessage,
      loadTaskDelta,
      loadTaskDeltas,
      loadSnapshot,
      bindSocket,
      connectSocket,
      clearRuntimeTimers,
      startPingLoop,
      startStatsLoop,
      scheduleReconnect,
      clearReconnectTimer,
    };
  }

  global.AgentSubagentTransport = {
    createSubagentTransport,
  };
})(window);
