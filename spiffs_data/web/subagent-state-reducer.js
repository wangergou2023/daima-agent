(function initSubagentStateReducerModule(global) {
  const core = global.AgentSubagentStateCore || {};
  const selectors = global.AgentSubagentStateSelectors || {};
  const {
    trimText,
    mergeTimeline,
    mergeBlockers,
    mergeCommits,
    deriveLatestFrame,
    shouldClearCurrentBlocker,
    derivePendingQueue,
    normalizeChildSession,
    mergeSessionFrames,
    mergeSessionTimeline,
    mergeSessionCommits,
    mergeSessionHistory,
    shouldPreserveNewerDetail,
    childSessionIsOlderThanDetail,
    isTerminalStatus,
    deriveSessionSummary,
    createEmptySubagentUiState,
    subagentEventKey,
    detailKeyForAgent,
    subagentEventsForAgent,
    buildInteractiveBlocker,
    normalizeInteractiveSnapshot,
  } = core;
  const { interactiveBlockerKey, selectedSubagentDetail } = selectors;

  function reduceSubagentUiEvent(state, action, helpers) {
    if (!state || !action || !action.kind || !helpers) return state;
    const nextState = state;

    if (action.kind === 'reset') {
      return createEmptySubagentUiState();
    }

    if (action.kind === 'select_tab') {
      const key = trimText(action.key);
      if (!key) return nextState;
      nextState.selectedTabKey = key;
      return nextState;
    }

    if (action.kind === 'subagent_event') {
      const payload = action.payload || {};
      const payloadVisibleRevision = Number(payload?.visible_revision) || 0;
      if (payloadVisibleRevision > (Number(nextState?.liveCursor?.visibleRevision) || 0)) {
        nextState.liveCursor = {
          ...(nextState.liveCursor || {}),
          visibleRevision: payloadVisibleRevision,
        };
      }
      const key = action.key || subagentEventKey(payload);
      if (!key) return nextState;
      if (action.entry) {
        const events = nextState.eventLog.get(key) || [];
        const last = events[events.length - 1];
        if (!last ||
            last.type !== action.entry.type ||
            last.status !== action.entry.status ||
            last.detail !== action.entry.detail ||
            last.task !== action.entry.task) {
          const nextEvents = events.slice();
          nextEvents.push(action.entry);
          if (nextEvents.length > 8) {
            nextEvents.splice(0, nextEvents.length - 8);
          }
          nextState.eventLog.set(key, nextEvents);
        }
      }
      const previous = nextState.details.get(key) || {};
      const incomingStatus = trimText(payload?.status);
      const preserveTerminalDetail = isTerminalStatus(previous.status) &&
        !isTerminalStatus(incomingStatus) &&
        trimText(previous.output || previous.session_summary);
      const timeline = mergeTimeline(previous.timeline, payload, previous);
      const blockers = mergeBlockers(previous.blockers, payload, previous);
      const commits = mergeCommits(previous.commits, payload, previous);
      const latestFrame = deriveLatestFrame(timeline);
      const clearBlocker = shouldClearCurrentBlocker(payload);
      const nextDetail = {
        ...previous,
        key,
        task_id: trimText(payload?.task_id) || previous.task_id || '',
        task_key: trimText(payload?.task_key) || previous.task_key || '',
        session_id: trimText(payload?.session_id) || previous.session_id || '',
        coordinator_id: trimText(payload?.coordinator_id) || previous.coordinator_id || '',
        team_run_id: trimText(payload?.team_run_id) || previous.team_run_id || '',
        subagent_type: trimText(payload?.subagent_type) || previous.subagent_type || '',
        task: trimText(payload?.task) || previous.task || '',
        status: preserveTerminalDetail ? (previous.status || '') : (incomingStatus || previous.status || ''),
        model: trimText(payload?.model) || previous.model || '',
        scope_path: trimText(payload?.scope_path) || previous.scope_path || '',
        scope_kind: trimText(payload?.scope_kind) || previous.scope_kind || '',
        analysis_focus: trimText(payload?.analysis_focus) || previous.analysis_focus || '',
        depends_on: trimText(payload?.depends_on) || previous.depends_on || '',
        elapsed_ms: Number(payload?.elapsed_ms) || previous.elapsed_ms || 0,
        output: preserveTerminalDetail ? (previous.output || '') : (trimText(payload?.output) || previous.output || ''),
        target_files: trimText(payload?.target_files) || previous.target_files || '',
        write_approved: payload?.write_approved === true || previous.write_approved === true,
        blocker_kind: preserveTerminalDetail ? (previous.blocker_kind || '') : (clearBlocker ? '' : (trimText(payload?.blocker_kind) || previous.blocker_kind || '')),
        blocker_text: preserveTerminalDetail ? (previous.blocker_text || '') : (clearBlocker ? '' : (trimText(payload?.blocker_text) || previous.blocker_text || '')),
        blocker_scope: preserveTerminalDetail ? (previous.blocker_scope || '') : (clearBlocker ? '' : (trimText(payload?.blocker_scope) || previous.blocker_scope || '')),
        wake_last_error: trimText(payload?.wake_last_error) || previous.wake_last_error || '',
        frames: Array.isArray(action.events) ? action.events : (previous.frames || []),
        timeline,
        blockers,
        commits,
        history: previous.history || [],
        latest_frame: latestFrame,
        pending_request: clearBlocker ? null : (previous.pending_request || null),
      };
      nextDetail.pending_queue = derivePendingQueue(nextDetail);
      nextDetail.session_summary = deriveSessionSummary(nextDetail, latestFrame) || previous.session_summary || '';
      nextState.details.set(key, nextDetail);
      if (!nextState.selectedTabKey) {
        nextState.selectedTabKey = key;
      }
      return nextState;
    }

    if (action.kind === 'subagent_session') {
      const payload = action.payload || {};
      const agent = payload.agent && typeof payload.agent === 'object' ? payload.agent : null;
      const payloadVisibleRevision = Number(payload?.visible_revision) || 0;
      if (payloadVisibleRevision > (Number(nextState?.liveCursor?.visibleRevision) || 0)) {
        nextState.liveCursor = {
          ...(nextState.liveCursor || {}),
          visibleRevision: payloadVisibleRevision,
        };
      }
      const key = trimText(payload?.task_id) || trimText(payload?.session_id) ||
        trimText(agent?.task_id) || trimText(agent?.session_id);
      if (!key || !agent) return nextState;

      const previous = nextState.details.get(key) || {};
      const previousVisibleRevision = Number(previous?.visible_revision) || 0;
      const coordinatorVisibleRevision = Number(
        nextState.coordinators.get(trimText(payload?.coordinator_id))?.visible_revision
      ) || 0;
      const staleVisibleRevision = payloadVisibleRevision > 0 &&
        Math.max(previousVisibleRevision, coordinatorVisibleRevision) > payloadVisibleRevision;
      const childSession = normalizeChildSession(agent.child_session, {
        status: trimText(payload?.status) || trimText(agent?.status),
        task: trimText(payload?.task) || trimText(agent?.description),
      });
      const incomingStatusIsNonTerminal = !isTerminalStatus(trimText(payload?.status) || trimText(agent?.status));
      const replaceChildSession = action.replaceChildSession === true ||
        childSession?.window?.replay_reset === true;
      const staleByTerminalRegression = !replaceChildSession &&
        isTerminalStatus(previous.status) &&
        incomingStatusIsNonTerminal;
      const staleChildSession = childSessionIsOlderThanDetail(previous, childSession);
      const status = trimText(payload?.status) || trimText(agent?.status) || previous.status || '';
      const preserveNewerDetail = shouldPreserveNewerDetail(previous, status, childSession, previous.status);
      const blockerKind = trimText(agent?.blocker_kind);
      const blockerText = trimText(agent?.blocker_text);
      const childPendingRequest = childSession?.pending_request || null;
      const childPendingQueue = childSession?.pending_queue || null;
      const childHasPendingQueue = !!(
        childPendingQueue &&
        ((Array.isArray(childPendingQueue.permissions) && childPendingQueue.permissions.length > 0) ||
          (Array.isArray(childPendingQueue.questions) && childPendingQueue.questions.length > 0))
      );
      const childLatestFrameBlockerKind = trimText(childSession?.latest_frame?.blocker_kind);
      const childLatestFrameBlockerText = trimText(childSession?.latest_frame?.blocker_text);
      const childIndicatesBlocked = !!(
        childPendingRequest ||
        childHasPendingQueue ||
        childLatestFrameBlockerKind ||
        childLatestFrameBlockerText
      );
      const staleByCompletedSummaryRegression = !!(
        !replaceChildSession &&
        isTerminalStatus(previous.status) &&
        trimText(previous.output || previous.session_summary) &&
        !trimText(agent?.output) &&
        childIndicatesBlocked
      );
      const freezeChildSession = preserveNewerDetail ||
        staleChildSession ||
        staleVisibleRevision ||
        staleByTerminalRegression ||
        staleByCompletedSummaryRegression;
      const mergedTimeline = childSession?.frames?.length
        ? (freezeChildSession
          ? (previous.timeline || [])
          : (replaceChildSession ? childSession.frames.slice() : mergeSessionTimeline(previous.timeline, childSession.frames)))
        : (previous.timeline || []);
      const mergedCommits = childSession?.commits?.length
        ? (freezeChildSession
          ? (previous.commits || [])
          : (replaceChildSession ? childSession.commits.slice() : mergeSessionCommits(previous.commits, childSession.commits)))
        : (previous.commits || []);
      const latestFrame = deriveLatestFrame(mergedTimeline);
      const clearBlocker = replaceChildSession &&
        !blockerKind &&
        !blockerText &&
        !childIndicatesBlocked &&
        (status === 'running' || status === 'done');
      const shouldPreservePendingState = !replaceChildSession &&
        !clearBlocker &&
        !childPendingRequest &&
        !childHasPendingQueue;
      const nextDetail = {
        ...previous,
        key,
        task_id: trimText(payload?.task_id) || trimText(agent?.task_id) || previous.task_id || '',
        task_key: trimText(payload?.task_key) || trimText(agent?.task_key) || previous.task_key || '',
        session_id: trimText(payload?.session_id) || trimText(agent?.session_id) || previous.session_id || '',
        coordinator_id: trimText(payload?.coordinator_id) || previous.coordinator_id || '',
        visible_revision: Math.max(previousVisibleRevision, payloadVisibleRevision),
        subagent_type: trimText(payload?.subagent_type) || trimText(agent?.subagent_type) || previous.subagent_type || '',
        task: trimText(payload?.task) || trimText(agent?.description) || previous.task || '',
        status: (preserveNewerDetail || staleVisibleRevision || staleByTerminalRegression || staleByCompletedSummaryRegression) &&
          isTerminalStatus(previous.status) && !isTerminalStatus(status)
          ? previous.status
          : status,
        model: trimText(agent?.model) || previous.model || '',
        scope_path: trimText(agent?.scope_path) || previous.scope_path || '',
        scope_kind: trimText(agent?.scope_kind) || previous.scope_kind || '',
        analysis_focus: trimText(agent?.analysis_focus) || previous.analysis_focus || '',
        depends_on: trimText(payload?.depends_on) || trimText(agent?.depends_on) || previous.depends_on || '',
        elapsed_ms: Number(agent?.elapsed_ms) || previous.elapsed_ms || 0,
        output: preserveNewerDetail && !trimText(agent?.output)
          ? (previous.output || '')
          : (replaceChildSession
            ? (trimText(agent?.output) || trimText(childSession?.summary) || '')
            : (trimText(agent?.output) || previous.output || '')),
        target_files: trimText(agent?.target_files) || previous.target_files || '',
        write_approved: agent?.write_approved === true || previous.write_approved === true,
        blocker_kind: preserveNewerDetail
          ? (clearBlocker ? '' : (previous.blocker_kind || ''))
          : ((staleVisibleRevision || staleByTerminalRegression || staleByCompletedSummaryRegression)
            ? (previous.blocker_kind || '')
            : (clearBlocker ? '' : (blockerKind || previous.blocker_kind || ''))),
        blocker_text: preserveNewerDetail
          ? (clearBlocker ? '' : (previous.blocker_text || ''))
          : ((staleVisibleRevision || staleByTerminalRegression || staleByCompletedSummaryRegression)
            ? (previous.blocker_text || '')
            : (clearBlocker ? '' : (blockerText || previous.blocker_text || ''))),
        frames: childSession?.frames?.length
          ? (freezeChildSession
            ? (previous.frames || [])
            : (replaceChildSession ? childSession.frames : mergeSessionFrames(previous.frames, childSession.frames)))
          : (previous.frames || []),
        timeline: mergedTimeline,
        blockers: previous.blockers || [],
        commits: mergedCommits,
        history: freezeChildSession
          ? (previous.history || [])
          : (replaceChildSession ? (childSession?.history || []) : mergeSessionHistory(previous.history, childSession?.history)),
        latest_frame: freezeChildSession ? (previous.latest_frame || latestFrame) : (childSession?.latest_frame || latestFrame),
        window: freezeChildSession ? (previous.window || null) : (childSession?.window || previous.window || null),
      };
      nextDetail.pending_request = freezeChildSession
        ? (previous.pending_request || null)
        : (clearBlocker
          ? null
          : (shouldPreservePendingState
            ? (previous.pending_request || null)
            : (childPendingRequest || previous.pending_request || null)));
      nextDetail.pending_queue = preserveNewerDetail
        ? derivePendingQueue(nextDetail)
        : (freezeChildSession
          ? derivePendingQueue(nextDetail)
          : (clearBlocker
            ? derivePendingQueue(nextDetail)
            : (shouldPreservePendingState
              ? (previous.pending_queue || derivePendingQueue(nextDetail))
              : (childPendingQueue || derivePendingQueue(nextDetail)))));
      nextDetail.session_summary = preserveNewerDetail
        ? (previous.session_summary || deriveSessionSummary(nextDetail, latestFrame) || '')
        : (freezeChildSession
          ? (previous.session_summary || deriveSessionSummary(nextDetail, latestFrame) || '')
          : (childSession?.summary || deriveSessionSummary(nextDetail, latestFrame) || previous.session_summary || ''));
      if (replaceChildSession && !freezeChildSession) {
        nextDetail.blockers = [];
      }
      nextState.details.set(key, nextDetail);
      if (!nextState.selectedTabKey) {
        nextState.selectedTabKey = key;
      }
      return nextState;
    }

    if (action.kind === 'interactive_blocker_set') {
      const blocker = action.blocker && typeof action.blocker === 'object' ? action.blocker : null;
      const key = interactiveBlockerKey(blocker);
      if (!key) return nextState;
      nextState.interactiveBlockers.set(key, { ...blocker });
      const detailKey = trimText(blocker?.task_id) || trimText(blocker?.session_id);
      if (detailKey) {
        const previous = nextState.details.get(detailKey) || {};
        const payload = {
          type: 'subagent_request',
          task_id: trimText(blocker?.task_id),
          session_id: trimText(blocker?.session_id),
          coordinator_id: trimText(blocker?.coordinator_id),
          subagent_type: trimText(previous?.subagent_type) || 'delegate',
          status: 'blocked',
          task: trimText(previous?.task) || trimText(blocker?.label) || 'interactive request',
          detail: trimText(blocker?.prompt),
          blocker_kind: trimText(blocker?.blocker_kind),
          blocker_text: trimText(blocker?.prompt),
          blocker_scope: trimText(blocker?.task_id) || trimText(blocker?.session_id) ? 'task' : 'coordinator',
          ts: Date.now(),
        };
        const timeline = mergeTimeline(previous.timeline, payload, previous);
        const blockers = mergeBlockers(previous.blockers, payload, previous);
        const commits = mergeCommits(previous.commits, payload, previous);
        const latestFrame = deriveLatestFrame(timeline);
        const nextDetail = {
          ...previous,
          key: detailKey,
          task_id: trimText(blocker?.task_id) || previous.task_id || '',
          session_id: trimText(blocker?.session_id) || previous.session_id || '',
          coordinator_id: trimText(blocker?.coordinator_id) || previous.coordinator_id || '',
          task: trimText(previous?.task) || trimText(blocker?.label) || 'interactive request',
          status: 'blocked',
          blocker_kind: trimText(blocker?.blocker_kind) || previous.blocker_kind || '',
          blocker_text: trimText(blocker?.prompt) || previous.blocker_text || '',
          blocker_scope: 'task',
          pending_request: {
            request_type: trimText(blocker?.request_type),
            request_id: trimText(blocker?.request_id),
            prompt: trimText(blocker?.prompt),
          },
          timeline,
          blockers,
          commits,
          history: previous.history || [],
          latest_frame: latestFrame,
        };
        nextDetail.pending_queue = derivePendingQueue(nextDetail);
        nextDetail.session_summary = deriveSessionSummary(nextDetail, latestFrame) || previous.session_summary || '';
        nextState.details.set(detailKey, nextDetail);
      }
      {
        const candidate = trimText(blocker?.task_id) || trimText(blocker?.session_id);
        if (candidate) {
          nextState.selectedTabKey = candidate;
        }
      }
      return nextState;
    }

    if (action.kind === 'interactive_blocker_clear') {
      const key = trimText(action.key) || interactiveBlockerKey(action.blocker);
      if (!key) return nextState;
      const blocker = nextState.interactiveBlockers.get(key) || null;
      nextState.interactiveBlockers.delete(key);
      const detailKey = trimText(blocker?.task_id) || trimText(blocker?.session_id);
      if (detailKey && nextState.details.has(detailKey)) {
        const previous = nextState.details.get(detailKey) || {};
        const nextDetail = {
          ...previous,
          pending_request: null,
        };
        nextDetail.pending_queue = derivePendingQueue(nextDetail);
        nextState.details.set(detailKey, nextDetail);
      }
      return nextState;
    }

    if (action.kind === 'coordinator') {
      const next = helpers.normalizeCoordinatorPayload(action.payload);
      if (!next.coordinator_id) return nextState;
      if ((Number(next.visible_revision) || 0) > (Number(nextState?.liveCursor?.visibleRevision) || 0)) {
        nextState.liveCursor = {
          ...(nextState.liveCursor || {}),
          visibleRevision: Number(next.visible_revision) || 0,
        };
      }
      const previous = nextState.coordinators.get(next.coordinator_id);
      const staleCoordinatorRevision = previous &&
        Number(previous.visible_revision) > 0 &&
        Number(next.visible_revision) > 0 &&
        Number(next.visible_revision) < Number(previous.visible_revision);
      for (const agent of next.agents) {
        const detailKey = detailKeyForAgent(agent);
        if (detailKey) {
          const previousDetail = nextState.details.get(detailKey) || {};
          const events = subagentEventsForAgent(agent, nextState);
          const mergedBlockerKind = trimText(agent.blocker_kind) || trimText(agent.coordinator_blocker_kind);
          const mergedBlockerText = trimText(agent.blocker_text) || trimText(agent.coordinator_blocker_text);
          const shouldClearMergedBlocker = !mergedBlockerKind &&
            !mergedBlockerText &&
            (trimText(agent.status) === 'done' || trimText(agent.status) === 'running');
          const childSession = normalizeChildSession(agent.child_session, {
            status: agent.status,
            task: agent.name,
          });
          const staleChildSession = childSessionIsOlderThanDetail(previousDetail, childSession);
          const preserveNewerDetail = shouldPreserveNewerDetail(previousDetail, agent.status, childSession, previousDetail.status);
          const freezeChildSession = preserveNewerDetail || staleChildSession || staleCoordinatorRevision;
          const mergedStatus = preserveNewerDetail &&
            isTerminalStatus(previousDetail.status) &&
            !isTerminalStatus(agent.status)
            ? (previousDetail.status || '')
            : (agent.status || previousDetail.status || '');
          const childTimeline = preserveNewerDetail
            ? (previousDetail.timeline || [])
            : (freezeChildSession
              ? (previousDetail.timeline || [])
              : (childSession?.frames?.length
              ? mergeSessionTimeline(previousDetail.timeline, childSession.frames)
              : (previousDetail.timeline || [])));
          const latestFrame = deriveLatestFrame(childTimeline);
          const mergedCommits = preserveNewerDetail
            ? (previousDetail.commits || [])
            : (freezeChildSession
              ? (previousDetail.commits || [])
              : (childSession?.commits?.length
              ? mergeSessionCommits(previousDetail.commits, childSession.commits)
              : (previousDetail.commits || [])));
          const nextDetail = {
            ...previousDetail,
            key: detailKey,
            task_id: agent.task_id || previousDetail.task_id || '',
            task_key: agent.task_key || previousDetail.task_key || '',
            session_id: agent.session_id || previousDetail.session_id || '',
            coordinator_id: next.coordinator_id || previousDetail.coordinator_id || '',
            visible_revision: Math.max(Number(previousDetail.visible_revision) || 0, Number(next.visible_revision) || 0),
            team_run_id: next.team_run_id || previousDetail.team_run_id || '',
            subagent_type: agent.subagent_type || previousDetail.subagent_type || '',
            task: agent.name || previousDetail.task || '',
            status: staleCoordinatorRevision ? (previousDetail.status || mergedStatus) : mergedStatus,
            model: agent.model || previousDetail.model || '',
            scope_path: agent.scope_path || previousDetail.scope_path || '',
            scope_kind: agent.scope_kind || previousDetail.scope_kind || '',
            analysis_focus: agent.analysis_focus || previousDetail.analysis_focus || '',
            depends_on: agent.depends_on || previousDetail.depends_on || '',
            elapsed_ms: Number(agent.elapsed_ms) || previousDetail.elapsed_ms || 0,
            output: preserveNewerDetail && !trimText(agent.output)
              ? (previousDetail.output || '')
              : (staleCoordinatorRevision ? (previousDetail.output || '') : (agent.output || previousDetail.output || '')),
            target_files: agent.target_files || previousDetail.target_files || '',
            write_approved: agent.write_approved || previousDetail.write_approved || false,
            blocker_kind: preserveNewerDetail
              ? (shouldClearMergedBlocker ? '' : (previousDetail.blocker_kind || ''))
              : (staleCoordinatorRevision ? (previousDetail.blocker_kind || '') : (shouldClearMergedBlocker ? '' : (mergedBlockerKind || previousDetail.blocker_kind || ''))),
            blocker_text: preserveNewerDetail
              ? (shouldClearMergedBlocker ? '' : (previousDetail.blocker_text || ''))
              : (staleCoordinatorRevision ? (previousDetail.blocker_text || '') : (shouldClearMergedBlocker ? '' : (mergedBlockerText || previousDetail.blocker_text || ''))),
            blocker_scope: preserveNewerDetail
              ? (shouldClearMergedBlocker ? '' : (previousDetail.blocker_scope || ''))
              : (staleCoordinatorRevision ? (previousDetail.blocker_scope || '') : (shouldClearMergedBlocker ? '' : (agent.blocker_kind ? 'task' : (agent.coordinator_blocker_kind ? 'coordinator' : (previousDetail.blocker_scope || ''))))),
            wake_last_error: agent.wake_last_error || previousDetail.wake_last_error || '',
            pending_request: childSession?.pending_request || agent.pending_request || previousDetail.pending_request || null,
            frames: childSession?.frames?.length
              ? (freezeChildSession ? (previousDetail.frames || []) : mergeSessionFrames(previousDetail.frames, childSession.frames))
              : events,
            timeline: childTimeline,
            blockers: previousDetail.blockers || [],
            commits: mergedCommits,
            history: freezeChildSession ? (previousDetail.history || []) : mergeSessionHistory(previousDetail.history, childSession?.history),
            latest_frame: freezeChildSession ? (previousDetail.latest_frame || latestFrame) : (childSession?.latest_frame || latestFrame),
            window: freezeChildSession ? (previousDetail.window || null) : (childSession?.window || previousDetail.window || null),
          };
          if (freezeChildSession) {
            nextDetail.pending_request = previousDetail.pending_request || null;
          }
          nextDetail.pending_queue = preserveNewerDetail
            ? derivePendingQueue(nextDetail)
            : (freezeChildSession ? derivePendingQueue(nextDetail) : (childSession?.pending_queue || derivePendingQueue(nextDetail)));
          nextDetail.session_summary = preserveNewerDetail
            ? (previousDetail.session_summary || deriveSessionSummary(nextDetail, latestFrame) || '')
            : (freezeChildSession
              ? (previousDetail.session_summary || deriveSessionSummary(nextDetail, latestFrame) || '')
              : (childSession?.summary || deriveSessionSummary(nextDetail, latestFrame) || previousDetail.session_summary || ''));
          nextState.details.set(detailKey, nextDetail);
        }
      }

      const mergedCoordinator = staleCoordinatorRevision
        ? { ...previous }
        : (previous ? { ...previous, ...next } : { ...next });
      delete mergedCoordinator.agents;
      nextState.coordinators.set(next.coordinator_id, mergedCoordinator);
      if (!nextState.selectedTabKey) {
        const firstAgent = next.agents[0];
        const firstKey = detailKeyForAgent(firstAgent);
        if (firstKey) {
          nextState.selectedTabKey = firstKey;
        }
      }
    }

    return nextState;
  }

  function hydrateStateFromSnapshot(snapshot, options) {
    const chatId = trimText(options?.chatId);
    const interactiveUiConfig = options?.interactiveUiConfig;
    let state = createEmptySubagentUiState();
    const coordinators = Array.isArray(snapshot?.coordinators) ? snapshot.coordinators : [];

    for (const coordinator of coordinators) {
      state = reduceSubagentUiEvent(state, { kind: 'coordinator', payload: coordinator }, {
        normalizeCoordinatorPayload: options?.normalizeCoordinatorPayload || core.normalizeCoordinatorPayload,
      });
    }

    if (typeof interactiveUiConfig === 'function') {
      const snapshotBlockers = normalizeInteractiveSnapshot(snapshot, chatId, interactiveUiConfig);
      for (const blocker of snapshotBlockers) {
        state = reduceSubagentUiEvent(state, { kind: 'interactive_blocker_set', blocker }, {
          normalizeCoordinatorPayload: options?.normalizeCoordinatorPayload || core.normalizeCoordinatorPayload,
        });
      }

      if (!snapshotBlockers.length) {
      for (const detail of state.details.values()) {
        const blocker = buildInteractiveBlocker(detail, detail?.pending_request, chatId, interactiveUiConfig);
        if (!blocker) continue;
        state = reduceSubagentUiEvent(state, { kind: 'interactive_blocker_set', blocker }, {
          normalizeCoordinatorPayload: options?.normalizeCoordinatorPayload || core.normalizeCoordinatorPayload,
        });
      }
      }
    }

    return state;
  }

  global.AgentSubagentStateReducer = {
    reduceSubagentUiEvent,
    hydrateStateFromSnapshot,
    selectedSubagentDetail,
  };
})(window);
