(function initSelfTestReportViewModule(global) {
  function renderSelfTestReport(data, deps) {
    const api = deps || {};
    const messagesEl = api.messagesEl;
    if (!messagesEl || !data) {
      return;
    }

    const passed = Number(data.passed) || 0;
    const total = Number(data.total) || 0;
    const pct = total > 0 ? (passed * 100 / total) : 0;
    const color = pct === 100 ? '#22c55e' : pct >= 80 ? '#f59e0b' : '#ef4444';
    const probe = data && typeof data.log_probe === 'object' ? data.log_probe : null;

    let itemsHtml = '';
    (data.items || []).forEach(function(item) {
      const icon = item.ok ? '✅' : '❌';
      const cls = item.ok ? 'pass' : 'fail';
      itemsHtml += '<div class="st-item ' + cls + '"><span class="st-icon">' + icon + '</span><span class="st-name">' + item.name + '</span></div>';
    });

    let probeHtml = '';
    if (probe) {
      let verdict = '日志证据不足';
      let detail = '';
      if (probe.pending) {
        verdict = '已安排本轮日志分析，等待 agent 完成 opencode 分析后回看日志';
        detail = '当前只完成了静态自检，runtime log probe 会在后续 follow-up 回合补回。';
      } else if (probe.multi_subagent_confirmed) {
        verdict = '已确认多 subagent 调度';
        detail = '本次 marker 之后同时看到了 attach_task / launch candidate / restore queued 证据链。';
      } else if (!probe.marker_found) {
        verdict = '未命中本次自检日志 marker';
        detail = '说明 follow-up 没有在预期 marker 之后回看 runtime log，当前结论只来自静态自检。';
      } else {
        detail = '已命中本次 marker，但 attach_task / launch candidate / restore queued 证据还不完整，暂时不能确认真实多 subagent 调度。';
      }
      probeHtml =
        '<div class="st-items">' +
        '<div class="st-item ' + ((probe.pending || probe.multi_subagent_confirmed) ? 'pass' : 'fail') + '">' +
        '<span class="st-icon">' + (probe.pending ? '⏳' : (probe.multi_subagent_confirmed ? '🧭' : '⚠️')) + '</span>' +
        '<span class="st-name">' + verdict + '</span>' +
        '</div>' +
        '<div class="st-item">' +
        '<span class="st-icon">•</span>' +
        '<span class="st-name">' + detail + '</span>' +
        '</div>' +
        '<div class="st-item">' +
        '<span class="st-icon">•</span>' +
        '<span class="st-name">marker: ' + (probe.marker_found ? 'hit' : 'miss') + '，attach_task: ' + (Number(probe.attach_task_hits) || 0) + '，launch candidate: ' + (Number(probe.launch_candidate_hits) || 0) + '，restore queued: ' + (Number(probe.restore_queued_hits) || 0) + '</span>' +
        '</div>' +
        '</div>';
    }

    const html =
      '<div class="self-test-report">' +
      '<div class="st-header">' +
      '<span class="st-title">🔍 自检报告</span>' +
      '<span class="st-score" style="color:' + color + '">' + passed + '/' + total + ' 通过</span>' +
      '</div>' +
      '<div class="st-bar"><div class="st-bar-fill" style="width:' + pct + '%;background:' + color + '"></div></div>' +
      probeHtml +
      '<div class="st-items">' + itemsHtml + '</div>' +
      '</div>';

    const div = document.createElement('div');
    div.className = 'message system';
    div.innerHTML = html;
    messagesEl.appendChild(div);
    messagesEl.scrollTop = messagesEl.scrollHeight;
  }

  global.AgentSelfTestReportView = {
    renderSelfTestReport,
  };
})(window);
