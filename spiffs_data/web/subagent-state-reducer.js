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
    childSessionFreshnessRevision,
    deriveCanonicalOutputText,
    isTerminalStatus,
    deriveSessionSummary,
    deriveVisibleOutputText,
    createEmptySubagentUiState,
    subagentEventKey,
    detailKeyForAgent,
    subagentEventsForAgent,
    buildInteractiveBlocker,
    normalizeInteractiveSnapshot,
  } = core;
  const { interactiveBlockerKey, selectedSubagentDetail, orderedSubagentDetails } = selectors;

  function cursorVisibleRevision(payload) {
    return Number(payload?.replay_cursor?.visible_revision) ||
      Number(payload?.visible_revision) ||
      0;
  }

  function cursorAfterVisibleRevision(payload) {
    return Number(payload?.replay_cursor?.after_visible_revision) || 0;
  }

  function mergeLiveCursor(state, payload) {
    const currentVisibleRevision = Number(state?.liveCursor?.visibleRevision) || 0;
    const currentAfterVisibleRevision = Number(state?.liveCursor?.afterVisibleRevision) || 0;
    const nextVisibleRevision = cursorVisibleRevision(payload);
    const nextAfterVisibleRevision = cursorAfterVisibleRevision(payload);
    if (nextVisibleRevision <= 0 && nextAfterVisibleRevision <= 0) {
      return false;
    }
    state.liveCursor = {
      ...(state.liveCursor || {}),
      visibleRevision: Math.max(currentVisibleRevision, nextVisibleRevision),
      afterVisibleRevision: Math.max(currentAfterVisibleRevision, nextAfterVisibleRevision),
    };
    return true;
  }

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

    if (action.kind === 'cursor') {
      mergeLiveCursor(nextState, action.payload || {});
      return nextState;
    }

    if (action.kind === 'subagent_event') {
      const payload = action.payload || {};
      const payloadVisibleRevision = cursorVisibleRevision(payload);
      mergeLiveCursor(nextState, payload);
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
        trimText(deriveVisibleOutputText(previous, previous.latest_frame));
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
        freshness_revision: Math.max(
          Number(previous?.freshness_revision) || 0,
          payloadVisibleRevision,
          Number(nextState?.liveCursor?.visibleRevision) || 0,
        ),
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
      const payloadVisibleRevision = cursorVisibleRevision(payload);
      mergeLiveCursor(nextState, payload);
      const key = trimText(payload?.task_id) || trimText(payload?.session_id) ||
        trimText(agent?.task_id) || trimText(agent?.session_id);
      if (!key || !agent) return nextState;

      const previous = nextState.details.get(key) || {};
      const previousVisibleRevision = Number(previous?.visible_revision) || 0;
      const previousFreshnessRevision = Number(previous?.freshness_revision) || previousVisibleRevision;
      const coordinatorVisibleRevision = Number(
        nextState.coordinators.get(trimText(payload?.coordinator_id))?.visible_revision
      ) || 0;
      const childSession = normalizeChildSession(agent.child_session, {
        status: trimText(payload?.status) || trimText(agent?.status),
        task: trimText(payload?.task) || trimText(agent?.description),
      });
      const childSessionRevision = childSessionFreshnessRevision(childSession);
      const incomingFreshnessRevision = Math.max(
        payloadVisibleRevision,
        childSessionRevision,
        Number(nextState?.liveCursor?.visibleRevision) || 0,
      );
      const staleVisibleRevision = payloadVisibleRevision > 0 &&
        Math.max(previousFreshnessRevision, coordinatorVisibleRevision) > incomingFreshnessRevision;
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
      const agentSummary = trimText(agent?.summary);
      const agentOutput = trimText(agent?.output || agent?.output_text);
      const childVisibleOutput = deriveCanonicalOutputText(childSession, previous, agentOutput || agentSummary);
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
        trimText(deriveVisibleOutputText(previous, previous.latest_frame)) &&
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
        freshness_revision: Math.max(previousFreshnessRevision, incomingFreshnessRevision),
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
        output: preserveNewerDetail && !agentOutput
          ? (previous.output || '')
          : (replaceChildSession
            ? (childVisibleOutput || previous.output || '')
            : (childVisibleOutput || previous.output || '')),
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
          : (childVisibleOutput || childSession?.summary || deriveSessionSummary(nextDetail, latestFrame) || previous.session_summary || ''));
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
      mergeLiveCursor(nextState, next);
      const previous = nextState.coordinators.get(next.coordinator_id);
      const runtimeVisibleRevision = Number(nextState?.liveCursor?.visibleRevision) || 0;
      const staleCoordinatorRevision = previous &&
        Number(previous.visible_revision) > 0 &&
        Number(next.visible_revision) > 0 &&
        Number(next.visible_revision) < Number(previous.visible_revision);
      const staleByRuntimeCursor = Number(next.visible_revision) > 0 &&
        runtimeVisibleRevision > 0 &&
        Number(next.visible_revision) < runtimeVisibleRevision;
      for (const agent of next.agents) {
        const detailKey = detailKeyForAgent(agent);
        if (detailKey) {
          const previousDetail = nextState.details.get(detailKey) || {};
          const previousDetailFreshnessRevision = Number(previousDetail?.freshness_revision) ||
            Number(previousDetail?.visible_revision) || 0;
          const events = subagentEventsForAgent(agent, nextState);
          const mergedBlockerKind = trimText(agent.blocker_kind) || trimText(agent.coordinator_blocker_kind);
          const mergedBlockerText = trimText(agent.blocker_text) || trimText(agent.coordinator_blocker_text);
          const shouldClearMergedBlocker = !mergedBlockerKind &&
            !mergedBlockerText &&
            (isTerminalStatus(trimText(agent.status)) || trimText(agent.status) === 'running');
          const childSession = normalizeChildSession(agent.child_session, {
            status: agent.status,
            task: agent.name,
          });
          const agentSummary = trimText(agent.summary);
          const agentOutput = trimText(agent.output);
          const childVisibleOutput = deriveCanonicalOutputText(childSession, previousDetail, agentOutput || agentSummary);
          const childSessionRevision = childSessionFreshnessRevision(childSession);
          const incomingFreshnessRevision = Math.max(
            Number(next.visible_revision) || 0,
            childSessionRevision,
            Number(nextState?.liveCursor?.visibleRevision) || 0,
          );
          const incomingCoordinatorRevision = Number(next.visible_revision) || 0;
          const previousEffectiveOutput = trimText(
            deriveVisibleOutputText(previousDetail, previousDetail.latest_frame)
          );
          const previousHadTerminalResult = isTerminalStatus(previousDetail.status) ||
            isTerminalStatus(trimText(previousDetail?.latest_frame?.status)) ||
            trimText(previousDetail?.latest_frame?.phase) === 'done' ||
            trimText(previousDetail?.latest_frame?.phase) === 'failed';
          const previousStatusForPreservation = previousHadTerminalResult
            ? 'done'
            : previousDetail.status;
          const staleChildSession = childSessionIsOlderThanDetail(previousDetail, childSession) ||
            (incomingFreshnessRevision > 0 && previousDetailFreshnessRevision > incomingFreshnessRevision);
          const staleByDetailRevision = incomingCoordinatorRevision > 0 &&
            previousDetailFreshnessRevision > incomingCoordinatorRevision;
          const preserveNewerDetail = shouldPreserveNewerDetail(
            previousDetail,
            agent.status,
            childSession,
            previousStatusForPreservation,
          );
          const incomingLooksBlockedOrIncomplete = !!(
            !agentOutput &&
            (
              trimText(agent.blocker_kind) ||
              trimText(agent.blocker_text) ||
              childSession?.pending_request ||
              ((Array.isArray(childSession?.pending_queue?.permissions) && childSession.pending_queue.permissions.length > 0) ||
                (Array.isArray(childSession?.pending_queue?.questions) && childSession.pending_queue.questions.length > 0)) ||
              trimText(childSession?.latest_frame?.blocker_kind) ||
              trimText(childSession?.latest_frame?.blocker_text) ||
              !isTerminalStatus(trimText(agent.status))
            )
          );
          const staleByEffectiveOutputRegression = !!(
            previousHadTerminalResult &&
            previousEffectiveOutput &&
            incomingLooksBlockedOrIncomplete
          );
          const staleByCompletedSummaryRegression = !!(
            previousHadTerminalResult &&
            !isTerminalStatus(trimText(agent.status)) &&
            previousEffectiveOutput &&
            !trimText(agent.output) &&
            (
              trimText(agent.blocker_kind) ||
              trimText(agent.blocker_text) ||
              childSession?.pending_request ||
              ((Array.isArray(childSession?.pending_queue?.permissions) && childSession.pending_queue.permissions.length > 0) ||
                (Array.isArray(childSession?.pending_queue?.questions) && childSession.pending_queue.questions.length > 0)) ||
              trimText(childSession?.latest_frame?.blocker_kind) ||
              trimText(childSession?.latest_frame?.blocker_text)
            )
          );
          const freezeChildSession = preserveNewerDetail ||
            staleChildSession ||
            staleByDetailRevision ||
            staleCoordinatorRevision ||
            staleByRuntimeCursor ||
            staleByCompletedSummaryRegression ||
            staleByEffectiveOutputRegression;
          const preserveExistingVisibleState = preserveNewerDetail ||
            staleByDetailRevision ||
            staleCoordinatorRevision ||
            staleByRuntimeCursor ||
            staleByCompletedSummaryRegression ||
            staleByEffectiveOutputRegression;
          const retainRicherSessionWindow = !freezeChildSession &&
            isTerminalStatus(trimText(agent.status)) &&
            (
              ((Array.isArray(previousDetail.history) ? previousDetail.history.length : 0) >
                (Array.isArray(childSession?.history) ? childSession.history.length : 0)) ||
              ((Array.isArray(previousDetail.timeline) ? previousDetail.timeline.length : 0) >
                (Array.isArray(childSession?.frames) ? childSession.frames.length : 0)) ||
              ((Array.isArray(previousDetail.commits) ? previousDetail.commits.length : 0) >
                (Array.isArray(childSession?.commits) ? childSession.commits.length : 0))
            );
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
          const nextTimeline = childTimeline.slice();
          const timelineNeedsTerminalSummaryFrame = !preserveNewerDetail &&
            !freezeChildSession &&
            isTerminalStatus(agent.status) &&
            !!childVisibleOutput;
          if (timelineNeedsTerminalSummaryFrame) {
            const terminalFrame = {
              id: trimText(previousDetail?.latest_frame?.id) || '',
              seq: Number(childSession?.latest_frame?.seq) || Number(previousDetail?.latest_frame?.seq) || 0,
              type: 'subagent_done',
              status: trimText(agent.status),
              phase: trimText(agent.status) === 'error' || trimText(agent.status) === 'failed' ? 'failed' : 'done',
              task: trimText(agent.name) || trimText(previousDetail?.task),
              detail: childVisibleOutput || trimText(previousDetail?.latest_frame?.detail) || '',
              output_preview: childVisibleOutput,
              blocker_kind: '',
              blocker_text: '',
              ts: Number(childSession?.latest_frame?.ts) || Number(previousDetail?.latest_frame?.ts) || Date.now(),
            };
            terminalFrame.key = [
              trimText(terminalFrame.type),
              trimText(terminalFrame.status),
              trimText(terminalFrame.phase),
              trimText(terminalFrame.detail),
              trimText(terminalFrame.output_preview),
              trimText(terminalFrame.blocker_kind),
            ].join('|');
            const lastFrame = nextTimeline[nextTimeline.length - 1];
            if (lastFrame && trimText(lastFrame.key) === trimText(terminalFrame.key)) {
              nextTimeline[nextTimeline.length - 1] = { ...lastFrame, ...terminalFrame };
            } else {
              nextTimeline.push(terminalFrame);
            }
          }
          const latestFrame = deriveLatestFrame(nextTimeline);
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
            freshness_revision: Math.max(previousDetailFreshnessRevision, incomingFreshnessRevision),
            team_run_id: next.team_run_id || previousDetail.team_run_id || '',
            subagent_type: agent.subagent_type || previousDetail.subagent_type || '',
            task: agent.name || previousDetail.task || '',
            status: preserveExistingVisibleState
              ? (previousDetail.status || mergedStatus)
              : mergedStatus,
            model: agent.model || previousDetail.model || '',
            scope_path: agent.scope_path || previousDetail.scope_path || '',
            scope_kind: agent.scope_kind || previousDetail.scope_kind || '',
            analysis_focus: agent.analysis_focus || previousDetail.analysis_focus || '',
            depends_on: agent.depends_on || previousDetail.depends_on || '',
            elapsed_ms: Number(agent.elapsed_ms) || previousDetail.elapsed_ms || 0,
            output: preserveNewerDetail && !agentOutput
              ? (previousDetail.output || '')
              : ((staleByDetailRevision || staleCoordinatorRevision || staleByRuntimeCursor || staleByCompletedSummaryRegression || staleByEffectiveOutputRegression)
                ? (previousDetail.output || '')
                : (childVisibleOutput || previousDetail.output || '')),
            target_files: agent.target_files || previousDetail.target_files || '',
            write_approved: agent.write_approved || previousDetail.write_approved || false,
            blocker_kind: preserveNewerDetail
              ? (shouldClearMergedBlocker ? '' : (previousDetail.blocker_kind || ''))
              : (preserveExistingVisibleState
                ? (previousDetail.blocker_kind || '')
                : (shouldClearMergedBlocker ? '' : (mergedBlockerKind || previousDetail.blocker_kind || ''))),
            blocker_text: preserveNewerDetail
              ? (shouldClearMergedBlocker ? '' : (previousDetail.blocker_text || ''))
              : (preserveExistingVisibleState
                ? (previousDetail.blocker_text || '')
                : (shouldClearMergedBlocker ? '' : (mergedBlockerText || previousDetail.blocker_text || ''))),
            blocker_scope: preserveNewerDetail
              ? (shouldClearMergedBlocker ? '' : (previousDetail.blocker_scope || ''))
              : (preserveExistingVisibleState
                ? (previousDetail.blocker_scope || '')
                : (shouldClearMergedBlocker ? '' : (agent.blocker_kind ? 'task' : (agent.coordinator_blocker_kind ? 'coordinator' : (previousDetail.blocker_scope || ''))))),
            wake_last_error: agent.wake_last_error || previousDetail.wake_last_error || '',
            pending_request: preserveExistingVisibleState
              ? (previousDetail.pending_request || null)
              : (shouldClearMergedBlocker
              ? null
              : (childSession?.pending_request || agent.pending_request || previousDetail.pending_request || null)),
            frames: childSession?.frames?.length
              ? (freezeChildSession
                ? (previousDetail.frames || [])
                : (retainRicherSessionWindow
                  ? mergeSessionFrames(previousDetail.frames, childSession.frames)
                  : mergeSessionFrames(previousDetail.frames, childSession.frames)))
              : events,
            timeline: nextTimeline,
            blockers: previousDetail.blockers || [],
            commits: mergedCommits,
            history: freezeChildSession
              ? (previousDetail.history || [])
              : (retainRicherSessionWindow
                ? mergeSessionHistory(previousDetail.history, childSession?.history)
                : mergeSessionHistory(previousDetail.history, childSession?.history)),
            latest_frame: freezeChildSession
              ? (previousDetail.latest_frame || latestFrame)
              : ((retainRicherSessionWindow && previousDetail.latest_frame && latestFrame &&
                  (Number(previousDetail.latest_frame.ts) || 0) > (Number(latestFrame?.ts) || 0))
                ? previousDetail.latest_frame
                : (childSession?.latest_frame || latestFrame)),
            window: freezeChildSession
              ? (previousDetail.window || null)
              : (retainRicherSessionWindow
                ? (previousDetail.window || childSession?.window || null)
                : (childSession?.window || previousDetail.window || null)),
          };
          if (freezeChildSession) {
            nextDetail.pending_request = previousDetail.pending_request || null;
          }
          if (shouldClearMergedBlocker && !freezeChildSession && !preserveNewerDetail) {
            nextDetail.pending_request = null;
          }
          nextDetail.pending_queue = preserveNewerDetail
            ? derivePendingQueue(nextDetail)
            : ((freezeChildSession || preserveExistingVisibleState)
              ? derivePendingQueue(nextDetail)
              : (shouldClearMergedBlocker
                ? derivePendingQueue(nextDetail)
                : (childSession?.pending_queue || derivePendingQueue(nextDetail))));
          nextDetail.session_summary = preserveNewerDetail
            ? (previousDetail.session_summary || deriveSessionSummary(nextDetail, latestFrame) || '')
            : ((freezeChildSession || preserveExistingVisibleState)
              ? (previousDetail.session_summary || deriveSessionSummary(nextDetail, latestFrame) || '')
              : (childVisibleOutput || childSession?.summary || deriveSessionSummary(nextDetail, latestFrame) || previousDetail.session_summary || ''));
          nextState.details.set(detailKey, nextDetail);
        }
      }

      const mergedCoordinator = staleCoordinatorRevision
        ? { ...previous }
        : (previous ? { ...previous, ...next } : { ...next });
      delete mergedCoordinator.agents;
      nextState.coordinators.set(next.coordinator_id, mergedCoordinator);
      {
        const selectedKey = trimText(nextState.selectedTabKey);
        const selectedDetail = selectedKey ? nextState.details.get(selectedKey) : null;
        const selectedBelongsToCoordinator = trimText(selectedDetail?.coordinator_id) === trimText(next.coordinator_id);
        const selectedFailed = selectedBelongsToCoordinator &&
          (trimText(selectedDetail?.status) === 'failed' || trimText(selectedDetail?.status) === 'error');
        const preferredAgent = next.agents.find((agent) => {
          if (!isTerminalStatus(agent.status) || trimText(agent.status) !== 'done') {
            return false;
          }
          const detailKey = detailKeyForAgent(agent);
          const detail = detailKey ? nextState.details.get(detailKey) : null;
          return !!trimText(deriveVisibleOutputText(detail, detail?.latest_frame));
        });
        const preferredKey = preferredAgent ? detailKeyForAgent(preferredAgent) : '';
        if (selectedFailed && preferredKey) {
          nextState.selectedTabKey = preferredKey;
        }
      }
      if (!nextState.selectedTabKey) {
        const orderedDetails = orderedSubagentDetails(nextState);
        const preferredDetail = orderedDetails[0];
        const preferredKey = trimText(preferredDetail?.key) || detailKeyForAgent(preferredDetail);
        if (preferredKey) {
          nextState.selectedTabKey = preferredKey;
        } else {
          const firstAgent = next.agents[0];
          const firstKey = detailKeyForAgent(firstAgent);
          if (firstKey) {
            nextState.selectedTabKey = firstKey;
          }
        }
      }
    }

    return nextState;
  }

  function hydrateStateFromSnapshot(snapshot, options) {
    const chatId = trimText(options?.chatId);
    const interactiveUiConfig = options?.interactiveUiConfig;
    const coordinators = Array.isArray(snapshot?.coordinators) ? snapshot.coordinators : [];
    const snapshotBlockers = typeof interactiveUiConfig === 'function'
      ? normalizeInteractiveSnapshot(snapshot, chatId, interactiveUiConfig)
      : [];
    const explicitEmptySnapshot = coordinators.length === 0 && snapshotBlockers.length === 0;
    let state = createEmptySubagentUiState();
    if (explicitEmptySnapshot) {
      return state;
    }
    const snapshotVisibleRevision = cursorVisibleRevision(snapshot);
    const snapshotAfterVisibleRevision = cursorAfterVisibleRevision(snapshot);
    state.liveCursor = {
      ...(state.liveCursor || {}),
      visibleRevision: snapshotVisibleRevision,
      afterVisibleRevision: snapshotAfterVisibleRevision,
    };

    for (const coordinator of coordinators) {
      state = reduceSubagentUiEvent(state, { kind: 'coordinator', payload: coordinator }, {
        normalizeCoordinatorPayload: options?.normalizeCoordinatorPayload || core.normalizeCoordinatorPayload,
      });
    }

    if (typeof interactiveUiConfig === 'function') {
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
