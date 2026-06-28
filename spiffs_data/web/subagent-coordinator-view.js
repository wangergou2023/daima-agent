(function initSubagentCoordinatorViewModule(global) {
  function renderCoordinatorAgent(agent, maxElapsed, deps) {
    const {
      formatElapsed,
      resolveAgentRole,
      roleEmoji,
      currentSelectedDetailKey,
      detailKeyForAgent,
      coordinatorAgentHint,
      clipText,
      subagentFocusLabel,
      subagentEventsForAgent,
      onSelectDetail,
    } = deps || {};

    const row = document.createElement('div');
    row.className = 'coordinator-agent';

    const outputText = String(agent?.output || '').trim();
    if (outputText) {
      row.classList.add('has-output');
    }
    row.dataset.taskId = agent?.task_id || '';
    const detailKey = typeof detailKeyForAgent === 'function' ? detailKeyForAgent(agent) : '';
    row.dataset.detailKey = detailKey;
    if (detailKey && detailKey === currentSelectedDetailKey) {
      row.classList.add('selected');
    }
    if (agent?.wake_last_error || agent?.status === 'error' || agent?.status === 'failed') {
      row.classList.add('blocked');
    }

    const body = document.createElement('div');
    body.className = 'coordinator-agent-body';

    const statusEl = document.createElement('span');
    statusEl.className = `coordinator-agent-status ${agent?.status || 'waiting'}`;

    if (agent?.status === 'running') {
      const spinner = document.createElement('span');
      spinner.className = 'coordinator-agent-spin';
      spinner.textContent = '⟳';
      statusEl.appendChild(spinner);
    } else if (agent?.status === 'queued') {
      statusEl.textContent = '⋯';
    } else if (agent?.status === 'done') {
      statusEl.textContent = '✓';
    } else if (agent?.status === 'error') {
      statusEl.textContent = '✗';
    } else {
      statusEl.textContent = '—';
    }

    const nameEl = document.createElement('span');
    nameEl.className = 'coordinator-agent-name';
    const role = typeof resolveAgentRole === 'function' ? resolveAgentRole(agent?.subagent_type || agent?.name) : '';
    const emoji = role && roleEmoji ? roleEmoji[role] || '' : '';
    nameEl.textContent = emoji ? `${emoji} ${agent?.name || 'Agent'}` : (agent?.name || 'Agent');

    const metaEl = document.createElement('span');
    metaEl.className = 'coordinator-agent-meta';

    const expandHint = document.createElement('span');
    expandHint.className = 'coordinator-agent-expand-hint';
    expandHint.textContent = '▼';

    const elapsedEl = document.createElement('span');
    elapsedEl.className = 'coordinator-agent-elapsed';
    const elapsedMs = Number(agent?.elapsed_ms) || 0;
    elapsedEl.textContent = elapsedMs > 0 && typeof formatElapsed === 'function' ? formatElapsed(elapsedMs) : '';

    const hint = typeof coordinatorAgentHint === 'function' ? coordinatorAgentHint(agent) : '';
    if (hint) {
      const hintEl = document.createElement('span');
      hintEl.className = 'coordinator-agent-hint';
      hintEl.textContent = hint;
      metaEl.appendChild(hintEl);
    }

    if (agent?.scope_path) {
      const scopeEl = document.createElement('span');
      scopeEl.className = 'coordinator-agent-scope';
      scopeEl.textContent = typeof clipText === 'function' ? clipText(agent.scope_path, 42) : agent.scope_path;
      scopeEl.title = agent.scope_path;
      metaEl.appendChild(scopeEl);
    }

    if (agent?.depends_on) {
      const dependsEl = document.createElement('span');
      dependsEl.className = 'coordinator-agent-focus';
      dependsEl.textContent = `after: ${typeof clipText === 'function' ? clipText(agent.depends_on, 24) : agent.depends_on}`;
      dependsEl.title = agent.depends_on;
      metaEl.appendChild(dependsEl);
    }

    if (agent?.analysis_focus) {
      const focusEl = document.createElement('span');
      focusEl.className = 'coordinator-agent-focus';
      focusEl.textContent = typeof subagentFocusLabel === 'function'
        ? subagentFocusLabel(agent.analysis_focus)
        : agent.analysis_focus;
      focusEl.title = agent.analysis_focus;
      metaEl.appendChild(focusEl);
    }

    if (agent?.model) {
      const modelEl = document.createElement('span');
      modelEl.className = 'coordinator-agent-model';
      modelEl.textContent = agent.model;
      metaEl.appendChild(modelEl);
    }

    metaEl.appendChild(expandHint);
    metaEl.appendChild(elapsedEl);

    body.appendChild(statusEl);
    body.appendChild(nameEl);
    body.appendChild(metaEl);
    row.appendChild(body);

    if (elapsedMs > 0 && maxElapsed > 0) {
      const barWrap = document.createElement('div');
      barWrap.className = 'coordinator-agent-bar-wrap';

      const bar = document.createElement('div');
      bar.className = 'coordinator-agent-bar';

      const fill = document.createElement('div');
      fill.className = `coordinator-agent-bar-fill ${role} ${agent?.status || 'waiting'}`;
      const pct = Math.min(100, (elapsedMs / maxElapsed) * 100);
      fill.style.width = `${pct}%`;

      bar.appendChild(fill);
      barWrap.appendChild(bar);
      row.appendChild(barWrap);
    }

    row.addEventListener('click', () => {
      if (detailKey && typeof onSelectDetail === 'function') {
        onSelectDetail(detailKey);
      }
      if (outputText) {
        row.classList.toggle('expanded');
      }
    });

    if (outputText) {
      const output = document.createElement('div');
      output.className = 'coordinator-agent-output';
      output.textContent = outputText;
      row.appendChild(output);
    }

    const events = typeof subagentEventsForAgent === 'function' ? subagentEventsForAgent(agent) : [];
    if (events.length) {
      const timeline = document.createElement('div');
      timeline.className = 'coordinator-agent-events';
      for (const event of events) {
        const item = document.createElement('div');
        item.className = `coordinator-agent-event ${event.type}`;

        const badge = document.createElement('span');
        badge.className = 'coordinator-agent-event-badge';
        if (event.type === 'subagent_start') {
          badge.textContent = '开始';
        } else if (event.type === 'subagent_done') {
          badge.textContent = event.status === 'error' ? '失败' : '完成';
        } else {
          badge.textContent = '进展';
        }

        const text = document.createElement('span');
        text.className = 'coordinator-agent-event-text';
        text.textContent = typeof clipText === 'function'
          ? clipText(event.detail || event.task || event.text, 220)
          : String(event.detail || event.task || event.text || '');

        item.appendChild(badge);
        item.appendChild(text);
        timeline.appendChild(item);
      }
      row.appendChild(timeline);
    }

    return row;
  }

  function renderCoordinatorCard(state, renderCoordinatorAgent, coordinatorSummaryText) {
    const card = document.createElement('section');
    card.className = 'coordinator-card';
    card.dataset.coordinatorId = state.coordinator_id || '';

    const head = document.createElement('div');
    head.className = 'coordinator-card-head';

    const titleWrap = document.createElement('div');
    titleWrap.className = 'coordinator-card-copy';

    const titleEl = document.createElement('div');
    titleEl.className = 'coordinator-card-title';
    titleEl.textContent = state.coordinator_id ? `Coordinator · ${state.coordinator_id}` : 'Coordinator';

    const summaryEl = document.createElement('div');
    summaryEl.className = 'coordinator-card-summary';
    const summaryParts = [coordinatorSummaryText(state)];
    if (state.wake_last_error) {
      summaryParts.push(`last=${state.wake_last_error}`);
    }
    summaryEl.textContent = summaryParts.join(' · ');

    titleWrap.appendChild(titleEl);
    titleWrap.appendChild(summaryEl);

    const metaEl = document.createElement('div');
    metaEl.className = 'coordinator-card-meta';
    metaEl.textContent = `${state.completed_count || 0}/${state.agent_count || state.agents.length || 0}`;

    head.appendChild(titleWrap);
    head.appendChild(metaEl);
    card.appendChild(head);

    const agentsWrap = document.createElement('div');
    agentsWrap.className = 'coordinator-card-agents';

    let maxElapsed = 0;
    for (const agent of state.agents) {
      const ms = Number(agent.elapsed_ms) || 0;
      if (ms > maxElapsed) maxElapsed = ms;
    }
    for (const agent of state.agents) {
      agentsWrap.appendChild(renderCoordinatorAgent(agent, maxElapsed));
    }

    card.appendChild(agentsWrap);
    return card;
  }

  function renderCoordinatorPanel(ctx) {
    const {
      panelEl,
      agentsEl,
      orderedStates,
      detailStates,
      summary,
      coordinatorDismissed,
      coordinatorVisible,
      renderCoordinatorAgent,
      coordinatorSummaryText,
    } = ctx;
    if (!panelEl || !agentsEl) return { coordinatorVisible };

    const titleEl = panelEl.querySelector('.coordinator-title');
    const summaryEl = panelEl.querySelector('.coordinator-summary');
    if (titleEl) {
      const totalCoordinators = orderedStates.length;
      const totalSubagents = detailStates.length;
      titleEl.textContent = totalSubagents > 1
        ? `Subagents · ${totalSubagents}`
        : (totalCoordinators > 1 ? `Coordinators · ${totalCoordinators}` : 'Coordinator');
      if (summaryEl) {
        const first = orderedStates[0];
        const mode = first?.dispatch_mode ? ` · ${first.dispatch_mode}` : '';
        const parts = [];
        if (summary.running > 0) parts.push(`${summary.running} 个运行中`);
        if (summary.blocked > 0) parts.push(`${summary.blocked} 个阻塞`);
        if (summary.done > 0) parts.push(`${summary.done} 个完成`);
        if (summary.failed > 0) parts.push(`${summary.failed} 个失败`);
        if (!parts.length) parts.push(`${totalSubagents || totalCoordinators} 个可见`);
        summaryEl.textContent = `${parts.join(' · ')}${mode}`;
      }
    }

    agentsEl.innerHTML = '';
    for (const state of orderedStates) {
      agentsEl.appendChild(renderCoordinatorCard(state, renderCoordinatorAgent, coordinatorSummaryText));
    }

    if (!coordinatorVisible || coordinatorDismissed) {
      panelEl.hidden = true;
      panelEl.setAttribute('aria-hidden', 'true');
      panelEl.classList.remove('removing');
      return { coordinatorVisible: false };
    }

    panelEl.hidden = false;
    panelEl.removeAttribute('aria-hidden');
    panelEl.classList.remove('removing');
    return { coordinatorVisible: true };
  }

  function hideCoordinatorPanel(ctx) {
    const {
      panelEl,
      agentsEl,
      coordinatorVisible,
      onBeforeHide,
      onAfterHide,
    } = ctx;
    if (!panelEl || !coordinatorVisible) {
      return { coordinatorVisible };
    }
    onBeforeHide?.();
    panelEl.hidden = true;
    panelEl.setAttribute('aria-hidden', 'true');
    panelEl.classList.add('removing');
    const nextVisible = false;
    const onEnd = () => {
      panelEl.removeEventListener('transitionend', onEnd);
      panelEl.classList.remove('removing');
      if (agentsEl) agentsEl.innerHTML = '';
      onAfterHide?.();
    };
    panelEl.addEventListener('transitionend', onEnd);
    setTimeout(() => {
      if (panelEl.hidden) return;
      panelEl.removeEventListener('transitionend', onEnd);
      onEnd();
    }, 400);
    return { coordinatorVisible: nextVisible };
  }

  global.AgentSubagentCoordinatorView = {
    renderCoordinatorAgent,
    renderCoordinatorPanel,
    hideCoordinatorPanel,
  };
})(window);
