(function initAgentStatePresenterModule(global) {
  function createAgentStatePresenter(config) {
    const api = config || {};

    function updateAgentIntent(intent) {
      const tag = api.agentIntentTag;
      if (!tag || !api.agentStateRow) return;
      const config = api.intentConfig && intent ? api.intentConfig[intent] : null;
      if (!config) {
        tag.hidden = true;
        tag.setAttribute('aria-hidden', 'true');
        return;
      }
      tag.className = 'agent-tag ' + config.cssClass;
      tag.querySelector('.agent-tag-icon').textContent = config.icon;
      tag.querySelector('.agent-tag-label').textContent = config.label;
      tag.hidden = false;
      tag.removeAttribute('aria-hidden');
    }

    function updateAgentRole(role) {
      const tag = api.agentRoleTag;
      if (!tag || !api.agentStateRow) return;
      const label = api.roleLabels ? api.roleLabels[String(role).toLowerCase()] : '';
      if (!label) {
        tag.hidden = true;
        tag.setAttribute('aria-hidden', 'true');
        return;
      }
      tag.className = 'agent-tag role-tag role-active';
      tag.querySelector('.agent-tag-label').textContent = label;
      tag.hidden = false;
      tag.removeAttribute('aria-hidden');
    }

    function clearAgentState() {
      api.setCurrentAgentRole?.('');
      api.setCurrentAgentModel?.('');
      if (api.agentIntentTag) {
        api.agentIntentTag.hidden = true;
        api.agentIntentTag.setAttribute('aria-hidden', 'true');
      }
      if (api.agentRoleTag) {
        api.agentRoleTag.hidden = true;
        api.agentRoleTag.setAttribute('aria-hidden', 'true');
      }
      api.resetSubagentState?.();
    }

    function handleAgentStateMessage(data) {
      if (!data) return;
      if (data.intent !== undefined) {
        updateAgentIntent(data.intent);
      }
      if (data.role !== undefined) {
        api.setCurrentAgentRole?.(data.role || '');
        updateAgentRole(data.role);
      }
      if (data.model !== undefined) {
        api.setCurrentAgentModel?.(data.model || '');
      }
    }

    return {
      clearAgentState,
      handleAgentStateMessage,
      updateAgentIntent,
      updateAgentRole,
    };
  }

  global.AgentStatePresenter = {
    createAgentStatePresenter,
  };
})(window);
