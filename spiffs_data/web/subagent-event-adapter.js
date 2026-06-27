(function initSubagentEventAdapterModule(global) {
  const SUBAGENT_MESSAGE_TYPES = new Set([
    'coordinator_status',
    'coordinator_output',
    'coordinator_done',
    'interactive_request',
    'sudo_request',
    'subagent_start',
    'subagent_done',
    'subagent_progress',
    'subagent_blocked',
    'subagent_unblocked',
    'subagent_session',
  ]);

  function interactiveUiConfig(requestType) {
    if (requestType === 'question_text') {
      return {
        title: '需要补充信息',
        prompt: '请回答这个问题后继续。',
        placeholder: '输入你的回答',
        submitText: '提交',
        inputType: 'text',
        label: 'question reply',
        blockerKind: 'question',
      };
    }
    return {
      title: '需要授权',
      prompt: '命令需要提升权限。',
      placeholder: '输入 sudo 密码',
      submitText: '继续',
      inputType: 'password',
      label: 'sudo permission',
      blockerKind: 'permission',
    };
  }

  function parseIncomingPayload(raw) {
    if (!raw || typeof raw !== 'object') {
      return raw;
    }
    if (typeof raw.text !== 'string' || !raw.text) {
      return raw;
    }
    try {
      return JSON.parse(raw.text);
    } catch (_) {
      return raw;
    }
  }

  function isSubagentPayload(data) {
    return !!(data && typeof data === 'object' && SUBAGENT_MESSAGE_TYPES.has(data.type));
  }

  function normalizeSnapshot(snapshot) {
    if (!snapshot || typeof snapshot !== 'object') {
      return { coordinators: [] };
    }
    if (!Array.isArray(snapshot.coordinators)) {
      return { ...snapshot, coordinators: [] };
    }
    return snapshot;
  }

  function trimText(value) {
    return String(value || '').trim();
  }

  function defaultFormatSubagentEvent(data) {
    const type = trimText(data?.subagent_type) || 'subagent';
    const task = trimText(data?.task);
    const detail = trimText(data?.detail);
    const title = task ? `${type} · ${task}` : type;
    if (data?.type === 'subagent_start') {
      return detail ? `subagent start · ${title} · ${detail}` : `subagent start · ${title}`;
    }
    if (data?.type === 'subagent_done') {
      return detail ? `subagent done · ${title} · ${detail}` : `subagent done · ${title}`;
    }
    return detail ? `subagent · ${title} · ${detail}` : `subagent · ${title}`;
  }

  function makeSubagentEventAction(data, helpers) {
    const payload = data && typeof data === 'object' ? data : null;
    if (!payload) {
      return null;
    }
    const key = typeof helpers?.subagentEventKey === 'function'
      ? helpers.subagentEventKey(payload)
      : trimText(payload.task_id) || trimText(payload.session_id);
    if (!key) {
      return null;
    }
    const now = typeof helpers?.now === 'function' ? Number(helpers.now()) || Date.now() : Date.now();
    const formatEventText = typeof helpers?.formatEventText === 'function'
      ? helpers.formatEventText
      : defaultFormatSubagentEvent;
    const entry = {
      type: trimText(payload.type) || 'subagent_progress',
      status: trimText(payload.status),
      task: trimText(payload.task),
      detail: trimText(payload.detail),
      blocker_kind: trimText(payload.blocker_kind),
      blocker_text: trimText(payload.blocker_text),
      blocker_scope: trimText(payload.blocker_scope),
      text: formatEventText(payload),
      ts: now,
    };
    return {
      kind: 'subagent_event',
      payload: { ...payload, ts: entry.ts },
      key,
      entry,
    };
  }

  function makeCoordinatorAction(payload) {
    if (!payload || typeof payload !== 'object') {
      return null;
    }
    return {
      kind: 'coordinator',
      payload,
    };
  }

  function makeInteractiveBlockerSetAction(blocker) {
    if (!blocker || typeof blocker !== 'object') {
      return null;
    }
    return {
      kind: 'interactive_blocker_set',
      blocker: { ...blocker },
    };
  }

  function makeInteractiveBlockerClearAction(blocker, helpers) {
    const key = trimText(blocker?.key) ||
      (typeof helpers?.interactiveBlockerKey === 'function' ? helpers.interactiveBlockerKey(blocker) : '');
    if (!key) {
      return null;
    }
    return {
      kind: 'interactive_blocker_clear',
      key,
    };
  }

  function makeHydrateInput(snapshot, helpers) {
    return {
      snapshot: normalizeSnapshot(snapshot),
      chatId: trimText(helpers?.chatId),
      interactiveUiConfig: typeof helpers?.interactiveUiConfig === 'function'
        ? helpers.interactiveUiConfig
      : interactiveUiConfig,
    };
  }

  function makeSubagentSessionAction(data, options) {
    const payload = data && typeof data === 'object' ? data : null;
    const session = payload?.session && typeof payload.session === 'object' ? payload.session : null;
    if (!session) {
      return null;
    }
    return {
      kind: 'subagent_session',
      payload: session,
      replaceChildSession: options?.replaceChildSession === true,
    };
  }

  function makeInteractivePromptState(input) {
    const requestType = trimText(input?.request_type) || 'sudo_password';
    const ui = interactiveUiConfig(requestType);
    return {
      requestType,
      requestId: trimText(input?.request_id),
      prompt: trimText(input?.prompt) || ui.prompt,
      ui,
    };
  }

  function resolveRestoredInteractivePrompt(blocker) {
    if (!blocker || !trimText(blocker.request_type) || !trimText(blocker.request_id)) {
      return null;
    }
    return makeInteractivePromptState(blocker);
  }

  function makeInteractiveReplyPayload(request, helpers) {
    const requestType = trimText(request?.request_type) || 'sudo_password';
    const value = typeof helpers?.value === 'string' ? helpers.value : '';
    const cancelled = helpers?.cancelled === true;
    return {
      type: 'interactive_reply',
      request_type: requestType,
      chat_id: trimText(request?.chat_id) || trimText(helpers?.chatId),
      session_id: trimText(request?.session_id),
      task_id: trimText(request?.task_id),
      coordinator_id: trimText(request?.coordinator_id),
      request_id: trimText(request?.request_id),
      value: cancelled ? '' : value,
      password: requestType === 'sudo_password' ? (cancelled ? '' : value) : '',
      cancelled,
    };
  }

  function makeInteractiveUnblockedEvent(request) {
    if (!request || typeof request !== 'object') {
      return null;
    }
    return {
      type: 'subagent_unblocked',
      task_id: trimText(request.task_id),
      session_id: trimText(request.session_id),
      coordinator_id: trimText(request.coordinator_id),
      subagent_type: 'delegate',
      status: 'running',
      task: trimText(request.label) || 'interactive approval',
      detail: '',
      blocker_kind: '',
      blocker_text: '',
      blocker_scope: trimText(request.task_id) || trimText(request.session_id) ? 'task' : 'coordinator',
    };
  }

  function makeInteractiveControllerState(request) {
    if (!request || typeof request !== 'object') {
      return null;
    }
    const prompt = makeInteractivePromptState(request);
    if (!prompt?.requestId) {
      return null;
    }
    return {
      request: {
        chat_id: trimText(request.chat_id),
        task_id: trimText(request.task_id),
        session_id: trimText(request.session_id),
        coordinator_id: trimText(request.coordinator_id),
        request_type: trimText(request.request_type) || prompt.requestType,
        blocker_kind: trimText(request.blocker_kind) || prompt.ui?.blockerKind || '',
        label: trimText(request.label) || prompt.ui?.label || '',
        prompt: prompt.prompt,
        request_id: prompt.requestId,
      },
      prompt,
    };
  }

  function makeInteractiveDismissActions(request, helpers) {
    if (!request || typeof request !== 'object') {
      return [];
    }
    const actions = [];
    const resumeEvent = makeInteractiveUnblockedEvent(request);
    if (resumeEvent) {
      const eventAction = makeSubagentEventAction(resumeEvent, helpers);
      if (eventAction) {
        actions.push(eventAction);
      }
    }
    const clearAction = makeInteractiveBlockerClearAction(request, helpers);
    if (clearAction) {
      actions.push(clearAction);
    }
    return actions;
  }

  function createSubagentEventAdapter(handlers) {
    const api = handlers || {};

    function handleInteractiveRequest(data) {
      const requestType = data.request_type || (data.type === 'sudo_request' ? 'sudo_password' : '');
      if (requestType !== 'sudo_password' && requestType !== 'question_text') {
        return false;
      }
      const controllerState = makeInteractiveControllerState({
        chat_id: api.getChatId ? api.getChatId() : '',
        task_id: data.task_id || '',
        session_id: data.session_id || '',
        coordinator_id: data.coordinator_id || '',
        request_type: requestType,
        blocker_kind: interactiveUiConfig(requestType).blockerKind,
        label: interactiveUiConfig(requestType).label,
        prompt: data.prompt || interactiveUiConfig(requestType).prompt,
        request_id: data.request_id || '',
      });
      if (!controllerState) {
        return false;
      }
      if (typeof api.setInteractiveControllerState === 'function') {
        api.setInteractiveControllerState(controllerState);
      }
      api.reduceSubagentUiEvent?.(makeInteractiveBlockerSetAction(controllerState.request));
      if (!(data.task_id || data.session_id)) {
        const prefix = requestType === 'question_text' ? '需要补充信息：' : '需要授权：';
        api.addSystemNote?.(`${prefix}\n${controllerState.prompt.prompt}`);
      }
      api.openInteractivePrompt?.(controllerState.prompt);
      api.renderCoordinatorPanel?.();
      return true;
    }

    function handleSubagentProgress(data) {
      api.handlePetToolMessage?.();
      api.pushSubagentEvent?.(data);
      api.renderCoordinatorPanel?.();
      api.clearPendingReasoningCard?.();
      return true;
    }

    function handleSubagentSession(data) {
      api.reduceSubagentUiEvent?.(makeSubagentSessionAction(data));
      api.renderCoordinatorPanel?.();
      api.clearPendingReasoningCard?.();
      return true;
    }

    function handleCoordinatorStatus(data) {
      api.updateCoordinatorStatus?.(data.coordinator || data);
      return true;
    }

    function handleCoordinatorOutput(data) {
      api.handleCoordinatorOutput?.(data.coordinator || data);
      return true;
    }

    function handleCoordinatorDone(data) {
      const payload = data.coordinator || {};
      if (Array.isArray(payload.agents)) {
        api.updateCoordinatorStatus?.(payload);
        api.handleCoordinatorOutput?.(payload);
      }
      api.setAssistantIdle?.();
      api.appendAssistantMessage?.(api.summarizeCoordinatorCompletion?.(payload) || '');
      api.syncSendState?.();
      return true;
    }

    function handle(data) {
      if (!data || typeof data !== 'object') return false;
      if (data.type === 'coordinator_status') return handleCoordinatorStatus(data);
      if (data.type === 'coordinator_output') return handleCoordinatorOutput(data);
      if (data.type === 'coordinator_done') return handleCoordinatorDone(data);
      if (data.type === 'interactive_request' || data.type === 'sudo_request') return handleInteractiveRequest(data);
      if (data.type === 'subagent_start' ||
          data.type === 'subagent_done' ||
          data.type === 'subagent_progress' ||
          data.type === 'subagent_blocked' ||
          data.type === 'subagent_unblocked') {
        return handleSubagentProgress(data);
      }
      if (data.type === 'subagent_session') return handleSubagentSession(data);
      return false;
    }

    return {
      interactiveUiConfig,
      parseIncomingPayload,
      isSubagentPayload,
      normalizeSnapshot,
      makeSubagentEventAction,
      makeCoordinatorAction,
      makeInteractiveBlockerSetAction,
      makeInteractiveBlockerClearAction,
      makeHydrateInput,
      makeSubagentSessionAction,
      makeInteractivePromptState,
      resolveRestoredInteractivePrompt,
      makeInteractiveReplyPayload,
      makeInteractiveUnblockedEvent,
      makeInteractiveControllerState,
      makeInteractiveDismissActions,
      handle,
    };
  }

  global.AgentSubagentEventAdapter = {
    interactiveUiConfig,
    parseIncomingPayload,
    isSubagentPayload,
    normalizeSnapshot,
    makeSubagentEventAction,
    makeCoordinatorAction,
    makeInteractiveBlockerSetAction,
    makeInteractiveBlockerClearAction,
    makeHydrateInput,
    makeSubagentSessionAction,
    makeInteractivePromptState,
    resolveRestoredInteractivePrompt,
    makeInteractiveReplyPayload,
    makeInteractiveUnblockedEvent,
    makeInteractiveControllerState,
    makeInteractiveDismissActions,
    createSubagentEventAdapter,
  };
})(window);
