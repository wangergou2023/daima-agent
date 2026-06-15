(function (global) {
  const DEFAULT_PACKAGE_ID = 'guga.codex-pet';
  const DEFAULT_FRAME_WIDTH = 192;
  const DEFAULT_FRAME_HEIGHT = 208;
  const DEFAULT_SPRITE_COLUMNS = 8;
  const DEFAULT_FRAME_DURATION_MS = 160;
  const DEFAULT_IDLE_DELAY_MS = 4000;
  const DEFAULT_BUBBLE_HIDE_MS = 2800;
  const DEFAULT_CLICK_RUN_DISTANCE_PX = 72;
  const DEFAULT_CLICK_RUN_HALF_MS = 780;
  const DEFAULT_TAP_ACTION_COOLDOWN_MS = 1600;
  const PET_CHAT_PREFIX = 'pet_';
  const DEFAULT_STATE_CONFIG = Object.freeze({
    idle: { row: 0, fallbackFrames: 6 },
    runRight: { row: 1, fallbackFrames: 8 },
    runLeft: { row: 2, fallbackFrames: 8 },
    waving: { row: 3, fallbackFrames: 4 },
    jumping: { row: 4, fallbackFrames: 5 },
    failed: { row: 5, fallbackFrames: 8 },
    waiting: { row: 6, fallbackFrames: 6 },
    running: { row: 7, fallbackFrames: 6 },
    review: { row: 8, fallbackFrames: 6 },
  });

  function buildPetChatId(chatId) {
    let base = chatId || `web_${Math.random().toString(36).slice(2, 8)}`;
    while (base.startsWith(PET_CHAT_PREFIX) && base.length > PET_CHAT_PREFIX.length) {
      base = base.slice(PET_CHAT_PREFIX.length);
    }
    return `${PET_CHAT_PREFIX}${base}`;
  }

  function createPetController(options = {}) {
    const elements = options.elements || {};
    const petDock = elements.dock || null;
    const petButton = elements.button || null;
    const petRunner = elements.runner || null;
    const petSprite = elements.sprite || null;
    const petBubble = elements.bubble || null;

    const packageId = options.packageId || DEFAULT_PACKAGE_ID;
    const petId = packageId.replace(/\.codex-pet$/, '');
    const chatId = options.chatId || `web_${Math.random().toString(36).slice(2, 8)}`;
    const petChatId = buildPetChatId(options.petChatId || chatId);
    const sendJson = typeof options.sendJson === 'function' ? options.sendJson : null;

    const frameWidth = options.frameWidth || DEFAULT_FRAME_WIDTH;
    const frameHeight = options.frameHeight || DEFAULT_FRAME_HEIGHT;
    const spriteColumns = options.spriteColumns || DEFAULT_SPRITE_COLUMNS;
    const frameDurationMs = options.frameDurationMs || DEFAULT_FRAME_DURATION_MS;
    const idleDelayMs = options.idleDelayMs || DEFAULT_IDLE_DELAY_MS;
    const bubbleHideMs = options.bubbleHideMs || DEFAULT_BUBBLE_HIDE_MS;
    const clickRunDistancePx = options.clickRunDistancePx || DEFAULT_CLICK_RUN_DISTANCE_PX;
    const clickRunHalfMs = options.clickRunHalfMs || DEFAULT_CLICK_RUN_HALF_MS;
    const tapActionCooldownMs = options.tapActionCooldownMs || DEFAULT_TAP_ACTION_COOLDOWN_MS;
    const stateConfig = options.stateConfig || DEFAULT_STATE_CONFIG;

    let petAnimationTimer = null;
    let petFrame = 0;
    let petState = 'idle';
    let petReturnTimer = null;
    let petIdleTimer = null;
    let petInteractionActive = false;
    let petInteractionTimer = null;
    let petInteractionAnimation = null;
    let queuedPetState = '';
    let petDockOffsetX = 0;
    let petDockOffsetY = 0;
    let petDragPointerId = null;
    let petDragStartX = 0;
    let petDragStartY = 0;
    let petDragStartOffsetX = 0;
    let petDragStartOffsetY = 0;
    let petDragStartLeft = 0;
    let petDragStartTop = 0;
    let petDragWidth = 0;
    let petDragHeight = 0;
    let petDragging = false;
    let petDragActionSent = false;
    let petBubbleTimer = null;
    let petTapCooldownTimer = null;
    let petTapSuppressedCount = 0;
    let lastPetTapSentAt = 0;
    let pendingAssistantResponse = false;
    let initialized = false;
    let destroyed = false;
    let petFrameCounts = Object.fromEntries(
      Object.entries(stateConfig).map(([name, config]) => [name, config.fallbackFrames]),
    );

    function renderPetFrame() {
      if (!petSprite) return;
      const row = stateConfig[petState]?.row ?? stateConfig.running.row;
      petSprite.style.backgroundPosition = `-${petFrame * frameWidth}px -${row * frameHeight}px`;
    }

    function applyPetDockOffset() {
      if (!petDock) return;
      petDock.style.transform = `translate(${petDockOffsetX}px, ${petDockOffsetY}px)`;
    }

    function playPetState(nextState) {
      if (!(nextState in stateConfig)) return;
      petState = nextState;
      petFrame = 0;
      renderPetFrame();
    }

    function resolvePetFallbackState() {
      return pendingAssistantResponse ? 'running' : 'idle';
    }

    function setPetState(nextState) {
      if (!(nextState in stateConfig)) return;
      if (petInteractionActive) {
        queuedPetState = nextState;
        return;
      }
      playPetState(nextState);
    }

    function clearPetReturnTimer() {
      if (!petReturnTimer) return;
      clearTimeout(petReturnTimer);
      petReturnTimer = null;
    }

    function clearPetIdleTimer() {
      if (!petIdleTimer) return;
      clearTimeout(petIdleTimer);
      petIdleTimer = null;
    }

    function clearPetBubbleTimer() {
      if (!petBubbleTimer) return;
      clearTimeout(petBubbleTimer);
      petBubbleTimer = null;
    }

    function clearPetTapCooldownTimer() {
      if (!petTapCooldownTimer) return;
      clearTimeout(petTapCooldownTimer);
      petTapCooldownTimer = null;
    }

    function hidePetBubble() {
      clearPetBubbleTimer();
      if (!petBubble) return;
      petBubble.hidden = true;
      petBubble.textContent = '';
      petBubble.setAttribute('aria-hidden', 'true');
    }

    function showPetBubble(text, durationMs = bubbleHideMs) {
      if (!petBubble) return;
      const message = String(text ?? '').trim();
      if (!message) {
        hidePetBubble();
        return;
      }
      clearPetBubbleTimer();
      petBubble.textContent = message;
      petBubble.hidden = false;
      petBubble.setAttribute('aria-hidden', 'false');
      petBubbleTimer = setTimeout(() => {
        if (destroyed) return;
        petBubbleTimer = null;
        hidePetBubble();
      }, Math.max(1200, durationMs));
    }

    function schedulePetIdle(delayMs = idleDelayMs) {
      clearPetIdleTimer();
      if (pendingAssistantResponse) return;
      petIdleTimer = setTimeout(() => {
        if (destroyed) return;
        petIdleTimer = null;
        if (!pendingAssistantResponse) {
          setPetState('idle');
        }
      }, delayMs);
    }

    function extractPetDirective(text) {
      const source = String(text ?? '');
      const regex = /\[\[pet:(?:state=)?([a-zA-Z]+)\]\]/g;
      let match;
      let state = '';
      while ((match = regex.exec(source)) !== null) {
        state = match[1] || '';
      }
      return {
        text: source.replace(regex, '').trimEnd(),
        state,
      };
    }

    function applyAssistantPetState(nextState) {
      clearPetReturnTimer();
      clearPetIdleTimer();

      if (!nextState || !(nextState in stateConfig)) {
        setPetState('idle');
        schedulePetIdle();
        return;
      }

      setPetState(nextState);
      if (nextState === 'waving' || nextState === 'jumping' || nextState === 'review') {
        const frameCount = petFrameCounts[nextState] || stateConfig[nextState]?.fallbackFrames || 1;
        petReturnTimer = setTimeout(() => {
          if (destroyed) return;
          petReturnTimer = null;
          schedulePetIdle();
        }, Math.max(700, frameCount * frameDurationMs + frameDurationMs));
        return;
      }
      schedulePetIdle();
    }

    function sendPetAction(action) {
      if (!action || !sendJson) return;
      sendJson({
        type: 'pet_action',
        action,
        chat_id: chatId,
        pet_chat_id: petChatId,
        pet_id: petId,
      });
    }

    function sendThrottledPetTapAction() {
      const now = Date.now();
      const elapsedMs = now - lastPetTapSentAt;
      const remainingMs = tapActionCooldownMs - elapsedMs;

      if (remainingMs <= 0) {
        clearPetTapCooldownTimer();
        petTapSuppressedCount = 0;
        lastPetTapSentAt = now;
        sendPetAction('tap');
        return;
      }

      petTapSuppressedCount += 1;
      if (petTapCooldownTimer) return;

      petTapCooldownTimer = setTimeout(() => {
        if (destroyed) return;
        petTapCooldownTimer = null;
        if (petTapSuppressedCount <= 0) return;
        petTapSuppressedCount = 0;
        lastPetTapSentAt = Date.now();
        sendPetAction('tap');
      }, remainingMs);
    }

    function clearPetInteraction() {
      if (petInteractionTimer) {
        clearTimeout(petInteractionTimer);
        petInteractionTimer = null;
      }
      if (petInteractionAnimation) {
        petInteractionAnimation.cancel();
        petInteractionAnimation = null;
      }
      if (petRunner) {
        petRunner.style.transform = 'translateX(0px)';
      }
      petInteractionActive = false;
    }

    function finishPetInteraction() {
      clearPetInteraction();
      const nextState = queuedPetState || resolvePetFallbackState();
      queuedPetState = '';
      playPetState(nextState);
      if (!pendingAssistantResponse && nextState !== 'running') {
        schedulePetIdle();
      }
    }

    function startPetInteraction() {
      if (!petRunner || !petSprite || petDragging || petInteractionActive) return;

      clearPetReturnTimer();
      clearPetIdleTimer();
      queuedPetState = '';
      clearPetInteraction();

      petInteractionActive = true;
      playPetState('runLeft');

      petInteractionTimer = setTimeout(() => {
        if (destroyed) return;
        if (petInteractionActive) {
          playPetState('runRight');
        }
      }, clickRunHalfMs);

      if (typeof petRunner.animate === 'function') {
        petInteractionAnimation = petRunner.animate(
          [
            { transform: 'translateX(0px)' },
            { transform: `translateX(-${clickRunDistancePx}px)`, offset: 0.5 },
            { transform: 'translateX(0px)', offset: 1 },
          ],
          {
            duration: clickRunHalfMs * 2,
            easing: 'ease-in-out',
            fill: 'forwards',
          },
        );
        petInteractionAnimation.onfinish = () => finishPetInteraction();
        petInteractionAnimation.oncancel = () => {};
        return;
      }

      petRunner.style.transition = `transform ${clickRunHalfMs}ms ease-in-out`;
      petRunner.style.transform = `translateX(-${clickRunDistancePx}px)`;
      petInteractionTimer = setTimeout(() => {
        if (destroyed) return;
        if (!petInteractionActive) return;
        playPetState('runRight');
        petRunner.style.transform = 'translateX(0px)';
        petInteractionTimer = setTimeout(() => {
          if (destroyed) return;
          finishPetInteraction();
        }, clickRunHalfMs);
      }, clickRunHalfMs);
    }

    function beginPetDrag(event) {
      if (!petButton || !petDock) return;
      petDragPointerId = event.pointerId;
      petDragStartX = event.clientX;
      petDragStartY = event.clientY;
      petDragStartOffsetX = petDockOffsetX;
      petDragStartOffsetY = petDockOffsetY;
      const rect = petDock.getBoundingClientRect();
      petDragStartLeft = rect.left;
      petDragStartTop = rect.top;
      petDragWidth = rect.width;
      petDragHeight = rect.height;
      petDragging = false;
      petDragActionSent = false;
      petButton.setPointerCapture(event.pointerId);
    }

    function updatePetDrag(event) {
      if (petDragPointerId !== event.pointerId || !petDock) return;

      const dx = event.clientX - petDragStartX;
      const dy = event.clientY - petDragStartY;
      if (!petDragging && Math.hypot(dx, dy) < 8) {
        return;
      }

      petDragging = true;
      petDock.classList.add('dragging');
      if (!petDragActionSent) {
        petDragActionSent = true;
        sendPetAction('drag');
      }

      const minLeft = 8;
      const minTop = 8;
      const maxLeft = Math.max(minLeft, window.innerWidth - petDragWidth - 8);
      const maxTop = Math.max(minTop, window.innerHeight - petDragHeight - 8);
      const nextLeft = Math.min(maxLeft, Math.max(minLeft, petDragStartLeft + dx));
      const nextTop = Math.min(maxTop, Math.max(minTop, petDragStartTop + dy));

      petDockOffsetX = petDragStartOffsetX + (nextLeft - petDragStartLeft);
      petDockOffsetY = petDragStartOffsetY + (nextTop - petDragStartTop);
      applyPetDockOffset();
    }

    function endPetDrag(event) {
      if (petDragPointerId !== event.pointerId || !petButton || !petDock) return;

      try {
        petButton.releasePointerCapture(event.pointerId);
      } catch (_) {}

      const wasDragging = petDragging;
      petDragPointerId = null;
      petDragging = false;
      petDock.classList.remove('dragging');

      if (!wasDragging) {
        startPetInteraction();
        sendThrottledPetTapAction();
      } else {
        sendPetAction('drop');
      }
      petDragActionSent = false;
    }

    function cancelPetDrag() {
      if (!petButton || !petDock) return;
      const wasDragging = petDragging;
      petDragPointerId = null;
      petDragging = false;
      petDock.classList.remove('dragging');
      if (wasDragging) {
        sendPetAction('drop');
      }
      petDragActionSent = false;
    }

    function countOpaqueFrames(img, row) {
      const canvas = document.createElement('canvas');
      canvas.width = frameWidth;
      canvas.height = frameHeight;
      const ctx = canvas.getContext('2d', { willReadFrequently: true });
      if (!ctx) {
        return 0;
      }

      let count = 0;
      for (let col = 0; col < spriteColumns; col += 1) {
        ctx.clearRect(0, 0, frameWidth, frameHeight);
        ctx.drawImage(
          img,
          col * frameWidth,
          row * frameHeight,
          frameWidth,
          frameHeight,
          0,
          0,
          frameWidth,
          frameHeight,
        );
        const alpha = ctx.getImageData(0, 0, frameWidth, frameHeight).data;
        let hasVisiblePixel = false;
        for (let i = 3; i < alpha.length; i += 4) {
          if (alpha[i] !== 0) {
            hasVisiblePixel = true;
            break;
          }
        }
        if (!hasVisiblePixel) break;
        count += 1;
      }
      return count;
    }

    function detectPetFrameCounts(img) {
      const nextCounts = {};
      for (const [name, config] of Object.entries(stateConfig)) {
        nextCounts[name] = countOpaqueFrames(img, config.row) || config.fallbackFrames;
      }
      petFrameCounts = nextCounts;
    }

    function startPetAnimation() {
      if (!petSprite || petAnimationTimer) return;
      petAnimationTimer = setInterval(() => {
        if (destroyed) return;
        const frameCount = petFrameCounts[petState] || stateConfig[petState]?.fallbackFrames || 1;
        petFrame = (petFrame + 1) % Math.max(1, frameCount);
        renderPetFrame();
      }, frameDurationMs);
    }

    function stopPetAnimation() {
      if (!petAnimationTimer) return;
      clearInterval(petAnimationTimer);
      petAnimationTimer = null;
    }

    function loadPetSpritesheet(src) {
      return new Promise((resolve, reject) => {
        const img = new Image();
        img.onload = () => resolve(img);
        img.onerror = reject;
        img.src = src;
      });
    }

    async function initPetPreview() {
      if (!petDock || !petSprite) return;
      try {
        const resp = await fetch(`/pets/${packageId}/pet.json`, { cache: 'no-store' });
        if (!resp.ok) return;
        const pet = await resp.json();
        if (!pet || !pet.spritesheetPath) return;
        const spritesheetUrl = `/pets/${packageId}/${pet.spritesheetPath}`;
        const spritesheet = await loadPetSpritesheet(spritesheetUrl);
        if (destroyed) return;
        detectPetFrameCounts(spritesheet);
        petSprite.style.backgroundImage = `url("${spritesheetUrl}")`;
        petSprite.setAttribute('aria-label', pet.displayName || 'Agent pet');
        if (pet.displayName) {
          petDock.setAttribute('title', pet.displayName);
        }
        setPetState(resolvePetFallbackState());
        if (!pendingAssistantResponse) {
          schedulePetIdle();
        }
        startPetAnimation();
        petDock.hidden = false;
        petDock.setAttribute('aria-hidden', 'false');
      } catch (_) {}
    }

    function init() {
      if (initialized || destroyed) return;
      initialized = true;
      if (petButton) {
        petButton.addEventListener('pointerdown', beginPetDrag);
        petButton.addEventListener('pointermove', updatePetDrag);
        petButton.addEventListener('pointerup', endPetDrag);
        petButton.addEventListener('pointercancel', cancelPetDrag);
      }
      initPetPreview();
    }

    function destroy() {
      destroyed = true;
      initialized = false;
      clearPetReturnTimer();
      clearPetIdleTimer();
      clearPetBubbleTimer();
      clearPetTapCooldownTimer();
      clearPetInteraction();
      stopPetAnimation();
      hidePetBubble();
      petTapSuppressedCount = 0;
      lastPetTapSentAt = 0;
      cancelPetDrag();
      if (petButton) {
        petButton.removeEventListener('pointerdown', beginPetDrag);
        petButton.removeEventListener('pointermove', updatePetDrag);
        petButton.removeEventListener('pointerup', endPetDrag);
        petButton.removeEventListener('pointercancel', cancelPetDrag);
      }
      if (petRunner) {
        petRunner.style.transform = 'translateX(0px)';
        petRunner.style.transition = '';
      }
      if (petSprite) {
        petSprite.style.backgroundImage = '';
        petSprite.style.backgroundPosition = '0px 0px';
        petSprite.setAttribute('aria-label', 'Agent pet');
      }
      if (petDock) {
        petDock.hidden = true;
        petDock.setAttribute('aria-hidden', 'true');
        petDock.classList.remove('dragging');
        petDock.removeAttribute('title');
      }
    }

    function handleSocketOpen() {
      if (!pendingAssistantResponse) {
        schedulePetIdle();
      }
    }

    function markAssistantPending() {
      pendingAssistantResponse = true;
      hidePetBubble();
      clearPetReturnTimer();
      clearPetIdleTimer();
      setPetState('running');
    }

    function handleToolMessage() {
      clearPetReturnTimer();
      clearPetIdleTimer();
      setPetState('running');
    }

    function noteDraftActivity() {
      if (!pendingAssistantResponse) {
        schedulePetIdle();
      }
    }

    function consumeAssistantText(text) {
      pendingAssistantResponse = false;
      const petDirective = extractPetDirective(text);
      applyAssistantPetState(petDirective.state);
      return petDirective.text;
    }

    function handlePetResponse(message) {
      if (!message || message.type !== 'pet_response') {
        return false;
      }
      if (message.chat_id && message.chat_id !== petChatId) {
        return true;
      }
      const petDirective = extractPetDirective(message.content);
      if (petDirective.text) {
        showPetBubble(petDirective.text);
      } else {
        hidePetBubble();
      }
      if (petDirective.state) {
        applyAssistantPetState(petDirective.state);
      }
      return true;
    }

    return {
      destroy,
      init,
      handleSocketOpen,
      markAssistantPending,
      handleToolMessage,
      noteDraftActivity,
      consumeAssistantText,
      handlePetResponse,
    };
  }

  global.AgentPet = {
    createPetController,
  };
})(window);
