(function initSubagentStateSelectorsModule(global) {
  const core = global.AgentSubagentStateCore || {};
  const { trimText, detailKeyForAgent, deriveVisibleOutputText } = core;
  const ROLE_EMOJI = Object.freeze({
    fast: '⚡',
    explore: '🗺️',
    librarian: '📚',
    oracle: '🧭',
    implement: '🛠️',
  });

  function resolveAgentRole(name) {
    const lower = String(name || '').toLowerCase();
    if (lower.includes('librarian')) return 'librarian';
    if (lower.includes('oracle')) return 'oracle';
    if (lower.includes('implement')) return 'implement';
    if (lower.includes('explore')) return 'explore';
    if (lower.includes('fast')) return 'fast';
    return '';
  }

  function formatElapsed(ms) {
    if (!ms || ms <= 0) return '';
    const s = ms / 1000;
    if (s < 1) return `${(s * 1000).toFixed(0)}ms`;
    if (s < 60) return `${s.toFixed(1)}s`;
    const m = Math.floor(s / 60);
    const rem = (s % 60).toFixed(0);
    return `${m}m ${rem}s`;
  }

  function subagentFocusLabel(focus) {
    const key = String(focus || '').trim();
    if (!key) return '';
    if (key === 'turn_execution') return 'turn execution';
    if (key === 'coordination') return 'coordination';
    if (key === 'tool_runtime') return 'tool runtime';
    if (key === 'llm_adapter') return 'llm adapter';
    if (key === 'execution_kernel') return 'execution kernel';
    if (key === 'adapter_layer') return 'adapter layer';
    if (key === 'repo_overview') return 'repo overview';
    if (key === 'local_overview') return 'local overview';
    if (key === 'file_analysis') return 'file analysis';
    return key.replace(/_/g, ' ');
  }

  function pendingQueuePrompt(entry) {
    if (!entry) return '';
    if (typeof entry === 'string') return trimText(entry);
    return trimText(entry.prompt) || trimText(entry.text) || '';
  }

  function currentSelectedSubagentKey(state) {
    return trimText(state?.selectedTabKey);
  }

  function coordinatorAgentHint(agent) {
    if (!agent?.parent_response_sent) return '等待父会话响应';
    if (agent?.coordinator_wake_state === 'pending' && agent?.wake_retry_count > 0) {
      return `重试中 ${agent.wake_retry_count}x`;
    }
    if (agent?.coordinator_wake_state === 'pending') return '待唤醒';
    if (agent?.coordinator_wake_state === 'dispatched' && agent?.status === 'running') return '已可见';
    if (agent?.wake_last_error) return agent.wake_last_error;
    return '';
  }

  function orderedCoordinatorStates(state) {
    const coordinators = state?.coordinators instanceof Map ? [...state.coordinators.values()] : [];
    const details = state?.details instanceof Map ? [...state.details.values()] : [];
    const withAgents = coordinators.map((coordinator) => {
      const coordinatorId = trimText(coordinator?.coordinator_id);
      const agents = details
        .filter((detail) => trimText(detail?.coordinator_id) === coordinatorId)
        .map((detail) => ({
          task_id: trimText(detail?.task_id),
          task_key: trimText(detail?.task_key),
          session_id: trimText(detail?.session_id),
          subagent_type: trimText(detail?.subagent_type),
          name: trimText(detail?.task) || trimText(detail?.subagent_type) || 'subagent',
          description: trimText(detail?.task) || trimText(detail?.subagent_type) || 'subagent',
          status: trimText(detail?.status) || 'running',
          model: trimText(detail?.model),
          scope_path: trimText(detail?.scope_path),
          scope_kind: trimText(detail?.scope_kind),
          analysis_focus: trimText(detail?.analysis_focus),
          depends_on: trimText(detail?.depends_on),
          output: trimText(detail?.output),
          target_files: trimText(detail?.target_files),
          elapsed_ms: Number(detail?.elapsed_ms) || 0,
          write_approved: detail?.write_approved === true,
          blocker_kind: trimText(detail?.blocker_kind),
          blocker_text: trimText(detail?.blocker_text),
          coordinator_blocker_kind: trimText(coordinator?.blocker_kind),
          coordinator_blocker_text: trimText(coordinator?.blocker_text),
          wake_last_error: trimText(detail?.wake_last_error),
          pending_request: detail?.pending_request || null,
        }))
        .sort((left, right) => {
          const leftElapsed = Number(left?.elapsed_ms) || 0;
          const rightElapsed = Number(right?.elapsed_ms) || 0;
          if (leftElapsed !== rightElapsed) return rightElapsed - leftElapsed;
          return trimText(left?.task_id || left?.session_id || left?.name)
            .localeCompare(trimText(right?.task_id || right?.session_id || right?.name));
        });

      return {
        ...coordinator,
        agent_count: agents.length || Number(coordinator?.agent_count) || 0,
        completed_count: agents.filter((agent) => trimText(agent?.status) === 'done').length,
        running_count: agents.filter((agent) => {
          const status = trimText(agent?.status);
          return status === 'running' || status === 'queued' || status === 'waiting';
        }).length,
        queued_count: agents.filter((agent) => trimText(agent?.status) === 'queued').length,
        blocked_count: agents.filter((agent) => trimText(agent?.status) === 'blocked' || trimText(agent?.blocker_kind)).length,
        failed_count: agents.filter((agent) => {
          const status = trimText(agent?.status);
          return status === 'failed' || status === 'error';
        }).length,
        effective_output_count: agents.filter((agent) => trimText(deriveVisibleOutputText(agent, agent?.latest_frame))).length,
        agents,
      };
    });
    return withAgents.sort((left, right) => {
      const leftRunning = trimText(left?.status) === 'running' ? 1 : 0;
      const rightRunning = trimText(right?.status) === 'running' ? 1 : 0;
      if (leftRunning !== rightRunning) return rightRunning - leftRunning;
      return trimText(left?.coordinator_id).localeCompare(trimText(right?.coordinator_id));
    });
  }

  function visibleSubagentTabs(state, limit = 8) {
    const details = state?.details instanceof Map ? [...state.details.values()] : [];
    const interactiveBlockers = state?.interactiveBlockers instanceof Map ? state.interactiveBlockers : null;
    const tabs = details
      .map((detail) => {
        const key = trimText(detail?.key) || detailKeyForAgent(detail);
        const hasInteractiveBlocker = !!(
          key &&
          interactiveBlockers &&
          (interactiveBlockers.has(trimText(detail?.task_id)) ||
            interactiveBlockers.has(trimText(detail?.session_id)) ||
            interactiveBlockers.has(trimText(detail?.coordinator_id)))
        );
        const hasBlocker = hasInteractiveBlocker || !!(trimText(detail?.blocker_kind) || trimText(detail?.status) === 'blocked');
        return {
        key,
        coordinator_id: trimText(detail?.coordinator_id),
        session_id: trimText(detail?.session_id),
        task_id: trimText(detail?.task_id),
        label: trimText(detail?.task) || trimText(detail?.subagent_type) || 'Subagent',
        description: trimText(detail?.subagent_type),
        status: trimText(detail?.status) || 'running',
        blocker_kind: trimText(detail?.blocker_kind),
        hasBlocker,
        last_updated_at: Number(detail?.latest_frame?.ts) || 0,
      };
      })
      .filter((tab) => tab.key)
      .sort((left, right) => {
        const leftSelected = trimText(left?.key) === currentSelectedSubagentKey(state) ? 1 : 0;
        const rightSelected = trimText(right?.key) === currentSelectedSubagentKey(state) ? 1 : 0;
        if (leftSelected !== rightSelected) return rightSelected - leftSelected;

        const leftBlocked = left?.hasBlocker ? 1 : 0;
        const rightBlocked = right?.hasBlocker ? 1 : 0;
        if (leftBlocked !== rightBlocked) return rightBlocked - leftBlocked;

        const leftStatus = trimText(left?.status);
        const rightStatus = trimText(right?.status);
        const leftActive = (leftStatus === 'running' || leftStatus === 'queued' || leftStatus === 'waiting') ? 1 : 0;
        const rightActive = (rightStatus === 'running' || rightStatus === 'queued' || rightStatus === 'waiting') ? 1 : 0;
        if (leftActive !== rightActive) return rightActive - leftActive;

        return (Number(right?.last_updated_at) || 0) - (Number(left?.last_updated_at) || 0);
      });

    const capped = tabs.slice(0, limit);
    const selectedKey = currentSelectedSubagentKey(state);
    if (!selectedKey || capped.some((tab) => tab.key === selectedKey)) {
      if (capped.some((tab) => tab.hasBlocker)) {
        return capped;
      }
    } else {
      const selectedTab = tabs.find((tab) => tab.key === selectedKey);
      if (selectedTab) {
        if (limit <= 0) {
          return [selectedTab];
        }
        return [selectedTab].concat(capped.filter((tab) => tab.key !== selectedKey).slice(0, Math.max(0, limit - 1)));
      }
    }

    const blockedTab = tabs.find((tab) => tab.hasBlocker && !capped.some((entry) => entry.key === tab.key));
    if (!blockedTab) {
      return capped;
    }

    if (limit <= 0) {
      return [blockedTab];
    }
    return [blockedTab].concat(capped.filter((tab) => tab.key !== blockedTab.key).slice(0, Math.max(0, limit - 1)));
  }

  function orderedSubagentDetails(state) {
    const details = state?.details instanceof Map ? [...state.details.values()] : [];
    return details.sort((left, right) => {
      const leftSelected = trimText(left?.key) === currentSelectedSubagentKey(state) ? 1 : 0;
      const rightSelected = trimText(right?.key) === currentSelectedSubagentKey(state) ? 1 : 0;
      if (leftSelected !== rightSelected) return rightSelected - leftSelected;

      const leftBlocked = trimText(left?.blocker_kind) || trimText(left?.status) === 'blocked' ? 1 : 0;
      const rightBlocked = trimText(right?.blocker_kind) || trimText(right?.status) === 'blocked' ? 1 : 0;
      if (leftBlocked !== rightBlocked) return rightBlocked - leftBlocked;

      const leftStatus = trimText(left?.status);
      const rightStatus = trimText(right?.status);
      const leftActive = (leftStatus === 'running' || leftStatus === 'queued' || leftStatus === 'waiting') ? 1 : 0;
      const rightActive = (rightStatus === 'running' || rightStatus === 'queued' || rightStatus === 'waiting') ? 1 : 0;
      if (leftActive !== rightActive) return rightActive - leftActive;

      const leftTs = Number(left?.latest_frame?.ts) || 0;
      const rightTs = Number(right?.latest_frame?.ts) || 0;
      if (leftTs !== rightTs) return rightTs - leftTs;

      return trimText(left?.task || left?.subagent_type).localeCompare(trimText(right?.task || right?.subagent_type));
    });
  }

  function subagentSummary(state) {
    const details = orderedSubagentDetails(state);
    const total = details.length;
    let blocked = 0;
    let running = 0;
    let done = 0;
    let failed = 0;
    for (const detail of details) {
      const status = trimText(detail?.status);
      if (status === 'blocked' || trimText(detail?.blocker_kind)) blocked++;
      else if (status === 'running' || status === 'queued' || status === 'waiting') running++;
      else if (status === 'done') done++;
      else if (status === 'failed' || status === 'error') failed++;
    }
    return { total, blocked, running, done, failed };
  }

  function coordinatorSummaryText(state) {
    const total = Number(state?.agent_count) || 0;
    const done = Number(state?.completed_count) || 0;
    const running = Math.max(0, total - done);
    const status = trimText(state?.status) || 'running';
    const wakeState = trimText(state?.wake_state) || 'idle';
    const retries = Number(state?.wake_retry_count) || 0;
    const parentReady = state?.parent_response_sent === true;
    if (!parentReady) return '等待父会话首条响应后再注入结果';
    if (wakeState === 'pending' && retries > 0) return `等待重试唤醒 · 已重试 ${retries} 次`;
    if (wakeState === 'pending') return '等待唤醒父会话';
    if ((Number(state?.queued_count) || 0) > 0) return `${Number(state?.queued_count) || 0} 排队中 · ${done}/${total} 完成`;
    if (wakeState === 'dispatched' && status === 'running') return `${running} 运行中 · 已推送父会话`;
    if (!total) return status === 'failed' ? '子任务失败' : '等待子任务';
    if (status === 'failed') return `${done}/${total} 完成，存在失败`;
    if (status === 'done') return `${total}/${total} 完成`;
    return `${running} 运行中 · ${done}/${total} 完成`;
  }

  function coordinatorPanelViewModel(state, panelState) {
    return {
      orderedStates: orderedCoordinatorStates(state),
      detailStates: orderedSubagentDetails(state),
      summary: subagentSummary(state),
      coordinatorDismissed: panelState?.dismissed === true,
      coordinatorVisible: panelState?.visible === true,
    };
  }

  function detailPanelViewModel(state, chatId, options) {
    const detailLimit = Number(options?.detailLimit) > 0 ? Number(options.detailLimit) : 8;
    return {
      detailView: selectedSubagentDetailView(state, chatId),
      visibleTabs: visibleSubagentTabs(state, detailLimit),
      orderedDetails: orderedSubagentDetails(state).slice(0, detailLimit),
      selectedKey: effectiveSelectedSubagentKey(state),
    };
  }

  function effectiveSelectedSubagentKey(state) {
    const selected = currentSelectedSubagentKey(state);
    if (selected && state?.details?.has?.(selected)) {
      return selected;
    }

    const orderedDetails = orderedSubagentDetails(state);
    for (const detail of orderedDetails) {
      if (!detail) continue;
      const detailKey = trimText(detail?.key) || detailKeyForAgent(detail);
      const blocked = trimText(detail?.blocker_kind) || trimText(detail?.status) === 'blocked';
      if (!blocked) continue;
      if (blockerForDetail(state, detail, '')) {
        return detailKey;
      }
    }

    const tabs = visibleSubagentTabs(state, 1);
    if (tabs.length && trimText(tabs[0]?.key)) {
      return trimText(tabs[0].key);
    }

    const coordinators = orderedCoordinatorStates(state);
    for (const coordinator of coordinators) {
      const firstAgent = Array.isArray(coordinator?.agents) ? coordinator.agents[0] : null;
      const key = detailKeyForAgent(firstAgent);
      if (key) return key;
    }
    return '';
  }

  function interactiveBlockerKey(blocker) {
    if (!blocker) return '';
    return String(blocker.task_id || blocker.session_id || blocker.coordinator_id || blocker.chat_id || '').trim();
  }

  function blockerForDetail(state, detail, chatId) {
    if (!detail || !(state?.interactiveBlockers instanceof Map)) return null;
    const candidates = [
      detail.task_id,
      detail.session_id,
      detail.coordinator_id,
    ];
    for (const candidate of candidates) {
      const key = String(candidate || '').trim();
      if (!key) continue;
      const blocker = state.interactiveBlockers.get(key);
      if (!blocker) continue;
      if (chatId && blocker.chat_id && blocker.chat_id !== chatId) continue;
      return blocker;
    }
    return null;
  }

  function selectedSubagentDetail(state) {
    const key = effectiveSelectedSubagentKey(state);
    return key ? state.details.get(key) : null;
  }

  function currentInteractiveBlocker(state, chatId) {
    const selectedDetail = selectedSubagentDetail(state);
    const selected = selectedDetail ? blockerForDetail(state, selectedDetail, chatId) : null;
    if (selected) {
      return selected;
    }

    const orderedDetails = orderedSubagentDetails(state);
    for (const detail of orderedDetails) {
      const blocker = blockerForDetail(state, detail, chatId);
      if (blocker) {
        return blocker;
      }
    }

    if (!(state?.interactiveBlockers instanceof Map)) {
      return null;
    }

    for (const blocker of state.interactiveBlockers.values()) {
      if (!blocker) continue;
      if (chatId && blocker.chat_id && blocker.chat_id !== chatId) continue;
      return blocker;
    }

    return null;
  }

  function selectedSubagentDetailView(state, chatId) {
    const detail = selectedSubagentDetail(state);
    if (!detail) {
      return null;
    }

    const blockerHistory = Array.isArray(detail.blockers) ? detail.blockers.slice().reverse() : [];
    const blockers = [];
    if (detail.status === 'error' || detail.status === 'failed') {
      blockers.push({ kind: 'error', text: 'Subagent failed before producing a stable final result' });
    }
    if (trimText(detail.blocker_kind) || trimText(detail.blocker_text)) {
      blockers.push({
        kind: trimText(detail.blocker_kind) || 'blocked',
        text: trimText(detail.blocker_text) || `Blocked at ${trimText(detail.blocker_scope) || 'task'} scope`,
      });
    }
    if (trimText(detail.wake_last_error)) {
      blockers.push({ kind: 'retry', text: `Parent wake issue: ${detail.wake_last_error}` });
    }

    const interactive = blockerForDetail(state, detail, chatId);
    if (interactive) {
      blockers.push({
        kind: trimText(interactive.blocker_kind) || 'blocked',
        text: trimText(interactive.prompt) || 'Waiting for interactive approval in parent session',
      });
    }

    if (!blockers.length && blockerHistory.length) {
      const latest = blockerHistory[0];
      blockers.push({
        kind: trimText(latest.kind) || 'blocked',
        text: latest.state === 'resolved'
          ? `最近已恢复：${trimText(latest.text)}`
          : trimText(latest.text),
      });
    }

    const pendingQueue = detail.pending_queue && typeof detail.pending_queue === 'object'
      ? detail.pending_queue
      : { permissions: [], questions: [] };
    const permissionCount = Array.isArray(pendingQueue.permissions) ? pendingQueue.permissions.length : 0;
    const questionCount = Array.isArray(pendingQueue.questions) ? pendingQueue.questions.length : 0;
    const queueSummary = {
      permissionCount,
      questionCount,
      permissionPrompt: permissionCount ? pendingQueuePrompt(pendingQueue.permissions[0]) : '',
      questionPrompt: questionCount ? pendingQueuePrompt(pendingQueue.questions[0]) : '',
    };

    const frames = Array.isArray(detail.timeline) && detail.timeline.length
      ? detail.timeline
      : (Array.isArray(detail.frames) ? detail.frames : []);

    const role = resolveAgentRole(detail.subagent_type || detail.task);
    const emoji = ROLE_EMOJI[role] || '';
    const title = trimText(detail.task) || trimText(detail.subagent_type) || 'Subagent';

    const metaParts = [];
    if (detail.status) metaParts.push(detail.status);
    if (detail.model) metaParts.push(detail.model);
    if (detail.elapsed_ms > 0) metaParts.push(formatElapsed(detail.elapsed_ms));
    if (detail.scope_kind) metaParts.push(detail.scope_kind);
    if (detail.analysis_focus) metaParts.push(subagentFocusLabel(detail.analysis_focus));
    if (detail.scope_path) metaParts.push(`scope: ${trimText(detail.scope_path)}`);
    if (detail.target_files) metaParts.push(`files: ${trimText(detail.target_files)}`);
    if (detail.write_approved) metaParts.push('write-ready');

    const frameItems = frames.map((frame) => {
      let badge = '进展';
      if (frame.phase === 'start' || frame.type === 'subagent_start') {
        badge = '开始';
      } else if (frame.type === 'subagent_request') {
        badge = frame.blocker_kind === 'question' ? '提问' : '请求';
      } else if (frame.type === 'subagent_step') {
        badge = frame.blocker_kind === 'tool' ? '工具' : '步骤';
      } else if (frame.phase === 'blocked' || frame.type === 'subagent_blocked') {
        badge = '阻塞';
      } else if (frame.phase === 'resumed' || frame.type === 'subagent_unblocked') {
        badge = '恢复';
      } else if (frame.phase === 'failed' || frame.status === 'error' || frame.status === 'failed') {
        badge = '失败';
      } else if (frame.phase === 'done' || frame.type === 'subagent_done') {
        badge = '完成';
      }

      const parts = [];
      if (frame.detail) parts.push(frame.detail);
      if (frame.blocker_text && frame.blocker_text !== frame.detail) parts.push(frame.blocker_text);
      if (frame.output_preview) parts.push(frame.output_preview);
      return {
        ...frame,
        badge,
        text: parts.join(' · ') || frame.task || frame.text || '',
      };
    });

    const commits = (Array.isArray(detail.commits) ? detail.commits : []).map((commit) => {
      let badge = '进展';
      if (commit.kind === 'result') badge = '结果';
      else if (commit.kind === 'question') badge = '提问';
      else if (commit.kind === 'permission') badge = '请求';
      else if (commit.kind === 'tool') badge = '工具';
      else if (commit.kind === 'blocker') badge = '阻塞';
      else if (commit.kind === 'resume') badge = '恢复';
      else if (commit.kind === 'start') badge = '开始';
      return {
        ...commit,
        badge,
        text_line: `${commit.label || 'subagent'} · ${commit.text || ''}`.trim(),
      };
    });

    const historyItems = (Array.isArray(detail.history) ? detail.history : []).map((item) => ({
      id: trimText(item?.id),
      role: trimText(item?.role) || 'assistant',
      content: trimText(item?.content),
      reasoning: trimText(item?.reasoning),
      ts: Number(item?.ts) || 0,
      source: trimText(item?.source),
    }));

    return {
      detail,
      title,
      titleEmoji: emoji,
      metaText: metaParts.join(' · ') || '等待更多子任务信息',
      framesEmptyText: '暂无事件帧',
      outputEmptyText: '暂无最终输出',
      commitTitle: 'Session commits',
      historyTitle: 'Session history',
      summaryText: trimText(detail.session_summary) ||
        trimText(detail.latest_frame?.output_preview) ||
        trimText(detail.latest_frame?.detail) ||
        trimText(deriveVisibleOutputText(detail, detail.latest_frame)) ||
        (trimText(detail.output) ? '该子任务已产生最终输出' : ''),
      outputText: trimText(deriveVisibleOutputText(detail, detail.latest_frame)),
      blockers,
      blockerHistory,
      queueSummary,
      frames: frameItems,
      interactive,
      commits,
      history: Array.isArray(detail.history) ? detail.history : [],
      historyItems,
    };
  }

  global.AgentSubagentStateSelectors = {
    currentSelectedSubagentKey,
    orderedCoordinatorStates,
    visibleSubagentTabs,
    orderedSubagentDetails,
    subagentSummary,
    coordinatorSummaryText,
    coordinatorPanelViewModel,
    detailPanelViewModel,
    resolveAgentRole,
    formatElapsed,
    subagentFocusLabel,
    coordinatorAgentHint,
    effectiveSelectedSubagentKey,
    interactiveBlockerKey,
    blockerForDetail,
    currentInteractiveBlocker,
    selectedSubagentDetailView,
    selectedSubagentDetail,
  };
})(window);
