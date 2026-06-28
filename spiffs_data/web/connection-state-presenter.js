(function initConnectionStatePresenterModule(global) {
  function createConnectionStatePresenter(config) {
    const api = config || {};
    let reconnectToastTimer = null;

    function setStatus(online) {
      const nextState = online === true
        ? 'connected'
        : online === false
          ? 'disconnected'
          : (String(online || '').trim() || 'connecting');
      api.setConnectionState?.(nextState);
      api.setIsConnected?.(nextState === 'connected');

      if (nextState === 'connected') {
        if (api.statusEl) {
          api.statusEl.textContent = '已连接';
        }
      } else if (nextState === 'disconnected') {
        if (api.statusEl) {
          const lastError = String(api.getLastConnectionErrorText?.() || '').trim();
          api.statusEl.textContent = lastError
            ? `未连接 · ${lastError}`
            : '未连接';
        }
      } else if (api.statusEl) {
        api.statusEl.textContent = '连接中';
      }

      api.dotEl?.classList.toggle('on', nextState === 'connected');
      api.dotEl?.classList.toggle('connecting', nextState === 'connecting');
      api.syncSendState?.();
    }

    function hideReconnectToast() {
      const toast = api.reconnectToastEl;
      if (!toast || toast.hidden) return;
      toast.classList.add('hiding');
      if (reconnectToastTimer) {
        clearTimeout(reconnectToastTimer);
        reconnectToastTimer = null;
      }
      const onEnd = () => {
        toast.removeEventListener('transitionend', onEnd);
        toast.hidden = true;
        toast.setAttribute('aria-hidden', 'true');
        toast.classList.remove('hiding');
      };
      toast.addEventListener('transitionend', onEnd);
      setTimeout(() => {
        if (toast.hidden) return;
        toast.removeEventListener('transitionend', onEnd);
        onEnd();
      }, 400);
    }

    function showReconnectToast(chatIdToRestore, messageCount) {
      const toast = api.reconnectToastEl;
      const textEl = api.reconnectToastTextEl;
      const actionEl = api.reconnectToastActionEl;
      if (!toast || !textEl) return;

      if (messageCount > 0) {
        textEl.textContent = `检测到之前的会话 (${messageCount} 条消息)`;
        if (actionEl) actionEl.hidden = false;
      } else {
        textEl.textContent = '正在恢复会话连接...';
        if (actionEl) actionEl.hidden = true;
      }

      toast.hidden = false;
      toast.removeAttribute('aria-hidden');
      toast.classList.remove('hiding');

      if (reconnectToastTimer) clearTimeout(reconnectToastTimer);
      reconnectToastTimer = setTimeout(() => hideReconnectToast(), 6000);
    }

    return {
      hideReconnectToast,
      setStatus,
      showReconnectToast,
    };
  }

  global.AgentConnectionStatePresenter = {
    createConnectionStatePresenter,
  };
})(window);
