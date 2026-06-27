(function initSubagentChatTransportModule(global) {
  function createSubagentChatTransport(config) {
    const api = config || {};

    function handleSessionSync(data) {
      const chatId = api.getChatId?.();
      if (data?.chat_id !== chatId || !Array.isArray(data.messages)) {
        api.saveReconnectSession?.();
        return;
      }

      const newMessages = data.messages.filter(
        (message) => message?.role === 'assistant' || message?.role === 'user'
      );
      newMessages.forEach((message) => {
        api.appendHistoryMessage?.(message);
      });

      if (data.last_seq !== undefined) {
        api.setLastMessageSeq?.(data.last_seq);
      }
      api.hideReconnectToast?.();
      api.setAssistantIdle?.();
      api.syncSendState?.();
      api.loadSubagentStateSnapshot?.(chatId);
      api.refreshContextStats?.();
      api.saveReconnectSession?.();
    }

    function handleUploadDone(data) {
      const chatId = api.getChatId?.();
      if (data?.chat_id !== chatId) {
        return;
      }
      api.applyUploadedImage?.(data);
      api.syncSendState?.();
    }

    function handleUploadError() {
      api.clearPendingImage?.();
    }

    function handleStopped(data) {
      const chatId = api.getChatId?.();
      if (data?.chat_id !== chatId || api.isStopRequested?.() !== true) {
        return;
      }
      api.clearPendingReasoningCard?.();
      api.setAssistantIdle?.();
      api.onSocketOpenForPet?.();
      api.syncSendState?.();
      api.saveReconnectSession?.();
    }

    function handleToolMessage(data) {
      api.onToolActivity?.();
      api.addToolMessage?.(data?.content);
      api.clearPendingReasoningCard?.();
    }

    function handlePetResponse(data) {
      api.onPetResponse?.(data);
    }

    function handleReasoningMessage(data) {
      api.onToolActivity?.();
      api.resetCurrentToolGroup?.();
      api.setPendingReasoningCardFromContent?.(data?.content);
      api.syncSendState?.();
    }

    function handleAssistantResponse(data) {
      const assistantText = api.consumeAssistantText?.(data?.content) ?? data?.content;
      api.setAssistantIdle?.();
      if (api.commitPendingReasoningCard?.(assistantText) !== true) {
        api.resetCurrentToolGroup?.();
        api.appendAssistantMessage?.(assistantText);
      }
      api.syncSendState?.();
      api.refreshContextStats?.();
      api.saveReconnectSession?.();
    }

    function createHandlers() {
      return {
        handleAgentStateMessage: api.handleAgentStateMessage,
        handleSessionSync,
        handleUploadDone,
        handleUploadError,
        handleSelfTestResult: api.handleSelfTestResult,
        handleStopped,
        handleToolMessage,
        handlePetResponse,
        handleReasoningMessage,
        handleAssistantResponse,
      };
    }

    function createLifecycleActions() {
      return {
        onOpen(socket) {
          const chatId = api.getChatId?.();
          api.setStatus?.(true);
          api.clearAgentState?.();
          api.onSocketOpenForPet?.();
          const lastSeq = Number(api.getLastMessageSeq?.()) || 0;
          if (lastSeq > 0 && socket?.send) {
            socket.send(JSON.stringify({
              type: 'session_sync',
              chat_id: chatId,
              last_seq: lastSeq,
            }));
            api.showReconnectToast?.(chatId, api.getMessageCount?.() || 0);
          }
          api.saveReconnectSession?.();
          api.loadSubagentStateSnapshot?.(chatId);
          api.refreshContextStats?.();
          api.startPingLoop?.();
          api.startStatsLoop?.();
        },
        onClose() {
          api.setStatus?.(false);
          api.clearAgentState?.();
          api.saveReconnectSession?.();
          api.clearRuntimeTimers?.();
          api.scheduleReconnect?.();
        },
        onError() {
          api.setStatus?.(false);
        },
        onMessage(evt) {
          try {
            const raw = JSON.parse(evt.data);
            if (api.handleParsedMessage?.(raw) === true) {
              return;
            }
          } catch (_) {
            api.handlePlaintextMessage?.(evt?.data);
          }
        },
      };
    }

    return {
      createHandlers,
      createLifecycleActions,
    };
  }

  global.AgentSubagentChatTransport = {
    createSubagentChatTransport,
  };
})(window);
