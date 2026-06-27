(function initSubagentInteractiveControllerModule(global) {
  function createInteractiveController(config) {
    const api = config || {};
    let activeRequest = null;

    function openPromptUi(promptState) {
      if (!promptState) return;
      const ui = promptState.ui || {};
      if (api.titleEl) api.titleEl.textContent = ui.title || '';
      if (api.promptEl) api.promptEl.textContent = promptState.prompt || ui.prompt || '';
      if (api.inputEl) {
        api.inputEl.type = ui.inputType || 'text';
        api.inputEl.placeholder = ui.placeholder || '';
        api.inputEl.value = '';
      }
      if (api.submitEl) api.submitEl.textContent = ui.submitText || '';
      if (api.modalEl) api.modalEl.classList.add('show');
      if (api.focusInput) {
        api.focusInput();
      }
    }

    function clearPromptUi() {
      if (api.modalEl) api.modalEl.classList.remove('show');
      if (api.inputEl) api.inputEl.value = '';
    }

    function apply(controllerState) {
      if (!controllerState) return;
      activeRequest = controllerState.request || null;
      openPromptUi(controllerState.prompt || controllerState.request || null);
    }

    function clear() {
      activeRequest = null;
      clearPromptUi();
    }

    function currentRequest() {
      return activeRequest;
    }

    function currentValue() {
      return api.inputEl ? String(api.inputEl.value || '') : '';
    }

    function buildReplyPayload(makePayload, chatId, cancelled) {
      if (typeof makePayload !== 'function' || !activeRequest?.request_id) {
        return null;
      }
      return makePayload(activeRequest, {
        chatId,
        value: currentValue(),
        cancelled: cancelled === true,
      });
    }

    return {
      openPromptUi,
      clearPromptUi,
      apply,
      clear,
      currentRequest,
      currentValue,
      buildReplyPayload,
    };
  }

  global.AgentSubagentInteractiveController = {
    createInteractiveController,
  };
})(window);
