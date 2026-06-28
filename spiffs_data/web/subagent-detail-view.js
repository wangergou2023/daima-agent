(function initSubagentDetailViewModule(global) {
  function renderSubagentDetailTabs(ctx) {
    const {
      detailTabsEl,
      visibleTabs,
      selectedKey,
      onSelectDetail,
    } = ctx;
    if (!detailTabsEl) return;
    detailTabsEl.innerHTML = '';

    for (const tab of visibleTabs) {
      const visualStatus = tab.blocker_kind ? 'blocked' : (tab.status || 'waiting');
      const button = document.createElement('button');
      button.type = 'button';
      button.className = `subagent-detail-tab ${visualStatus}${tab.key === selectedKey ? ' active' : ''}`;
      button.setAttribute('role', 'tab');
      button.setAttribute('aria-selected', tab.key === selectedKey ? 'true' : 'false');
      button.dataset.detailKey = tab.key;

      const badge = document.createElement('span');
      badge.className = 'subagent-detail-tab-badge';
      badge.textContent = visualStatus === 'done' ? '✓' :
        (visualStatus === 'error' || visualStatus === 'failed' || visualStatus === 'blocked' ? '!' : '…');

      const label = document.createElement('span');
      label.className = 'subagent-detail-tab-label';
      label.textContent = tab.label || 'Subagent';

      button.appendChild(badge);
      button.appendChild(label);
      button.addEventListener('click', () => onSelectDetail(tab.key));
      detailTabsEl.appendChild(button);
    }
  }

  function renderSubagentSessionRail(ctx) {
    const {
      sessionRailEl,
      orderedDetails,
      selectedKey,
      onSelectDetail,
    } = ctx;
    if (!sessionRailEl) return;
    sessionRailEl.innerHTML = '';

    if (!orderedDetails.length) {
      sessionRailEl.hidden = true;
      return;
    }

    sessionRailEl.hidden = false;
    for (const detail of orderedDetails) {
      const card = document.createElement('button');
      card.type = 'button';
      card.className = `subagent-session-card ${detail.key === selectedKey ? 'active' : ''} ${detail.blocker_kind ? 'blocked' : (detail.status || 'waiting')}`;
      card.dataset.detailKey = detail.key || '';

      const top = document.createElement('div');
      top.className = 'subagent-session-card-top';

      const title = document.createElement('span');
      title.className = 'subagent-session-card-title';
      title.textContent = detail.task || detail.subagent_type || 'Subagent';

      const status = document.createElement('span');
      status.className = 'subagent-session-card-status';
      status.textContent = detail.blocker_kind
        ? (detail.blocker_kind === 'question' ? '提问' : detail.blocker_kind === 'permission' ? '授权' : '阻塞')
        : (detail.status || 'waiting');

      top.appendChild(title);
      top.appendChild(status);

      const summary = document.createElement('div');
      summary.className = 'subagent-session-card-summary';
      summary.textContent = detail.session_summary ||
        detail.latest_frame?.output_preview ||
        detail.latest_frame?.detail ||
        detail.output ||
        '等待更多子会话信息';

      card.appendChild(top);
      card.appendChild(summary);
      card.addEventListener('click', () => onSelectDetail(detail.key));
      sessionRailEl.appendChild(card);
    }
  }

  function renderSubagentDetailPanel(ctx) {
    const {
      panelEl,
      titleEl,
      metaEl,
      blockersEl,
      framesEl,
      outputEl,
      detailView,
      visibleTabs,
      orderedDetails,
      selectedKey,
      sessionRailEl,
      detailTabsEl,
      onSelectDetail,
      makeReasoningNode,
      renderAssistantMarkdown,
    } = ctx;
    if (!panelEl || !titleEl || !metaEl || !blockersEl || !framesEl || !outputEl) {
      return;
    }

    const detail = detailView?.detail || null;
    if (!detail || !detailView) {
      panelEl.hidden = true;
      panelEl.setAttribute('aria-hidden', 'true');
      if (sessionRailEl) {
        sessionRailEl.innerHTML = '';
        sessionRailEl.hidden = true;
      }
      if (detailTabsEl) detailTabsEl.innerHTML = '';
      blockersEl.innerHTML = '';
      framesEl.innerHTML = '';
      outputEl.innerHTML = '';
      return;
    }

    panelEl.hidden = false;
    panelEl.setAttribute('aria-hidden', 'false');
    renderSubagentSessionRail({
      sessionRailEl,
      orderedDetails,
      selectedKey,
      onSelectDetail,
    });
    renderSubagentDetailTabs({
      detailTabsEl,
      visibleTabs,
      selectedKey,
      onSelectDetail,
    });

    titleEl.textContent = detailView.titleEmoji
      ? `${detailView.titleEmoji} ${detailView.title}`
      : detailView.title;
    metaEl.textContent = detailView.metaText;

    blockersEl.innerHTML = '';
    for (const blocker of detailView.blockers) {
      const item = document.createElement('div');
      item.className = `subagent-blocker ${blocker.kind}`;
      const badge = document.createElement('span');
      badge.className = 'subagent-blocker-badge';
      badge.textContent = blocker.kind === 'permission' ? '授权' :
        blocker.kind === 'question' ? '提问' :
        blocker.kind === 'retry' ? '重试' :
        blocker.kind === 'error' ? '失败' : '阻塞';
      const text = document.createElement('span');
      text.className = 'subagent-blocker-text';
      text.textContent = blocker.text;
      item.appendChild(badge);
      item.appendChild(text);
      blockersEl.appendChild(item);
    }

    if (detailView.blockerHistory.length) {
      const historyWrap = document.createElement('div');
      historyWrap.className = 'subagent-blocker-history';
      const historyTitle = document.createElement('div');
      historyTitle.className = 'subagent-blocker-history-title';
      historyTitle.textContent = '最近阻塞记录';
      historyWrap.appendChild(historyTitle);

      for (const blocker of detailView.blockerHistory.slice(0, 4)) {
        const row = document.createElement('div');
        row.className = `subagent-blocker-history-row ${blocker.state || 'blocked'}`;
        row.textContent = `${blocker.state === 'resolved' ? '已恢复' : '阻塞'} · ${blocker.text}`;
        historyWrap.appendChild(row);
      }
      blockersEl.appendChild(historyWrap);
    }

    const permissionCount = detailView.queueSummary.permissionCount;
    const questionCount = detailView.queueSummary.questionCount;
    if (permissionCount || questionCount) {
      const queueWrap = document.createElement('div');
      queueWrap.className = 'subagent-queue-panel';
      const queueTitle = document.createElement('div');
      queueTitle.className = 'subagent-queue-title';
      queueTitle.textContent = 'Pending queue';
      queueWrap.appendChild(queueTitle);

      if (permissionCount) {
        const row = document.createElement('div');
        row.className = 'subagent-queue-row permission';
        row.textContent = `权限请求 ${permissionCount} 个 · ${detailView.queueSummary.permissionPrompt}`;
        queueWrap.appendChild(row);
      }
      if (questionCount) {
        const row = document.createElement('div');
        row.className = 'subagent-queue-row question';
        row.textContent = `待回答问题 ${questionCount} 个 · ${detailView.queueSummary.questionPrompt}`;
        queueWrap.appendChild(row);
      }
      blockersEl.appendChild(queueWrap);
    }

    framesEl.innerHTML = '';
    const frames = detailView.frames;
    if (!frames.length) {
      const empty = document.createElement('div');
      empty.className = 'subagent-detail-empty';
      empty.textContent = detailView.framesEmptyText || '暂无事件帧';
      framesEl.appendChild(empty);
    } else {
      for (const frame of frames) {
        const item = document.createElement('div');
        item.className = `subagent-detail-frame ${frame.type || frame.phase || 'subagent_progress'}`;

        const badge = document.createElement('span');
        badge.className = 'subagent-detail-frame-badge';
        badge.textContent = frame.badge || '进展';

        const text = document.createElement('div');
        text.className = 'subagent-detail-frame-text';
        text.textContent = frame.text || '';

        item.appendChild(badge);
        item.appendChild(text);
        framesEl.appendChild(item);
      }
    }

    const commits = detailView.commits;
    if (commits.length) {
      const commitWrap = document.createElement('section');
      commitWrap.className = 'subagent-commit-panel';
      const commitTitle = document.createElement('div');
      commitTitle.className = 'subagent-commit-title';
      commitTitle.textContent = detailView.commitTitle || 'Session commits';
      commitWrap.appendChild(commitTitle);

      for (const commit of commits.slice().reverse()) {
        const row = document.createElement('div');
        row.className = `subagent-commit-row ${commit.kind || 'progress'} ${commit.phase || 'running'}`;
        const badge = document.createElement('span');
        badge.className = 'subagent-commit-badge';
        badge.textContent = commit.badge || '进展';
        const text = document.createElement('div');
        text.className = 'subagent-commit-text';
        text.textContent = commit.text_line || '';
        row.appendChild(badge);
        row.appendChild(text);
        commitWrap.appendChild(row);
      }
      framesEl.appendChild(commitWrap);
    }

    const historyItems = Array.isArray(detailView.historyItems) ? detailView.historyItems : [];
    if (historyItems.length) {
      const historyWrap = document.createElement('section');
      historyWrap.className = 'subagent-commit-panel';
      const historyTitle = document.createElement('div');
      historyTitle.className = 'subagent-commit-title';
      historyTitle.textContent = detailView.historyTitle || 'Session history';
      historyWrap.appendChild(historyTitle);

      for (const item of historyItems.slice().reverse()) {
        const row = document.createElement('article');
        row.className = `message-row ${item.role || 'assistant'}`;
        const card = document.createElement('div');
        card.className = `message-card ${item.role || 'assistant'}`;
        if (item.role === 'assistant' && item.reasoning) {
          const reasoningNode = makeReasoningNode(item.reasoning);
          if (reasoningNode) {
            card.appendChild(reasoningNode);
          }
        }
        const copy = document.createElement('div');
        copy.className = 'message-copy';
        if (item.role === 'assistant') {
          renderAssistantMarkdown(copy, item.content || '');
        } else {
          copy.textContent = item.content || '';
        }
        card.appendChild(copy);
        row.appendChild(card);
        historyWrap.appendChild(row);
      }
      framesEl.appendChild(historyWrap);
    }

    outputEl.innerHTML = '';
    if (detailView.outputText) {
      const summaryLabel = document.createElement('div');
      summaryLabel.className = 'subagent-detail-output-label';
      summaryLabel.textContent = 'Session summary';
      const summaryBody = document.createElement('div');
      summaryBody.className = 'subagent-detail-summary-body';
      summaryBody.textContent = detailView.summaryText || '该子任务已产生最终输出';
      const outputLabel = document.createElement('div');
      outputLabel.className = 'subagent-detail-output-label';
      outputLabel.textContent = 'Final output';
      const outputBody = document.createElement('pre');
      outputBody.className = 'subagent-detail-output-body';
      outputBody.textContent = detailView.outputText;
      outputEl.appendChild(summaryLabel);
      outputEl.appendChild(summaryBody);
      outputEl.appendChild(outputLabel);
      outputEl.appendChild(outputBody);
    } else {
      const empty = document.createElement('div');
      empty.className = 'subagent-detail-empty';
      empty.textContent = detailView.outputEmptyText || '暂无最终输出';
      outputEl.appendChild(empty);
    }
  }

  global.AgentSubagentDetailView = {
    renderSubagentDetailPanel,
  };
})(window);
