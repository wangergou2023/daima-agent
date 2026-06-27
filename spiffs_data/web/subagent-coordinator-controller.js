(function initSubagentCoordinatorControllerModule(global) {
  function createCoordinatorPanelController(config) {
    const syncDockState = typeof config?.syncDockState === 'function' ? config.syncDockState : null;
    let visible = false;
    let dismissed = false;

    function state() {
      return {
        visible,
        dismissed,
      };
    }

    function reset() {
      dismissed = false;
      visible = false;
      syncDockState?.(state());
    }

    function markActive() {
      dismissed = false;
      syncDockState?.(state());
    }

    function render(nextVisible) {
      visible = nextVisible === true;
      syncDockState?.(state());
      return state();
    }

    function open() {
      dismissed = false;
      visible = true;
      syncDockState?.(state());
      return state();
    }

    function hide() {
      visible = false;
      syncDockState?.(state());
      return state();
    }

    function close() {
      dismissed = true;
      return hide();
    }

    function toggle() {
      if (visible) {
        return close();
      }
      return open();
    }

    return {
      state,
      reset,
      markActive,
      render,
      open,
      hide,
      close,
      toggle,
    };
  }

  global.AgentSubagentCoordinatorController = {
    createCoordinatorPanelController,
  };
})(window);
