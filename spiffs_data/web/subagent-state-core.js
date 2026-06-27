(function initSubagentStateCoreModule(global) {
  const DETAIL_TIMELINE_LIMIT = 80;
  const DETAIL_BLOCKER_LIMIT = 8;
  const DETAIL_COMMIT_LIMIT = 80;
  const DETAIL_HISTORY_LIMIT = 80;

  function trimText(value) {
    return String(value || '').trim();
  }

  function clipText(value, limit) {
    const text = trimText(value);
    if (!text) return '';
    return text.length > limit ? `${text.slice(0, Math.max(0, limit - 3))}...` : text;
  }

  function timestampValue(value) {
    const num = Number(value);
    return Number.isFinite(num) && num > 0 ? num : Date.now();
  }

  function timelineKeyFromFrame(frame) {
    if (!frame) return '';
    if (trimText(frame.id)) return trimText(frame.id);
    if (Number(frame.seq) > 0) return `seq:${Number(frame.seq)}`;
    return [
      trimText(frame.type),
      trimText(frame.status),
      trimText(frame.phase),
      trimText(frame.detail),
      trimText(frame.output_preview),
      trimText(frame.blocker_kind),
    ].join('|');
  }

  function makeTimelineFrame(payload, previous) {
    const type = trimText(payload?.type) || trimText(previous?.type) || 'subagent_progress';
    const status = trimText(payload?.status) || trimText(previous?.status) || '';
    const detail = trimText(payload?.detail) || trimText(previous?.detail) || '';
    const output = trimText(payload?.visible_output) ||
      trimText(payload?.output) ||
      trimText(previous?.visible_output) ||
      trimText(previous?.output) ||
      '';
    const blockerKind = trimText(payload?.blocker_kind) || trimText(previous?.blocker_kind) || '';
    const blockerText = trimText(payload?.blocker_text) || trimText(previous?.blocker_text) || '';
    let phase = 'progress';

    if (type === 'subagent_start') phase = 'start';
    else if (type === 'subagent_done') phase = status === 'failed' || status === 'error' ? 'failed' : 'done';
    else if (type === 'subagent_blocked' || type === 'subagent_request') phase = 'blocked';
    else if (type === 'subagent_step') phase = 'progress';
    else if (type === 'subagent_unblocked') phase = 'resumed';

    const frame = {
      id: trimText(payload?.id) || trimText(previous?.id),
      seq: Number(payload?.seq) || Number(previous?.seq) || 0,
      type,
      status,
      phase,
      task: trimText(payload?.task) || trimText(previous?.task) || '',
      detail,
      output_preview: clipText(output, 220),
      blocker_kind: blockerKind,
      blocker_text: blockerText,
      ts: timestampValue(payload?.ts || payload?.timestamp || previous?.ts),
    };
    frame.key = timelineKeyFromFrame(frame);
    return frame;
  }

  function mergeTimeline(existing, payload, previous) {
    const timeline = Array.isArray(existing) ? existing.slice() : [];
    const frame = makeTimelineFrame(payload, previous);
    const last = timeline[timeline.length - 1];
    if (last && last.key === frame.key) {
      timeline[timeline.length - 1] = { ...last, ...frame };
    } else {
      timeline.push(frame);
      if (timeline.length > DETAIL_TIMELINE_LIMIT) {
        timeline.splice(0, timeline.length - DETAIL_TIMELINE_LIMIT);
      }
    }
    return timeline;
  }

  function deriveLatestFrame(timeline) {
    if (!Array.isArray(timeline) || !timeline.length) return null;
    for (let i = timeline.length - 1; i >= 0; i--) {
      const frame = timeline[i];
      if (!frame) continue;
      if (trimText(frame.output_preview) || trimText(frame.detail) || trimText(frame.blocker_text)) {
        return frame;
      }
    }
    return timeline[timeline.length - 1] || null;
  }

  function isTerminalStatus(status) {
    const text = trimText(status);
    return text === 'done' || text === 'failed' || text === 'error';
  }

  function detailLooksMoreComplete(detail) {
    if (!detail || typeof detail !== 'object') return false;
    if (isTerminalStatus(detail.status)) return true;
    if (trimText(detail.output)) return true;
    if (trimText(detail.session_summary)) return true;
    return false;
  }

  function childSessionLooksBlocked(childSession) {
    if (!childSession || typeof childSession !== 'object') return false;
    const pendingQueue = childSession.pending_queue && typeof childSession.pending_queue === 'object'
      ? childSession.pending_queue
      : null;
    const permissionCount = Array.isArray(pendingQueue?.permissions) ? pendingQueue.permissions.length : 0;
    const questionCount = Array.isArray(pendingQueue?.questions) ? pendingQueue.questions.length : 0;
    if (permissionCount > 0 || questionCount > 0) return true;

    const frames = Array.isArray(childSession.frames) ? childSession.frames : [];
    for (const frame of frames) {
      const phase = trimText(frame?.phase);
      const type = trimText(frame?.type);
      if (phase === 'blocked' || type === 'subagent_blocked' || type === 'subagent_request') {
        return true;
      }
    }

    const commits = Array.isArray(childSession.commits) ? childSession.commits : [];
    for (const commit of commits) {
      const phase = trimText(commit?.phase);
      const kind = trimText(commit?.kind);
      if (phase === 'blocked' || kind === 'blocker' || kind === 'permission' || kind === 'question') {
        return true;
      }
    }

    return false;
  }

  function shouldPreserveNewerDetail(previousDetail, incomingStatus, childSession, previousStatus) {
    if (!detailLooksMoreComplete(previousDetail)) return false;
    if (!isTerminalStatus(previousStatus)) return false;
    if (isTerminalStatus(incomingStatus)) return false;
    return childSessionLooksBlocked(childSession);
  }

  function maxSessionSeq(items) {
    if (!Array.isArray(items) || !items.length) return 0;
    let max = 0;
    for (const item of items) {
      const seq = Number(item?.seq) || 0;
      if (seq > max) max = seq;
    }
    return max;
  }

  function childSessionIsOlderThanDetail(detail, childSession) {
    if (!detail || typeof detail !== 'object' || !childSession || typeof childSession !== 'object') {
      return false;
    }

    const incomingFrameSeq = Math.max(
      maxSessionSeq(childSession.frames),
      Number(childSession.latest_frame?.seq) || 0,
    );
    const incomingCommitSeq = maxSessionSeq(childSession.commits);
    const incomingHistorySeq = maxSessionSeq(childSession.history);

    const currentFrameSeq = Math.max(
      maxSessionSeq(detail.frames),
      Number(detail.latest_frame?.seq) || 0,
    );
    const currentCommitSeq = maxSessionSeq(detail.commits);
    const currentHistorySeq = maxSessionSeq(detail.history);

    const hasIncomingSeq =
      incomingFrameSeq > 0 ||
      incomingCommitSeq > 0 ||
      incomingHistorySeq > 0;
    if (!hasIncomingSeq) {
      return false;
    }

    if (incomingFrameSeq > 0 && currentFrameSeq > 0 && incomingFrameSeq < currentFrameSeq) {
      return true;
    }
    if (incomingCommitSeq > 0 && currentCommitSeq > 0 && incomingCommitSeq < currentCommitSeq) {
      return true;
    }
    if (incomingHistorySeq > 0 && currentHistorySeq > 0 && incomingHistorySeq < currentHistorySeq) {
      return true;
    }
    return false;
  }

  function deriveSessionSummary(detail, latestFrame) {
    const explicitOutput = trimText(detail?.output);
    if (explicitOutput) {
      return clipText(explicitOutput, 280);
    }

    const frameOutput = trimText(latestFrame?.output_preview);
    if (frameOutput) {
      return frameOutput;
    }

    const blockerText = trimText(latestFrame?.blocker_text);
    if (blockerText) {
      return blockerText;
    }

    return trimText(latestFrame?.detail);
  }

  function commitKey(commit) {
    if (!commit) return '';
    if (trimText(commit.id)) return trimText(commit.id);
    if (Number(commit.seq) > 0) return `seq:${Number(commit.seq)}`;
    return [
      trimText(commit.kind),
      trimText(commit.phase),
      trimText(commit.label),
      trimText(commit.text),
      trimText(commit.status),
    ].join('|');
  }

  function makeCommitRecord(payload, previous) {
    const type = trimText(payload?.type) || trimText(previous?.type) || 'subagent_progress';
    const status = trimText(payload?.status) || trimText(previous?.status) || '';
    const detail = trimText(payload?.detail) || '';
    const output = clipText(
      trimText(payload?.visible_output) ||
      trimText(payload?.output),
      220,
    );
    const blockerText = trimText(payload?.blocker_text) || '';
    let kind = 'progress';
    let phase = 'running';

    if (type === 'subagent_start') {
      kind = 'start';
      phase = 'running';
    } else if (type === 'subagent_done') {
      kind = 'result';
      phase = status === 'failed' || status === 'error' ? 'failed' : 'done';
    } else if (type === 'subagent_request') {
      kind = trimText(payload?.blocker_kind) === 'question' ? 'question' : 'permission';
      phase = 'blocked';
    } else if (type === 'subagent_step') {
      kind = trimText(payload?.blocker_kind) || 'progress';
      phase = 'running';
    } else if (type === 'subagent_blocked') {
      kind = 'blocker';
      phase = 'blocked';
    } else if (type === 'subagent_unblocked') {
      kind = 'resume';
      phase = 'resumed';
    }

    const text = output || blockerText || detail;
    if (!text && kind === 'progress') return null;

    const record = {
      id: trimText(payload?.id) || trimText(previous?.id),
      seq: Number(payload?.seq) || Number(previous?.seq) || 0,
      kind,
      phase,
      status,
      label: trimText(payload?.task) || trimText(previous?.task) || trimText(payload?.subagent_type) || 'subagent',
      text: text || (kind === 'start' ? 'subagent started' : ''),
      ts: timestampValue(payload?.ts || payload?.timestamp || previous?.ts),
    };
    record.key = commitKey(record);
    return record;
  }

  function mergeCommits(existing, payload, previous) {
    const commits = Array.isArray(existing) ? existing.slice() : [];
    const next = makeCommitRecord(payload, previous);
    if (!next) return commits;
    const last = commits[commits.length - 1];
    if (last && last.key === next.key) {
      commits[commits.length - 1] = { ...last, ...next };
    } else {
      commits.push(next);
      if (commits.length > DETAIL_COMMIT_LIMIT) {
        commits.splice(0, commits.length - DETAIL_COMMIT_LIMIT);
      }
    }
    return commits;
  }

  function blockerKey(blocker) {
    if (!blocker) return '';
    return [
      trimText(blocker.kind),
      trimText(blocker.scope),
      trimText(blocker.text),
      trimText(blocker.state),
    ].join('|');
  }

  function makeBlockerRecord(payload, previous) {
    const kind = trimText(payload?.blocker_kind) || trimText(previous?.blocker_kind) || '';
    const text = trimText(payload?.blocker_text) || trimText(previous?.blocker_text) || '';
    const scope = trimText(payload?.blocker_scope) || trimText(previous?.blocker_scope) || 'task';
    const type = trimText(payload?.type) || '';
    let state = '';

    if (type === 'subagent_blocked' || kind || text) state = 'blocked';
    else if (type === 'subagent_unblocked') state = 'resolved';

    if (!state || (!kind && !text)) return null;

    const record = {
      kind: kind || 'blocked',
      text: text || `Blocked at ${scope}`,
      scope,
      state,
      ts: timestampValue(payload?.ts || payload?.timestamp || previous?.ts),
    };
    record.key = blockerKey(record);
    return record;
  }

  function shouldClearCurrentBlocker(payload) {
    return trimText(payload?.type) === 'subagent_unblocked' &&
      !trimText(payload?.blocker_kind) &&
      !trimText(payload?.blocker_text);
  }

  function derivePendingQueue(detail) {
    const queue = {
      permissions: [],
      questions: [],
    };
    const status = trimText(detail?.status);
    const blockerKind = trimText(detail?.blocker_kind);
    const blockerText = trimText(detail?.blocker_text);
    const pendingRequest = detail?.pending_request && typeof detail.pending_request === 'object'
      ? {
          request_type: trimText(detail.pending_request.request_type),
          request_id: trimText(detail.pending_request.request_id),
          prompt: trimText(detail.pending_request.prompt),
        }
      : null;
    if (pendingRequest?.request_type && pendingRequest?.prompt) {
      if (pendingRequest.request_type !== 'question' && pendingRequest.request_type !== 'question_text') {
        queue.permissions.push(pendingRequest);
        return queue;
      }
      if (pendingRequest.request_type === 'question' || pendingRequest.request_type === 'question_text') {
        queue.questions.push(pendingRequest);
        return queue;
      }
    }
    if (status === 'blocked' && blockerKind && blockerText) {
      if (blockerKind === 'permission') {
        queue.permissions.push({
          request_type: 'permission',
          request_id: '',
          prompt: blockerText,
        });
      } else if (blockerKind === 'question') {
        queue.questions.push({
          request_type: 'question',
          request_id: '',
          prompt: blockerText,
        });
      }
    }
    return queue;
  }

  function normalizePendingQueueItems(items, fallbackType) {
    if (!Array.isArray(items)) return [];
    return items.map((item) => {
      if (item && typeof item === 'object') {
        const requestType = trimText(item.request_type) || fallbackType;
        const requestId = trimText(item.request_id);
        const prompt = trimText(item.prompt);
        if (!requestType || !prompt) return null;
        return {
          request_type: requestType,
          request_id: requestId,
          prompt,
        };
      }
      const prompt = trimText(item);
      if (!prompt) return null;
      return {
        request_type: fallbackType,
        request_id: '',
        prompt,
      };
    }).filter(Boolean);
  }

  function normalizeCoordinatorAgent(agent) {
    const taskId = trimText(agent?.task_id);
    const taskKey = trimText(agent?.task_key);
    const dependsOn = trimText(agent?.depends_on);
    const sessionId = trimText(agent?.session_id);
    const subagentType = trimText(agent?.subagent_type);
    const description = trimText(agent?.description || agent?.name || subagentType || 'subagent');
    const status = trimText(agent?.status) || 'waiting';
    const model = trimText(agent?.model);
    const output = trimText(agent?.output || agent?.output_text);
    const targetFiles = trimText(agent?.target_files);
    const scopePath = trimText(agent?.scope_path);
    const scopeKind = trimText(agent?.scope_kind);
    const analysisFocus = trimText(agent?.analysis_focus);
    const elapsedMs = Number(agent?.elapsed_ms) || 0;
    const writeApproved = agent?.write_approved === true;
    const coordinatorStatus = trimText(agent?.coordinator_status);
    const coordinatorWakeState = trimText(agent?.coordinator_wake_state);
    const wakeRetryCount = Number(agent?.wake_retry_count) || 0;
    const wakeLastError = trimText(agent?.wake_last_error);
    const blockerKind = trimText(agent?.blocker_kind);
    const blockerText = trimText(agent?.blocker_text);
    const coordinatorBlockerKind = trimText(agent?.coordinator_blocker_kind);
    const coordinatorBlockerText = trimText(agent?.coordinator_blocker_text);
    const parentResponseSent = agent?.parent_response_sent === true;
    const pendingRequest = agent?.pending_request && typeof agent.pending_request === 'object'
      ? {
          request_type: trimText(agent.pending_request.request_type),
          request_id: trimText(agent.pending_request.request_id),
          prompt: trimText(agent.pending_request.prompt),
        }
      : null;
    const childSession = agent?.child_session && typeof agent.child_session === 'object'
      ? agent.child_session
      : null;
    return {
      task_id: taskId,
      task_key: taskKey,
      depends_on: dependsOn,
      session_id: sessionId,
      subagent_type: subagentType,
      name: description,
      description,
      status,
      model,
      scope_path: scopePath,
      scope_kind: scopeKind,
      analysis_focus: analysisFocus,
      output,
      target_files: targetFiles,
      elapsed_ms: elapsedMs,
      write_approved: writeApproved,
      coordinator_status: coordinatorStatus,
      coordinator_wake_state: coordinatorWakeState,
      wake_retry_count: wakeRetryCount,
      wake_last_error: wakeLastError,
      blocker_kind: blockerKind,
      blocker_text: blockerText,
      coordinator_blocker_kind: coordinatorBlockerKind,
      coordinator_blocker_text: coordinatorBlockerText,
      parent_response_sent: parentResponseSent,
      pending_request: pendingRequest,
      child_session: childSession,
    };
  }

  function normalizeCoordinatorPayload(payload) {
    const coordinator = payload?.coordinator || payload || {};
    const agents = Array.isArray(coordinator.agents)
      ? coordinator.agents.map(normalizeCoordinatorAgent)
      : [];
    return {
      coordinator_id: trimText(coordinator.coordinator_id),
      chat_id: trimText(coordinator.chat_id),
      team_run_id: trimText(coordinator.team_run_id),
      team_name: trimText(coordinator.team_name),
      dispatch_mode: trimText(coordinator.dispatch_mode),
      status: trimText(coordinator.status) || 'running',
      agent_count: Number(coordinator.agent_count) || agents.length,
      completed_count: Number(coordinator.completed_count) ||
        agents.filter((agent) => agent.status === 'done' || agent.status === 'error').length,
      running_count: Number(coordinator.running_count) ||
        agents.filter((agent) => agent.status === 'running').length,
      queued_count: Number(coordinator.queued_count) ||
        agents.filter((agent) => agent.status === 'queued').length,
      blocked_count: Number(coordinator.blocked_count) ||
        agents.filter((agent) => agent.blocker_kind || agent.blocker_text).length,
      failed_count: Number(coordinator.failed_count) ||
        agents.filter((agent) => agent.status === 'error' || agent.status === 'failed').length,
      effective_output_count: Number(coordinator.effective_output_count) || 0,
      visible_revision: Number(coordinator.visible_revision) || 0,
      completion_notified: coordinator.completion_notified === true,
      parent_response_sent: coordinator.parent_response_sent === true,
      wake_state: trimText(coordinator.wake_state) || 'idle',
      wake_retry_count: Number(coordinator.wake_retry_count) || 0,
      wake_last_attempt_ms: Number(coordinator.wake_last_attempt_ms) || 0,
      wake_last_success_ms: Number(coordinator.wake_last_success_ms) || 0,
      wake_last_error: trimText(coordinator.wake_last_error),
      blocker_kind: trimText(coordinator.blocker_kind),
      blocker_text: trimText(coordinator.blocker_text),
      agents,
    };
  }

  function normalizeChildSession(childSession, fallbackDetail) {
    const child = childSession && typeof childSession === 'object' ? childSession : null;
    if (!child) return null;

    const history = Array.isArray(child.history)
      ? child.history
          .map((item, index) => {
            if (!item || typeof item !== 'object') return null;
            const role = trimText(item.role) || 'assistant';
            const content = trimText(item.content);
            const reasoning = trimText(item.reasoning);
            if (!content && !reasoning) return null;
            return {
              id: trimText(item.id),
              seq: Number(item.seq) || 0,
              role,
              content,
              reasoning,
              ts: Number(item.ts) || 0,
              source: trimText(item.source),
              key: trimText(item.id) || [role, content, reasoning, String(index)].join('|'),
            };
          })
          .filter(Boolean)
          .slice(-DETAIL_HISTORY_LIMIT)
      : [];

    const frames = Array.isArray(child.frames)
      ? child.frames
          .map((frame) => {
            if (!frame || typeof frame !== 'object') return null;
            const next = {
              id: trimText(frame.id),
              seq: Number(frame.seq) || 0,
              type: trimText(frame.type) || 'subagent_progress',
              status: trimText(frame.status) || trimText(fallbackDetail?.status),
              phase: trimText(frame.phase) || 'progress',
              task: trimText(frame.task) || trimText(fallbackDetail?.task) || 'subagent',
              detail: trimText(frame.detail),
              output_preview: trimText(frame.output_preview),
              blocker_kind: trimText(frame.blocker_kind),
              blocker_text: trimText(frame.blocker_text),
              ts: timestampValue(frame.ts),
            };
            next.key = timelineKeyFromFrame(next);
            return next;
          })
          .filter(Boolean)
      : [];

    const commits = Array.isArray(child.commits)
      ? child.commits
          .map((commit) => {
            if (!commit || typeof commit !== 'object') return null;
            return {
              id: trimText(commit.id),
              seq: Number(commit.seq) || 0,
              kind: trimText(commit.kind) || 'progress',
              phase: trimText(commit.phase) || 'running',
              status: trimText(commit.status) || trimText(fallbackDetail?.status),
              label: trimText(commit.label) || trimText(fallbackDetail?.task) || 'subagent',
              text: trimText(commit.text),
              ts: timestampValue(commit.ts),
              key: commitKey(commit),
            };
          })
          .filter(Boolean)
      : [];

    const pendingQueue = child.pending_queue && typeof child.pending_queue === 'object'
      ? {
          permissions: normalizePendingQueueItems(child.pending_queue.permissions, 'permission'),
          questions: normalizePendingQueueItems(child.pending_queue.questions, 'question'),
        }
      : { permissions: [], questions: [] };

    const pendingRequest = child.pending_request && typeof child.pending_request === 'object'
      ? {
          request_type: trimText(child.pending_request.request_type),
          request_id: trimText(child.pending_request.request_id),
          prompt: trimText(child.pending_request.prompt),
        }
      : null;

    const latestFrame = child.latest_frame && typeof child.latest_frame === 'object'
      ? {
          id: trimText(child.latest_frame.id),
          seq: Number(child.latest_frame.seq) || 0,
          type: trimText(child.latest_frame.type) || 'subagent_progress',
          status: trimText(child.latest_frame.status) || trimText(fallbackDetail?.status),
          phase: trimText(child.latest_frame.phase) || 'progress',
          task: trimText(child.latest_frame.task) || trimText(fallbackDetail?.task) || 'subagent',
          detail: trimText(child.latest_frame.detail),
          output_preview: trimText(child.latest_frame.output_preview),
          blocker_kind: trimText(child.latest_frame.blocker_kind),
          blocker_text: trimText(child.latest_frame.blocker_text),
          ts: timestampValue(child.latest_frame.ts),
          key: timelineKeyFromFrame(child.latest_frame),
        }
      : null;

    const cursorMeta = child.cursor && typeof child.cursor === 'object'
      ? {
          history: child.cursor.history && typeof child.cursor.history === 'object'
            ? {
                after_seq: Number(child.cursor.history.after_seq) || 0,
                visible_seq: Number(child.cursor.history.visible_seq) || 0,
                first_visible_seq: Number(child.cursor.history.first_visible_seq) || 0,
                replay_reset: child.cursor.history.replay_reset === true,
              }
            : null,
          frames: child.cursor.frames && typeof child.cursor.frames === 'object'
            ? {
                after_seq: Number(child.cursor.frames.after_seq) || 0,
                visible_seq: Number(child.cursor.frames.visible_seq) || 0,
                first_visible_seq: Number(child.cursor.frames.first_visible_seq) || 0,
                replay_reset: child.cursor.frames.replay_reset === true,
              }
            : null,
          commits: child.cursor.commits && typeof child.cursor.commits === 'object'
            ? {
                after_seq: Number(child.cursor.commits.after_seq) || 0,
                visible_seq: Number(child.cursor.commits.visible_seq) || 0,
                first_visible_seq: Number(child.cursor.commits.first_visible_seq) || 0,
                replay_reset: child.cursor.commits.replay_reset === true,
              }
            : null,
        }
      : null;

    const windowMeta = child.window && typeof child.window === 'object'
      ? {
          history_limit: Number(child.window.history_limit) || 0,
          history_count: Number(child.window.history_count) || history.length,
          history_total: Number(child.window.history_total) || history.length,
          history_truncated: child.window.history_truncated === true,
          history_first_seq: Number(child.window.history_first_seq) || 0,
          history_last_seq: Number(child.window.history_last_seq) || 0,
          frame_limit: Number(child.window.frame_limit) || 0,
          frame_count: Number(child.window.frame_count) || frames.length,
          frame_total: Number(child.window.frame_total) || frames.length,
          frame_truncated: child.window.frame_truncated === true,
          frame_first_seq: Number(child.window.frame_first_seq) || 0,
          frame_last_seq: Number(child.window.frame_last_seq) || 0,
          commit_limit: Number(child.window.commit_limit) || 0,
          commit_count: Number(child.window.commit_count) || commits.length,
          commit_total: Number(child.window.commit_total) || commits.length,
          commit_truncated: child.window.commit_truncated === true,
          commit_first_seq: Number(child.window.commit_first_seq) || 0,
          commit_last_seq: Number(child.window.commit_last_seq) || 0,
        }
      : {
          history_limit: 0,
          history_count: history.length,
          history_total: history.length,
          history_truncated: false,
          history_first_seq: history.length ? Number(history[0]?.seq) || 0 : 0,
          history_last_seq: history.length ? Number(history[history.length - 1]?.seq) || 0 : 0,
          frame_limit: 0,
          frame_count: frames.length,
          frame_total: frames.length,
          frame_truncated: false,
          frame_first_seq: frames.length ? Number(frames[0]?.seq) || 0 : 0,
          frame_last_seq: frames.length ? Number(frames[frames.length - 1]?.seq) || 0 : 0,
          commit_limit: 0,
          commit_count: commits.length,
          commit_total: commits.length,
          commit_truncated: false,
          commit_first_seq: commits.length ? Number(commits[0]?.seq) || 0 : 0,
          commit_last_seq: commits.length ? Number(commits[commits.length - 1]?.seq) || 0 : 0,
        };

    return {
      summary: trimText(child.summary),
      status: trimText(child.status),
      history,
      frames,
      commits,
      cursor: cursorMeta,
      window: windowMeta,
      pending_queue: pendingQueue,
      pending_request: pendingRequest && pendingRequest.request_type && pendingRequest.prompt ? pendingRequest : null,
      latest_frame: latestFrame,
    };
  }

  function mergeSessionTimeline(existing, frames) {
    let timeline = Array.isArray(existing) ? existing.slice() : [];
    if (!Array.isArray(frames) || !frames.length) return timeline;
    for (const frame of frames) {
      if (!frame) continue;
      timeline = mergeTimeline(timeline, {
        id: frame.id,
        seq: frame.seq,
        type: frame.type,
        status: frame.status,
        task: frame.task,
        detail: frame.detail,
        output: frame.output_preview,
        blocker_kind: frame.blocker_kind,
        blocker_text: frame.blocker_text,
        ts: frame.ts,
      }, frame);
    }
    timeline.sort((left, right) => {
      const leftSeq = Number(left?.seq) || 0;
      const rightSeq = Number(right?.seq) || 0;
      if (leftSeq > 0 || rightSeq > 0) {
        if (leftSeq !== rightSeq) return leftSeq - rightSeq;
      }
      return timestampValue(left?.ts) - timestampValue(right?.ts);
    });
    return timeline;
  }

  function mergeSessionFrames(existing, incoming) {
    const next = Array.isArray(existing) ? existing.slice() : [];
    const items = Array.isArray(incoming) ? incoming : [];
    if (!items.length) return next;

    for (const frame of items) {
      if (!frame) continue;
      const normalized = {
        id: trimText(frame.id),
        seq: Number(frame.seq) || 0,
        type: trimText(frame.type) || 'subagent_progress',
        status: trimText(frame.status),
        phase: trimText(frame.phase) || 'progress',
        task: trimText(frame.task) || 'subagent',
        detail: trimText(frame.detail),
        output_preview: trimText(frame.output_preview),
        blocker_kind: trimText(frame.blocker_kind),
        blocker_text: trimText(frame.blocker_text),
        ts: timestampValue(frame.ts),
      };
      normalized.key = timelineKeyFromFrame(normalized);
      const normalizedId = normalized.id;
      const normalizedSeq = normalized.seq;
      const existingIndex = next.findIndex((current) => {
        if (!current) return false;
        const currentId = trimText(current.id);
        const currentSeq = Number(current.seq) || 0;
        if (normalizedId && currentId) return normalizedId === currentId;
        if (normalizedSeq > 0 && currentSeq > 0) return normalizedSeq === currentSeq;
        return timelineKeyFromFrame(current) === normalized.key;
      });
      if (existingIndex >= 0) {
        next[existingIndex] = { ...next[existingIndex], ...normalized };
      } else {
        next.push(normalized);
      }
    }

    next.sort((left, right) => {
      const leftSeq = Number(left?.seq) || 0;
      const rightSeq = Number(right?.seq) || 0;
      if (leftSeq > 0 || rightSeq > 0) {
        if (leftSeq !== rightSeq) return leftSeq - rightSeq;
      }
      return timestampValue(left?.ts) - timestampValue(right?.ts);
    });
    return next;
  }

  function mergeSessionCommits(existing, commits) {
    let next = Array.isArray(existing) ? existing.slice() : [];
    if (!Array.isArray(commits) || !commits.length) return next;
    for (const commit of commits) {
      if (!commit) continue;
      next = mergeCommits(next, {
        id: commit.id,
        seq: commit.seq,
        type: commit.kind === 'result'
          ? 'subagent_done'
          : commit.kind === 'question' || commit.kind === 'permission'
            ? 'subagent_request'
          : commit.kind === 'blocker'
            ? 'subagent_blocked'
            : commit.kind === 'resume'
              ? 'subagent_unblocked'
              : commit.kind === 'start'
                ? 'subagent_start'
                : 'subagent_progress',
        status: commit.status,
        task: commit.label,
        detail: commit.text,
        output: commit.kind === 'result' ? commit.text : '',
        blocker_kind: commit.kind === 'question' || commit.kind === 'permission' ? commit.kind : '',
        blocker_text: commit.kind === 'question' || commit.kind === 'permission' ? commit.text : '',
        ts: commit.ts,
      }, commit);
    }
    next.sort((left, right) => {
      const leftSeq = Number(left?.seq) || 0;
      const rightSeq = Number(right?.seq) || 0;
      if (leftSeq > 0 || rightSeq > 0) {
        if (leftSeq !== rightSeq) return leftSeq - rightSeq;
      }
      return timestampValue(left?.ts) - timestampValue(right?.ts);
    });
    return next;
  }

  function historyKey(item) {
    if (!item) return '';
    return [
      trimText(item.id),
      String(Number(item.seq) || 0),
      trimText(item.role),
      trimText(item.content),
      trimText(item.reasoning),
      String(Number(item.ts) || 0),
    ].join('|');
  }

  function mergeSessionHistory(existing, incoming) {
    const next = Array.isArray(existing) ? existing.slice() : [];
    const items = Array.isArray(incoming) ? incoming : [];
    if (!items.length) return next;

    for (const item of items) {
      if (!item) continue;
      const normalized = {
        id: trimText(item.id),
        seq: Number(item.seq) || 0,
        role: trimText(item.role) || 'assistant',
        content: trimText(item.content),
        reasoning: trimText(item.reasoning),
        ts: Number(item.ts) || 0,
        source: trimText(item.source),
      };
      if (!normalized.content && !normalized.reasoning) continue;
      normalized.key = normalized.id || historyKey(normalized);
      if (!normalized.key) continue;
      const exists = next.some((current) => {
        const currentKey = trimText(current?.id) || historyKey(current);
        return currentKey === normalized.key;
      });
      if (exists) continue;
      next.push(normalized);
    }

    if (next.length > DETAIL_HISTORY_LIMIT) {
      next.splice(0, next.length - DETAIL_HISTORY_LIMIT);
    }
    next.sort((left, right) => {
      const leftSeq = Number(left?.seq) || 0;
      const rightSeq = Number(right?.seq) || 0;
      if (leftSeq > 0 || rightSeq > 0) {
        if (leftSeq !== rightSeq) return leftSeq - rightSeq;
      }
      return timestampValue(left?.ts) - timestampValue(right?.ts);
    });
    return next;
  }

  function mergeBlockers(existing, payload, previous) {
    const blockers = Array.isArray(existing) ? existing.slice() : [];
    const next = makeBlockerRecord(payload, previous);
    if (!next) return blockers;
    const last = blockers[blockers.length - 1];
    if (last && last.key === next.key) {
      blockers[blockers.length - 1] = { ...last, ...next };
    } else {
      blockers.push(next);
      if (blockers.length > DETAIL_BLOCKER_LIMIT) {
        blockers.splice(0, blockers.length - DETAIL_BLOCKER_LIMIT);
      }
    }
    return blockers;
  }

  function createEmptySubagentUiState() {
    return {
      coordinators: new Map(),
      details: new Map(),
      eventLog: new Map(),
      interactiveBlockers: new Map(),
      liveCursor: {
        visibleRevision: 0,
        afterVisibleRevision: 0,
      },
      selectedTabKey: '',
    };
  }

  function subagentEventKey(data) {
    const taskId = trimText(data?.task_id);
    if (taskId) return taskId;
    const sessionId = trimText(data?.session_id);
    if (sessionId) return sessionId;
    return trimText(data?.task || data?.subagent_type);
  }

  function detailKeyForAgent(agent) {
    return trimText(agent?.task_id) || trimText(agent?.session_id) || trimText(agent?.name);
  }

  function subagentEventsForAgent(agent, state) {
    const eventLog = state?.eventLog instanceof Map ? state.eventLog : null;
    if (!eventLog) return [];
    const candidates = [
      trimText(agent?.task_id),
      trimText(agent?.session_id),
      trimText(agent?.name),
    ].filter(Boolean);

    for (const key of candidates) {
      const events = eventLog.get(key);
      if (events?.length) return events;
    }
    return [];
  }

  function buildInteractiveBlocker(detail, pendingRequest, chatId, interactiveUiConfig) {
    if (!detail || !pendingRequest || typeof interactiveUiConfig !== 'function') return null;
    const requestType = trimText(pendingRequest.request_type);
    const requestId = trimText(pendingRequest.request_id);
    if (!requestType || !requestId) return null;
    const ui = interactiveUiConfig(requestType) || {};
    return {
      chat_id: trimText(chatId),
      task_id: trimText(detail.task_id),
      session_id: trimText(detail.session_id),
      coordinator_id: trimText(detail.coordinator_id),
      request_type: requestType,
      blocker_kind: trimText(ui.blockerKind),
      label: trimText(detail.task) || trimText(ui.label),
      prompt: trimText(pendingRequest.prompt) || trimText(ui.prompt),
      request_id: requestId,
    };
  }

  function normalizeInteractiveBlockerEntry(entry, interactiveUiConfig) {
    if (!entry || typeof entry !== 'object' || typeof interactiveUiConfig !== 'function') return null;
    const requestType = trimText(entry.request_type);
    const requestId = trimText(entry.request_id);
    if (!requestType || !requestId) return null;
    const ui = interactiveUiConfig(requestType) || {};
    return {
      chat_id: trimText(entry.chat_id),
      task_id: trimText(entry.task_id),
      session_id: trimText(entry.session_id),
      coordinator_id: trimText(entry.coordinator_id),
      request_type: requestType,
      blocker_kind: trimText(entry.blocker_kind) || trimText(ui.blockerKind),
      label: trimText(entry.label) || trimText(ui.label),
      prompt: trimText(entry.prompt) || trimText(ui.prompt),
      request_id: requestId,
    };
  }

  function normalizeInteractiveSnapshot(snapshot, chatId, interactiveUiConfig) {
    const interactive = snapshot?.interactive && typeof snapshot.interactive === 'object'
      ? snapshot.interactive
      : null;
    const blockers = Array.isArray(interactive?.blockers)
      ? interactive.blockers
      : [];
    const normalized = blockers
      .map((entry) => normalizeInteractiveBlockerEntry(entry, interactiveUiConfig))
      .filter(Boolean);

    if (normalized.length > 0) {
      return normalized;
    }

    const pendingRequest = snapshot?.pending_request && typeof snapshot.pending_request === 'object'
      ? snapshot.pending_request
      : null;
    if (!pendingRequest || typeof interactiveUiConfig !== 'function') {
      return [];
    }

    const fallback = normalizeInteractiveBlockerEntry({
      chat_id: trimText(chatId),
      task_id: '',
      session_id: '',
      coordinator_id: '',
      request_type: trimText(pendingRequest.request_type),
      request_id: trimText(pendingRequest.request_id),
      prompt: trimText(pendingRequest.prompt),
    }, interactiveUiConfig);
    return fallback ? [fallback] : [];
  }

  global.AgentSubagentStateCore = {
    DETAIL_TIMELINE_LIMIT,
    DETAIL_BLOCKER_LIMIT,
    DETAIL_COMMIT_LIMIT,
    DETAIL_HISTORY_LIMIT,
    trimText,
    clipText,
    timestampValue,
    timelineKeyFromFrame,
    makeTimelineFrame,
    mergeTimeline,
    deriveLatestFrame,
    isTerminalStatus,
    detailLooksMoreComplete,
    childSessionLooksBlocked,
    shouldPreserveNewerDetail,
    childSessionIsOlderThanDetail,
    deriveSessionSummary,
    commitKey,
    makeCommitRecord,
    mergeCommits,
    blockerKey,
    makeBlockerRecord,
    shouldClearCurrentBlocker,
    derivePendingQueue,
    normalizePendingQueueItems,
    normalizeCoordinatorAgent,
    normalizeCoordinatorPayload,
    normalizeChildSession,
    mergeSessionFrames,
    mergeSessionTimeline,
    mergeSessionCommits,
    historyKey,
    mergeSessionHistory,
    mergeBlockers,
    createEmptySubagentUiState,
    subagentEventKey,
    detailKeyForAgent,
    subagentEventsForAgent,
    buildInteractiveBlocker,
    normalizeInteractiveBlockerEntry,
    normalizeInteractiveSnapshot,
  };
})(window);
