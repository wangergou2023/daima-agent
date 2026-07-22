const messages = document.getElementById('messages');
const chatShell = document.querySelector('.chat-shell');
const input = document.getElementById('input');
const form = document.getElementById('composer');
const statusEl = document.getElementById('status');
const dot = document.getElementById('dot');
const ctxBadge = document.getElementById('ctxBadge');
const themeSelect = document.getElementById('themeSelect');
const terminalSecuritySelect = document.getElementById('terminalSecuritySelect');
const interactiveModal = document.getElementById('interactiveModal');
const interactiveTitle = document.getElementById('interactiveTitle');
const interactivePrompt = document.getElementById('interactivePrompt');
const interactiveInput = document.getElementById('interactiveInput');
const interactiveCancel = document.getElementById('interactiveCancel');
const interactiveSubmit = document.getElementById('interactiveSubmit');
const sendBtn = document.getElementById('sendBtn');
const attachmentPreview = document.getElementById('attachmentPreview');
const attachmentImage = document.getElementById('attachmentImage');
const attachmentName = document.getElementById('attachmentName');
const attachmentRemove = document.getElementById('attachmentRemove');
const scrollToBottomBtn = document.getElementById('scrollToBottom');
const sessionSidebar = document.getElementById('sessionSidebar');
const sessionList = document.getElementById('sessionList');
const newSessionBtn = document.getElementById('newSessionBtn');
const petDock = document.getElementById('petDock');
const petChooser = document.getElementById('petChooser');
const petChooserButton = document.getElementById('petChooserButton');
const petChooserLabel = document.getElementById('petChooserLabel');
const petChooserMenu = document.getElementById('petChooserMenu');
const petButton = document.getElementById('petButton');
const petRunner = document.getElementById('petRunner');
const petSprite = document.getElementById('petSprite');
const petBubble = document.getElementById('petBubble');
const reconnectToast = document.getElementById('reconnectToast');
const reconnectToastText = document.getElementById('reconnectToastText');
const reconnectToastAction = document.getElementById('reconnectToastAction');

const CHAT_ID_KEY = 'agent_chat_id';
const THEME_KEY = 'agent_theme';
const PET_PACKAGE_KEY = 'agent_pet_package_id';
const NEAR_BOTTOM_PX = 72;
const EMPTY_MULTILINE_HEIGHT = 84;
const MAX_IMAGE_UPLOAD_BYTES = 10 * 1024 * 1024;
const DEFAULT_UI_CONFIG = Object.freeze({
  pet: {
    default_package_id: 'kitty.codex-pet',
    packages: [],
  },
  terminal: {
    security_level: 'build',
  },
});

const RECONNECT_SESSION_KEY = 'agent_last_session';
let storageWarningLogged = false;
let lastConnectionErrorText = '';

function readStorage(key, fallback = '') {
  try {
    const value = window.localStorage.getItem(key);
    return value == null ? fallback : value;
  } catch (error) {
    if (!storageWarningLogged) {
      storageWarningLogged = true;
      console.warn('localStorage unavailable, falling back to in-memory UI state', error);
    }
    return fallback;
  }
}

function writeStorage(key, value) {
  try {
    window.localStorage.setItem(key, value);
    return true;
  } catch (error) {
    if (!storageWarningLogged) {
      storageWarningLogged = true;
      console.warn('localStorage unavailable, falling back to in-memory UI state', error);
    }
    return false;
  }
}

const storedTheme = readStorage(THEME_KEY, 'warm') || 'warm';
let chatId = createChatId();
writeStorage(CHAT_ID_KEY, chatId);

let ws;
let baseStats = { model: 'unknown', used_tokens: 0, context_limit_tokens: 0 };
let stickToBottom = true;
let currentToolGroup = null;
let isConnected = false;
let connectionState = 'connecting';
let pendingAssistantResponse = false;
let stopRequested = false;
let pendingReasoningCard = null;
let pendingImagePath = '';
let pendingImageName = '';
let pendingImagePreviewUrl = '';
let imageUploadBusy = false;
let uiConfig = DEFAULT_UI_CONFIG;
let petController = null;
let availablePetPackages = normalizePetPackages(DEFAULT_UI_CONFIG);
let activePetPackageId = '';
let petChooserOpen = false;
let sessions = [];
let selectedSessionId = '';
let localSessions = [];
let lastMessageSeq = 0;
let sessionRestore = null;
let sessionStateRuntime = null;
let connectionStatePresenter = null;
let reconnectController = null;
let interactiveController = null;
var createSessionRestore = (window.AgentSessionRestore && window.AgentSessionRestore.createSessionRestore) || null;
var createSessionStateRuntime = (window.AgentSessionStateRuntime && window.AgentSessionStateRuntime.createSessionStateRuntime) || null;
var createConnectionStatePresenter = (window.AgentConnectionStatePresenter && window.AgentConnectionStatePresenter.createConnectionStatePresenter) || null;
var createReconnectController = (window.AgentReconnectController && window.AgentReconnectController.createReconnectController) || null;
var createInteractiveController = null;
var interactiveUiConfigFromAdapter = null;
var makeInteractiveReplyPayload = null;

function setLastMessageSeq(nextSeq) {
  lastMessageSeq = Number(nextSeq) || 0;
}

function isStopRequested() {
  return stopRequested === true;
}

function clearPendingReasoningCard() {
  pendingReasoningCard = null;
}

function appendHistoryMessage(message) {
  if (message?.seq !== undefined) {
    const nextSeq = Number(message.seq) || 0;
    if (nextSeq > lastMessageSeq) {
      setLastMessageSeq(nextSeq);
    }
  }
  appendNode(makeMessageNode(message.role, message.content || '', message.reasoning || ''));
}

function applyUploadedImage(data) {
  pendingImagePath = data?.image_path || '';
  pendingImageName = data?.filename || pendingImageName || '图片';
  imageUploadBusy = false;
  syncAttachmentPreview();
}

function onSocketOpenForPet() {
  if (petController) {
    petController.handleSocketOpen();
  }
}

function onToolActivity() {
  if (petController) {
    petController.handleToolMessage();
  }
}

function onPetResponse(data) {
  if (petController) {
    petController.handlePetResponse(data);
  }
}

function consumeAssistantText(text) {
  return petController
    ? petController.consumeAssistantText(text)
    : text;
}

function setPendingReasoningCardFromContent(content) {
  pendingReasoningCard = makeMessageNode('assistant', '', content);
  appendNode(pendingReasoningCard);
}

function commitPendingReasoningCard(text) {
  if (!pendingReasoningCard) {
    return false;
  }
  const copy = pendingReasoningCard.querySelector('.message-copy');
  if (copy) {
    renderAssistantMarkdown(copy, text);
  }
  pendingReasoningCard = null;
  return true;
}

function extractLastSeqFromHistory(history) {
  const items = Array.isArray(history) ? history : [];
  let maxSeq = 0;
  for (const item of items) {
    const seq = Number(item?.seq) || 0;
    if (seq > maxSeq) {
      maxSeq = seq;
    }
  }
  return maxSeq;
}

function ensureSessionStateRuntime() {
  if (sessionStateRuntime || !createSessionStateRuntime) {
    return sessionStateRuntime;
  }
  sessionStateRuntime = createSessionStateRuntime({
    createSessionRestore,
    fetchImpl: fetch.bind(window),
    interactiveUiConfig,
    renderHistoryMessages,
    renderSessions,
    saveReconnectSession,
    refreshContextStats,
    showReconnectToast(chatIdToRestore, messageCount) {
      ensureConnectionStatePresenter()?.showReconnectToast?.(chatIdToRestore, messageCount);
    },
    setSelectedSessionId(nextChatId) {
      selectedSessionId = nextChatId;
    },
    getChatId() {
      return chatId;
    },
    getLastMessageSeq() {
      return lastMessageSeq;
    },
    getMessageCount() {
      return messages.childElementCount;
    },
    emptySnapshot() {
      return { coordinators: [] };
    },
  });
  return sessionStateRuntime;
}

function ensureConnectionStatePresenter() {
  if (connectionStatePresenter || !createConnectionStatePresenter) {
    return connectionStatePresenter;
  }
  connectionStatePresenter = createConnectionStatePresenter({
    statusEl,
    dotEl: dot,
    reconnectToastEl: reconnectToast,
    reconnectToastTextEl: reconnectToastText,
    reconnectToastActionEl: reconnectToastAction,
    getLastConnectionErrorText() {
      return lastConnectionErrorText;
    },
    setConnectionState(nextState) {
      connectionState = nextState;
    },
    setIsConnected(nextValue) {
      isConnected = nextValue === true;
    },
    syncSendState,
  });
  return connectionStatePresenter;
}

function ensureReconnectController() {
  if (reconnectController || !createReconnectController) {
    return reconnectController;
  }
  reconnectController = createReconnectController({
    getChatId() {
      return chatId;
    },
    hideReconnectToast() {
      ensureConnectionStatePresenter()?.hideReconnectToast?.();
    },
    setLastMessageSeq(nextSeq) {
      lastMessageSeq = Number(nextSeq) || 0;
    },
    restoreSessionViewState(targetChatId, options) {
      return ensureSessionStateRuntime()?.restoreSessionViewState?.(targetChatId, options);
    },
  });
  return reconnectController;
}

function createChatId() {
  return `web_${Math.random().toString(36).slice(2, 8)}`;
}

function setActiveChatId(nextChatId) {
  if (!nextChatId || nextChatId === chatId) return;
  chatId = nextChatId;
  writeStorage(CHAT_ID_KEY, chatId);
  if (petController) {
    attachPetController();
  }
}

function formatSessionTime(ts) {
  const value = Number(ts) || 0;
  if (!value) return '新会话';
  const date = new Date(value * 1000);
  const now = new Date();
  const sameDay = date.toDateString() === now.toDateString();
  if (sameDay) {
    return date.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' });
  }
  return date.toLocaleDateString([], { month: '2-digit', day: '2-digit' });
}

function sessionTitle(item) {
  const id = item?.chat_id || '';
  if (item?.is_placeholder) return '当前新会话';
  if (id.startsWith('web_')) return id.replace(/^web_/, 'Web ');
  if (id.startsWith('pet_web_')) return id.replace(/^pet_web_/, '宠物 ');
  if (id.startsWith('ou_')) return id.replace(/^ou_/, '历史 ');
  return id;
}

function mergeSessionLists(remoteSessions, draftSessions) {
  const merged = [];
  const seen = new Set();

  for (const item of [...draftSessions, ...remoteSessions]) {
    const id = item?.chat_id;
    if (typeof id !== 'string' || !id.trim() || seen.has(id)) continue;
    seen.add(id);
    merged.push(item);
  }

  merged.sort((a, b) => {
    const at = Number(a?.latest_ts) || 0;
    const bt = Number(b?.latest_ts) || 0;
    if (at !== bt) return bt - at;
    return String(a?.chat_id || '').localeCompare(String(b?.chat_id || ''));
  });
  return merged;
}

function upsertLocalSession(chat_id, latest_ts = Math.floor(Date.now() / 1000)) {
  if (!chat_id) return;
  const next = {
    chat_id,
    latest_ts,
    has_history: true,
    is_local: true,
  };
  localSessions = [next, ...localSessions.filter((item) => item.chat_id !== chat_id)];
}

function renderSessions() {
  if (!sessionList) return;
  sessionList.innerHTML = '';

  const displayBase = mergeSessionLists(sessions, localSessions);
  const hasCurrent = displayBase.some((item) => item.chat_id === chatId);
  const shouldShowCurrentDraft = !hasCurrent && messages.childElementCount > 0;
  const displaySessions = hasCurrent || !shouldShowCurrentDraft
    ? displayBase
    : [{ chat_id: chatId, latest_ts: 0, has_history: false, is_placeholder: true }, ...displayBase];

  if (!displaySessions.length) {
    const empty = document.createElement('div');
    empty.className = 'session-empty';
    empty.textContent = '暂无会话';
    sessionList.appendChild(empty);
    return;
  }

  for (const item of displaySessions) {
    const row = document.createElement('div');
    row.className = 'session-row';
    if (item.chat_id === selectedSessionId) row.classList.add('active');
    row.dataset.chatId = item.chat_id;

    const button = document.createElement('button');
    button.type = 'button';
    button.className = 'session-open';
    button.dataset.chatId = item.chat_id;
    button.innerHTML = `
      <span class="session-title"></span>
      <span class="session-meta"></span>
    `;
    button.querySelector('.session-title').textContent = sessionTitle(item);
    button.querySelector('.session-meta').textContent = formatSessionTime(item.latest_ts);
    row.appendChild(button);

    if (!item.is_placeholder) {
      const deleteButton = document.createElement('button');
      deleteButton.type = 'button';
      deleteButton.className = 'session-delete';
      deleteButton.dataset.chatId = item.chat_id;
      deleteButton.setAttribute('aria-label', `删除会话 ${sessionTitle(item)}`);
      deleteButton.title = '删除会话';
      deleteButton.textContent = '×';
      row.appendChild(deleteButton);
    }

    sessionList.appendChild(row);
  }
}

async function loadSessions() {
  if (!sessionList) return;
  try {
    const resp = await fetch('/api/sessions', { cache: 'no-store' });
    if (!resp.ok) return;
    const data = await resp.json();
    sessions = Array.isArray(data?.sessions)
      ? data.sessions.filter((item) => typeof item?.chat_id === 'string' && item.chat_id.trim() && item.has_history !== false)
      : [];
    const remoteIds = new Set(sessions.map((item) => item.chat_id));
    localSessions = localSessions.filter((item) => !remoteIds.has(item.chat_id));
    renderSessions();
  } catch (_) {
    renderSessions();
  }
}

function renderHistoryMessages(history) {
  messages.innerHTML = '';
  currentToolGroup = null;
  for (const item of history) {
    const role = item?.role;
    const content = item?.content;
    const reasoning = item?.reasoning || '';
    if (!content || (role !== 'user' && role !== 'assistant')) continue;
    messages.appendChild(makeMessageNode(role, content, reasoning));
  }
  syncEmptyState();
  syncEmptyComposerLayout();
  stickToBottom = true;
  scrollToBottom(false);
  syncScrollButton();
  setLastMessageSeq(extractLastSeqFromHistory(history));
}

async function switchSession(nextChatId) {
  if (!nextChatId || nextChatId === chatId) return;
  clearPendingImage();
  setActiveChatId(nextChatId);
  selectedSessionId = nextChatId;
  lastMessageSeq = 0;
  saveReconnectSession();
  renderSessions();
  await ensureSessionStateRuntime()?.restoreSessionViewState?.(chatId, {
    requireCurrentChat: true,
    applyHistory: true,
    renderEmptyHistory: true,
    renderSessions: true,
    saveReconnect: true,
    refreshContextStats: true,
    restoreSubagent: true,
    forceApplySnapshot: true,
  });
}

async function deleteSession(targetChatId) {
  if (!targetChatId) return;

  sessions = sessions.filter((item) => item.chat_id !== targetChatId);
  localSessions = localSessions.filter((item) => item.chat_id !== targetChatId);

  const deletedCurrent = targetChatId === chatId || targetChatId === selectedSessionId;
  if (deletedCurrent) {
    setActiveChatId(createChatId());
    selectedSessionId = '';
    lastMessageSeq = 0;
    saveReconnectSession();
    renderHistoryMessages([]);
  }
  renderSessions();

  try {
    const resp = await fetch(`/api/session_delete?chat_id=${encodeURIComponent(targetChatId)}`, {
      method: 'POST',
      cache: 'no-store',
    });
    if (!resp.ok) {
      await loadSessions();
    }
  } catch (_) {
    await loadSessions();
  }
}

function startNewSession() {
  clearPendingImage();
  setActiveChatId(createChatId());
  selectedSessionId = '';
  lastMessageSeq = 0;
  saveReconnectSession();
  renderHistoryMessages([]);
  renderSessions();
  refreshContextStats();
}

function resolvePetPackageId(config) {
  const packageId = config?.pet?.default_package_id;
  return typeof packageId === 'string' && packageId.trim()
    ? packageId.trim()
    : DEFAULT_UI_CONFIG.pet.default_package_id;
}

function normalizePetPackages(config) {
  const packages = Array.isArray(config?.pet?.packages) ? config.pet.packages : [];
  const seen = new Set();
  const normalized = [];

  for (const item of packages) {
    const packageId = typeof item?.package_id === 'string' ? item.package_id.trim() : '';
    if (!packageId || seen.has(packageId)) continue;
    seen.add(packageId);
    normalized.push({
      package_id: packageId,
      pet_id: typeof item?.pet_id === 'string' && item.pet_id.trim()
        ? item.pet_id.trim()
        : packageId.replace(/\.codex-pet$/, ''),
      display_name: typeof item?.display_name === 'string' && item.display_name.trim()
        ? item.display_name.trim()
        : packageId.replace(/\.codex-pet$/, ''),
    });
  }

  if (!normalized.some((item) => item.package_id === resolvePetPackageId(config))) {
    normalized.push({
      package_id: resolvePetPackageId(config),
      pet_id: resolvePetPackageId(config).replace(/\.codex-pet$/, ''),
      display_name: resolvePetPackageId(config).replace(/\.codex-pet$/, ''),
    });
  }

  normalized.sort((a, b) => a.display_name.localeCompare(b.display_name));
  return normalized;
}

function resolveInitialPetPackageId(config) {
  const storedPackageId = readStorage(PET_PACKAGE_KEY, '');
  if (storedPackageId && availablePetPackages.some((item) => item.package_id === storedPackageId)) {
    return storedPackageId;
  }
  const defaultPackageId = resolvePetPackageId(config);
  if (availablePetPackages.some((item) => item.package_id === defaultPackageId)) {
    return defaultPackageId;
  }
  return availablePetPackages[0]?.package_id || DEFAULT_UI_CONFIG.pet.default_package_id;
}

function createPetController(packageId) {
  if (!window.AgentPet || typeof window.AgentPet.createPetController !== 'function') {
    return null;
  }
  return window.AgentPet.createPetController({
    chatId,
    packageId,
    elements: {
      dock: petDock,
      button: petButton,
      runner: petRunner,
      sprite: petSprite,
      bubble: petBubble,
    },
    sendJson(payload) {
      if (!ws || ws.readyState !== WebSocket.OPEN) return;
      ws.send(JSON.stringify(payload));
    },
  });
}

async function refreshUiConfig() {
  try {
    const resp = await fetch('/api/ui_config', { cache: 'no-store' });
    if (resp.ok) {
      const data = await resp.json();
      if (data && typeof data === 'object') {
        uiConfig = {
          ...DEFAULT_UI_CONFIG,
          ...data,
          pet: {
            ...DEFAULT_UI_CONFIG.pet,
            ...(data.pet || {}),
          },
          terminal: {
            ...DEFAULT_UI_CONFIG.terminal,
            ...(data.terminal || {}),
          },
        };
      }
    }
  } catch (_) {}
  syncTerminalSecurityControl();
  availablePetPackages = normalizePetPackages(uiConfig);
}

function renderPetChooser(selectedPackageId) {
  if (!petChooser || !petChooserButton || !petChooserLabel || !petChooserMenu) return;

  petChooserMenu.innerHTML = '';
  const selectedPet = availablePetPackages.find((item) => item.package_id === selectedPackageId);
  petChooserLabel.textContent = selectedPet?.display_name || 'Pet';

  for (const item of availablePetPackages) {
    const choice = document.createElement('button');
    choice.type = 'button';
    choice.className = 'pet-choice';
    choice.setAttribute('role', 'option');
    choice.setAttribute('aria-selected', item.package_id === selectedPackageId ? 'true' : 'false');
    choice.dataset.packageId = item.package_id;
    if (item.package_id === selectedPackageId) {
      choice.classList.add('active');
    }
    choice.innerHTML = `<span class="pet-choice-name">${item.display_name}</span><span class="pet-choice-meta">${item.package_id}</span>`;
    petChooserMenu.appendChild(choice);
  }

  const shouldShow = availablePetPackages.length > 1;
  petChooser.hidden = !shouldShow;
  petChooser.setAttribute('aria-hidden', shouldShow ? 'false' : 'true');
  petChooserButton.disabled = !shouldShow;
  setPetChooserOpen(false);
}

function setPetChooserOpen(open) {
  if (!petChooser || !petChooserButton || !petChooserMenu) return;
  petChooserOpen = !!open && availablePetPackages.length > 1;
  petChooser.classList.toggle('open', petChooserOpen);
  petChooserButton.setAttribute('aria-expanded', petChooserOpen ? 'true' : 'false');
  petChooserMenu.hidden = !petChooserOpen;
}

function togglePetChooser() {
  setPetChooserOpen(!petChooserOpen);
}

function attachPetController() {
  if (petController && typeof petController.destroy === 'function') {
    petController.destroy();
  }
  petController = createPetController(activePetPackageId);
  if (!petController) return;
  petController.init();
  if (pendingAssistantResponse) {
    petController.markAssistantPending();
    return;
  }
  if (isConnected) {
    petController.handleSocketOpen();
  }
}

function selectPetPackage(packageId) {
  if (!packageId || !availablePetPackages.some((item) => item.package_id === packageId)) {
    return;
  }
  if (packageId === activePetPackageId && petController) {
    setPetChooserOpen(false);
    return;
  }
  activePetPackageId = packageId;
  writeStorage(PET_PACKAGE_KEY, packageId);
  renderPetChooser(activePetPackageId);
  attachPetController();
}

function applyTheme(theme) {
  const next = theme || 'warm';
  document.body.dataset.theme = next;
  if (themeSelect) themeSelect.value = next;
  writeStorage(THEME_KEY, next);
}

function normalizeTerminalSecurityLevel(level) {
  return ['plan', 'build'].includes(level) ? level : 'build';
}

function syncTerminalSecurityControl() {
  if (!terminalSecuritySelect) return;
  terminalSecuritySelect.value = normalizeTerminalSecurityLevel(uiConfig?.terminal?.security_level);
}

async function setTerminalSecurityLevel(level) {
  if (!terminalSecuritySelect) return;
  const next = normalizeTerminalSecurityLevel(level);
  const prev = normalizeTerminalSecurityLevel(uiConfig?.terminal?.security_level);
  terminalSecuritySelect.disabled = true;
  terminalSecuritySelect.value = next;
  try {
    const resp = await fetch(`/api/terminal_security?level=${encodeURIComponent(next)}`, {
      method: 'POST',
      cache: 'no-store',
    });
    if (!resp.ok) throw new Error('save failed');
    uiConfig = {
      ...uiConfig,
      terminal: {
        ...(uiConfig.terminal || {}),
        security_level: next,
      },
    };
  } catch (_) {
    terminalSecuritySelect.value = prev;
  } finally {
    terminalSecuritySelect.disabled = false;
  }
}

function autoResize() {
  input.style.height = 'auto';
  input.style.height = `${Math.min(input.scrollHeight, 220)}px`;
  renderContextBadge();
  syncEmptyComposerLayout();
  syncSendState();
}

function estimateDraftTokens(text) {
  return Math.max(0, Math.ceil((text || '').length / 4));
}

function formatK(n) {
  if (!Number.isFinite(n)) return '--';
  if (n >= 1000) {
    const value = n / 1000;
    const digits = value >= 100 ? 0 : 1;
    return `${value.toFixed(digits)}K`;
  }
  return `${Math.round(n)}`;
}

function renderContextBadge() {
  const limit = Number(baseStats.context_limit_tokens) || 0;
  const used = Math.max(0, (Number(baseStats.used_tokens) || 0) + estimateDraftTokens(input.value));
  const model = baseStats.model || 'unknown';
  const usageText = limit > 0 ? `${formatK(used)}/${formatK(limit)}` : `${formatK(used)}/--`;
  ctxBadge.innerHTML = `<strong>${model}</strong><span>${usageText}</span>`;
}

function syncAttachmentPreview() {
  if (!attachmentPreview || !attachmentImage || !attachmentName) return;
  const hasImage = !!pendingImagePreviewUrl || !!pendingImagePath || imageUploadBusy;
  attachmentPreview.hidden = !hasImage;
  attachmentImage.hidden = !pendingImagePreviewUrl;
  if (pendingImagePreviewUrl) {
    attachmentImage.src = pendingImagePreviewUrl;
    attachmentImage.alt = pendingImageName || '已粘贴的图片';
  } else {
    attachmentImage.removeAttribute('src');
  }
  attachmentName.textContent = imageUploadBusy ? '图片上传中...' : pendingImageName;
}

function clearPendingImage() {
  if (pendingImagePreviewUrl) {
    URL.revokeObjectURL(pendingImagePreviewUrl);
  }
  pendingImagePath = '';
  pendingImageName = '';
  pendingImagePreviewUrl = '';
  imageUploadBusy = false;
  syncAttachmentPreview();
  syncSendState();
}

function isSupportedImageFile(file) {
  if (!file) return false;
  if (file.size <= 0 || file.size > MAX_IMAGE_UPLOAD_BYTES) return false;
  const type = String(file.type || '').toLowerCase();
  return ['image/png', 'image/jpeg', 'image/webp', 'image/gif'].includes(type);
}

function imageExtensionForType(type) {
  if (type === 'image/jpeg') return 'jpg';
  if (type === 'image/webp') return 'webp';
  if (type === 'image/gif') return 'gif';
  return 'png';
}

function normalizeImageFilename(file) {
  const name = String(file?.name || '').trim();
  if (/\.(png|jpe?g|webp|gif)$/i.test(name)) return name;
  return `image_${Date.now()}.${imageExtensionForType(String(file?.type || '').toLowerCase())}`;
}

function dataUrlToFile(dataUrl) {
  const match = /^data:(image\/(?:png|jpeg|webp|gif));base64,([a-z0-9+/=]+)$/i.exec(String(dataUrl || ''));
  if (!match) return null;

  const mimeType = match[1].toLowerCase();
  const raw = atob(match[2]);
  const bytes = new Uint8Array(raw.length);
  for (let i = 0; i < raw.length; i++) {
    bytes[i] = raw.charCodeAt(i);
  }
  return new File([bytes], `image_${Date.now()}.${imageExtensionForType(mimeType)}`, { type: mimeType });
}

function findPastedImageFile(clipboardData) {
  if (!clipboardData) return null;

  const files = Array.from(clipboardData.files || []);
  for (const file of files) {
    if (isSupportedImageFile(file)) return file;
  }

  const items = Array.from(clipboardData.items || []);
  for (const item of items) {
    if (!item.type || !item.type.startsWith('image/')) continue;
    const file = item.getAsFile();
    if (isSupportedImageFile(file)) return file;
  }

  const html = clipboardData.getData ? clipboardData.getData('text/html') : '';
  const dataUrlMatch = /src=["'](data:image\/(?:png|jpeg|webp|gif);base64,[^"']+)["']/i.exec(html);
  return dataUrlToFile(dataUrlMatch?.[1]);
}

async function uploadImageFile(file) {
  if (!isSupportedImageFile(file) || !ws || ws.readyState !== WebSocket.OPEN) {
    return false;
  }

  imageUploadBusy = true;
  pendingImagePath = '';
  pendingImageName = normalizeImageFilename(file);
  if (pendingImagePreviewUrl) {
    URL.revokeObjectURL(pendingImagePreviewUrl);
  }
  pendingImagePreviewUrl = URL.createObjectURL(file);
  syncAttachmentPreview();
  syncSendState();

  try {
    const bytes = await file.arrayBuffer();
    ws.send(JSON.stringify({
      type: 'upload_image',
      chat_id: chatId,
      filename: pendingImageName,
      mime_type: file.type || 'application/octet-stream',
      size: file.size,
    }));
    ws.send(bytes);
    return true;
  } catch (_) {
    clearPendingImage();
    return false;
  }
}

async function refreshContextStats() {
  try {
    const resp = await fetch(`/api/context_stats?chat_id=${encodeURIComponent(chatId)}`, { cache: 'no-store' });
    if (!resp.ok) return;
    const data = await resp.json();
    if (!data) return;
    baseStats = {
      model: data.model || 'unknown',
      used_tokens: Number(data.used_tokens) || 0,
      context_limit_tokens: Number(data.context_limit_tokens) || 0,
    };
    renderContextBadge();
  } catch (_) {}
}

function isNearBottom() {
  return messages.scrollHeight - messages.scrollTop - messages.clientHeight < NEAR_BOTTOM_PX;
}

function scrollToBottom(smooth) {
  messages.scrollTo({
    top: messages.scrollHeight,
    behavior: smooth ? 'smooth' : 'auto',
  });
}

function syncScrollButton() {
  const shouldShow = messages.scrollHeight > messages.clientHeight && !isNearBottom();
  scrollToBottomBtn.hidden = !shouldShow;
}

function syncEmptyState() {
  const isEmpty = messages.childElementCount === 0;
  chatShell.classList.toggle('empty-state', isEmpty);
  input.placeholder = isEmpty ? '有问题，尽管问' : '继续输入你的问题';
}

function syncEmptyComposerLayout() {
  const isEmpty = chatShell.classList.contains('empty-state');
  if (!isEmpty) {
    chatShell.classList.remove('empty-multiline');
    return;
  }
  const multiline = input.value.includes('\n') || input.scrollHeight > EMPTY_MULTILINE_HEIGHT;
  chatShell.classList.toggle('empty-multiline', multiline);
}

function syncSendState() {
  if (!sendBtn) return;
  const generating = pendingAssistantResponse;
  const hasDraft = !!input.value.trim() || !!pendingImagePath;
  sendBtn.classList.toggle('is-generating', generating);
  sendBtn.disabled = !isConnected || imageUploadBusy || (generating && stopRequested) || (!generating && !hasDraft);
  if (generating) {
    sendBtn.textContent = '';
    sendBtn.title = '停止生成';
    sendBtn.setAttribute('aria-label', '停止生成');
  } else {
    sendBtn.textContent = '发送';
    sendBtn.title = 'Send message';
    sendBtn.setAttribute('aria-label', 'Send message');
  }
}

function appendNode(node) {
  messages.appendChild(node);
  syncEmptyState();
  syncEmptyComposerLayout();
  if (stickToBottom) {
    scrollToBottom(false);
  }
  syncScrollButton();
}

function appendInlineMarkdown(target, text) {
  const parts = text.split(/(`[^`\n]+`|\*\*[^*\n][^`\n]*?\*\*)/g);
  for (const part of parts) {
    if (!part) continue;
    if (part.startsWith('`') && part.endsWith('`') && part.length >= 2) {
      const code = document.createElement('code');
      code.textContent = part.slice(1, -1);
      target.appendChild(code);
      continue;
    }
    if (part.startsWith('**') && part.endsWith('**') && part.length >= 4) {
      const strong = document.createElement('strong');
      strong.textContent = part.slice(2, -2);
      target.appendChild(strong);
      continue;
    }
    target.appendChild(document.createTextNode(part));
  }
}

function isUnorderedListLine(line) {
  return /^[-*]\s+/.test(line.trim());
}

function isOrderedListLine(line) {
  return /^\d+\.\s+/.test(line.trim());
}

function headingLevel(line) {
  const match = /^(#{1,3})\s+(.*)$/.exec(line.trim());
  if (!match) return null;
  return { level: match[1].length, text: match[2] };
}

function isRuleLine(line) {
  return /^---+$/.test(line.trim());
}

function isQuoteLine(line) {
  return /^>\s?/.test(line.trim());
}

function splitMarkdownTableRow(line) {
  const source = String(line ?? '').trim();
  if (!source.includes('|')) return [];
  const normalized = source.replace(/^\|/, '').replace(/\|$/, '');
  return normalized.split('|').map((cell) => cell.trim());
}

function isTableSeparatorLine(line) {
  const cells = splitMarkdownTableRow(line);
  if (!cells.length) return false;
  return cells.every((cell) => /^:?-{3,}:?$/.test(cell));
}

function isTableRowLine(line) {
  const cells = splitMarkdownTableRow(line);
  return cells.length >= 2;
}

function appendTableCells(row, cells, cellTag) {
  cells.forEach((cell) => {
    const el = document.createElement(cellTag);
    appendInlineMarkdown(el, cell);
    row.appendChild(el);
  });
}

function renderAssistantMarkdown(target, text) {
  target.textContent = '';
  const fragment = document.createDocumentFragment();
  const lines = String(text ?? '').replace(/\r\n?/g, '\n').split('\n');
  let index = 0;

  while (index < lines.length) {
    const current = lines[index];
    const trimmed = current.trim();

    if (!trimmed) {
      index += 1;
      continue;
    }

    if (isRuleLine(current)) {
      fragment.appendChild(document.createElement('hr'));
      index += 1;
      continue;
    }

    const heading = headingLevel(current);
    if (heading) {
      const el = document.createElement(`h${heading.level}`);
      appendInlineMarkdown(el, heading.text);
      fragment.appendChild(el);
      index += 1;
      continue;
    }

    if (trimmed.startsWith('```')) {
      const lang = trimmed.slice(3).trim();
      const codeLines = [];
      index += 1;
      while (index < lines.length && !lines[index].trim().startsWith('```')) {
        codeLines.push(lines[index]);
        index += 1;
      }
      if (index < lines.length) index += 1;

      const block = document.createElement('section');
      block.className = 'md-code-block';
      if (lang) {
        const label = document.createElement('div');
        label.className = 'md-code-label';
        label.textContent = lang;
        block.appendChild(label);
      }
      const pre = document.createElement('pre');
      const code = document.createElement('code');
      code.textContent = codeLines.join('\n');
      pre.appendChild(code);
      block.appendChild(pre);
      fragment.appendChild(block);
      continue;
    }

    if (isQuoteLine(current)) {
      const quote = document.createElement('blockquote');
      const quoteLines = [];
      while (index < lines.length && isQuoteLine(lines[index])) {
        quoteLines.push(lines[index].trim().replace(/^>\s?/, ''));
        index += 1;
      }
      quoteLines.forEach((line, lineIndex) => {
        if (lineIndex > 0) quote.appendChild(document.createElement('br'));
        appendInlineMarkdown(quote, line);
      });
      fragment.appendChild(quote);
      continue;
    }

    if (
      isTableRowLine(current) &&
      index + 1 < lines.length &&
      isTableSeparatorLine(lines[index + 1])
    ) {
      const tableWrap = document.createElement('div');
      tableWrap.className = 'md-table-wrap';
      const table = document.createElement('table');
      table.className = 'md-table';
      const thead = document.createElement('thead');
      const tbody = document.createElement('tbody');

      const headerRow = document.createElement('tr');
      appendTableCells(headerRow, splitMarkdownTableRow(current), 'th');
      thead.appendChild(headerRow);
      table.appendChild(thead);

      index += 2;
      while (index < lines.length && isTableRowLine(lines[index]) && !isTableSeparatorLine(lines[index])) {
        const bodyRow = document.createElement('tr');
        appendTableCells(bodyRow, splitMarkdownTableRow(lines[index]), 'td');
        tbody.appendChild(bodyRow);
        index += 1;
      }

      table.appendChild(tbody);
      tableWrap.appendChild(table);
      fragment.appendChild(tableWrap);
      continue;
    }

    if (isUnorderedListLine(current) || isOrderedListLine(current)) {
      const ordered = isOrderedListLine(current);
      const list = document.createElement(ordered ? 'ol' : 'ul');
      while (index < lines.length) {
        const line = lines[index];
        const matches = ordered ? isOrderedListLine(line) : isUnorderedListLine(line);
        if (!matches) break;
        const item = document.createElement('li');
        const content = ordered
          ? line.trim().replace(/^\d+\.\s+/, '')
          : line.trim().replace(/^[-*]\s+/, '');
        appendInlineMarkdown(item, content);
        list.appendChild(item);
        index += 1;
      }
      fragment.appendChild(list);
      continue;
    }

    const paragraphLines = [];
    while (index < lines.length) {
      const line = lines[index];
      const next = line.trim();
      if (!next) break;
      if (
        next.startsWith('```') ||
        isUnorderedListLine(line) ||
        isOrderedListLine(line) ||
        headingLevel(line) ||
        isRuleLine(line) ||
        isQuoteLine(line) ||
        (isTableRowLine(line) && index + 1 < lines.length && isTableSeparatorLine(lines[index + 1]))
      ) break;
      paragraphLines.push(line);
      index += 1;
    }

    const paragraph = document.createElement('p');
    paragraphLines.forEach((line, lineIndex) => {
      if (lineIndex > 0) paragraph.appendChild(document.createElement('br'));
      appendInlineMarkdown(paragraph, line);
    });
    fragment.appendChild(paragraph);
  }

  target.appendChild(fragment);
}

function makeReasoningNode(reasoning) {
  if (!reasoning) return null;
  const details = document.createElement('details');
  details.className = 'reasoning-block';

  const summary = document.createElement('summary');
  summary.textContent = '思考过程';
  details.appendChild(summary);

  const body = document.createElement('div');
  body.className = 'reasoning-copy';
  renderAssistantMarkdown(body, reasoning);
  details.appendChild(body);
  return details;
}

function makeMessageNode(role, text, reasoning = '') {
  const row = document.createElement('article');
  row.className = `message-row ${role}`;

  const card = document.createElement('div');
  card.className = `message-card ${role}`;

  if (role === 'assistant') {
    const label = document.createElement('div');
    label.className = 'message-label';
    label.textContent = 'Agent';
    card.appendChild(label);
  }

  if (role === 'assistant' && reasoning) {
    const reasoningNode = makeReasoningNode(reasoning);
    if (reasoningNode) {
      card.appendChild(reasoningNode);
    }
  }

  const copy = document.createElement('div');
  copy.className = 'message-copy';
  if (role === 'assistant') {
    renderAssistantMarkdown(copy, text);
  } else {
    copy.textContent = text;
  }
  card.appendChild(copy);

  row.appendChild(card);
  return row;
}

function updateToolSummary(group) {
  group.details.dataset.steps = String(group.lines.childElementCount);
}

function createToolGroup() {
  const row = document.createElement('article');
  row.className = 'message-row tool';

  const details = document.createElement('details');
  details.className = 'tool-group';
  details.open = true;

  const summary = document.createElement('summary');
  summary.className = 'tool-summary';

  const icon = document.createElement('span');
  icon.className = 'tool-icon';
  icon.textContent = '⌘';

  const summaryText = document.createElement('span');
  summaryText.className = 'tool-summary-text';

  const title = document.createElement('span');
  title.className = 'tool-title';
  title.textContent = '工具执行';

  const caret = document.createElement('span');
  caret.className = 'tool-caret';
  caret.textContent = '›';

  summaryText.appendChild(title);
  summary.appendChild(icon);
  summary.appendChild(summaryText);
  summary.appendChild(caret);

  const lines = document.createElement('div');
  lines.className = 'tool-lines';

  details.appendChild(summary);
  details.appendChild(lines);
  row.appendChild(details);

  appendNode(row);
  return { details, lines };
}

function addToolMessage(text) {
  if (!currentToolGroup) {
    currentToolGroup = createToolGroup();
  }
  const line = document.createElement('div');
  line.className = 'tool-line';
  line.textContent = text;
  currentToolGroup.lines.appendChild(line);
  updateToolSummary(currentToolGroup);
  if (stickToBottom) {
    scrollToBottom(false);
  }
  syncScrollButton();
}

function addMessage(role, text) {
  if (!text) return;
  if (role === 'tool') {
    addToolMessage(text);
    pendingReasoningCard = null;
    return;
  }
  currentToolGroup = null;
  pendingReasoningCard = null;
  appendNode(makeMessageNode(role, text));
}

function addSystemNote(text) {
  if (!text) return;
  addMessage('assistant', text);
}

function interactiveUiConfig(requestType) {
  return interactiveUiConfigFromAdapter
    ? interactiveUiConfigFromAdapter(requestType)
    : {
      title: '需要授权',
      prompt: '命令需要提升权限。',
      placeholder: '输入 sudo 密码',
      submitText: '继续',
      inputType: 'password',
      label: 'sudo permission',
      blockerKind: 'permission',
    };
}

function ensureInteractiveController() {
  if (interactiveController || !createInteractiveController) {
    return interactiveController;
  }
  interactiveController = createInteractiveController({
    modalEl: interactiveModal,
    titleEl: interactiveTitle,
    promptEl: interactivePrompt,
    inputEl: interactiveInput,
    submitEl: interactiveSubmit,
    focusInput() {
      setTimeout(() => interactiveInput.focus(), 30);
    },
  });
  return interactiveController;
}

function submitInteractivePrompt(cancelled) {
  const controller = ensureInteractiveController();
  if (!ws || ws.readyState !== WebSocket.OPEN) return;
  const payload = controller?.buildReplyPayload?.(makeInteractiveReplyPayload, chatId, !!cancelled) || null;
  if (!payload) return;
  ws.send(JSON.stringify(payload));
}

function onSocketOpen() {
  isConnected = true;
  connectionState = 'connected';
  ensureConnectionStatePresenter()?.setStatus?.(true);
  onSocketOpenForPet();
  syncSendState();
  refreshContextStats();
}

function onSocketClose() {
  isConnected = false;
  connectionState = 'disconnected';
  pendingAssistantResponse = false;
  stopRequested = false;
  currentToolGroup = null;
  pendingReasoningCard = null;
  ensureConnectionStatePresenter()?.setStatus?.(false);
  syncSendState();
}

function onSocketError() {
  isConnected = false;
  connectionState = 'disconnected';
  ensureConnectionStatePresenter()?.setStatus?.(false);
}

function onSocketMessage(e) {
  var data;
  try { data = JSON.parse(e.data); } catch (_) { return; }
  if (!data || typeof data !== 'object') return;

  var msgType = data.type || '';
  if (data.seq !== undefined) {
    setLastMessageSeq(data.seq);
  }

  if (msgType === 'response') {
    if (data.content) {
      addMessage('assistant', data.content);
    }
    pendingAssistantResponse = false;
    stopRequested = false;
    syncSendState();
    upsertLocalSession(chatId);
    renderSessions();
    saveReconnectSession();
    setTimeout(loadSessions, 600);
  } else if (msgType === 'tool' && data.content) {
    addMessage('tool', data.content);
  } else if (msgType === 'error' && data.content) {
    addSystemNote(data.content);
  } else if (msgType === 'stopped') {
    pendingAssistantResponse = false;
    stopRequested = false;
    syncSendState();
    if (data.content) {
      addSystemNote(data.content);
    }
  } else if (msgType === 'reasoning') {
    if (data.content) {
      setPendingReasoningCardFromContent(data.content);
    }
  } else if (msgType === 'upload_error') {
    imageUploadBusy = false;
    pendingImagePath = '';
    syncAttachmentPreview();
    syncSendState();
  } else if (msgType === 'upload_done') {
    if (data.image_path) {
      pendingImagePath = data.image_path;
    }
    imageUploadBusy = false;
    syncAttachmentPreview();
    syncSendState();
  } else if (msgType === 'tool_activity') {
    onToolActivity();
  } else if (msgType === 'interactive_request') {
    ensureInteractiveController()?.show?.(data, makeInteractiveReplyPayload);
  }
}

function attachSocketHandlers(sock) {
  sock.onopen = onSocketOpen;
  sock.onclose = onSocketClose;
  sock.onerror = onSocketError;
  sock.onmessage = onSocketMessage;
}

function ensureSocketConnection() {
  var boot = window._bootstrapWs;

  /* 如果 bootstrap WebSocket 已关闭或不存在，重新创建一个 */
  if (!boot || boot.readyState === WebSocket.CLOSED || boot.readyState === WebSocket.CLOSING) {
    try {
      var chatIdForWs = chatId || '';
      var wsUrl = (location.protocol === 'https:' ? 'wss' : 'ws') + '://' + location.host + '/ws';
      if (chatIdForWs) wsUrl += '?chat_id=' + encodeURIComponent(chatIdForWs);
      boot = new WebSocket(wsUrl);
      window._bootstrapWs = boot;
      window._bootstrapChatId = chatId;
    } catch (_) {
      return;
    }
  }

  ws = boot;
  if (window._bootstrapChatId) chatId = window._bootstrapChatId;

  /* 设置 app.js 处理器（覆盖任何已有处理器） */
  boot.onopen = function(e) {
    onSocketOpen(e);
  };

  boot.onclose = function(e) {
    onSocketClose(e);
  };

  boot.onerror = function(e) {
    onSocketError(e);
  };

  boot.onmessage = function(e) {
    onSocketMessage(e);
  };

  /* 如果连接已经建立，立即同步状态 */
  if (boot.readyState === WebSocket.OPEN) {
    onSocketOpen();
  }
}

function sendUserMessage(text, cancelCurrent) {
  const imagePath = pendingImagePath;
  const displayText = text || (imagePath ? '请看这张图片' : '');
  if (!displayText || imageUploadBusy || !ws || ws.readyState !== WebSocket.OPEN) return false;
  if (cancelCurrent) {
    ws.send(JSON.stringify({ type: 'stop', chat_id: chatId }));
  }
  pendingAssistantResponse = true;
  stopRequested = false;
  if (petController) {
    petController.markAssistantPending();
  }
  addMessage('user', displayText);
  selectedSessionId = chatId;
  upsertLocalSession(chatId);
  renderSessions();
  ws.send(JSON.stringify({
    type: 'message',
    content: displayText,
    chat_id: chatId,
    image_path: imagePath || undefined,
  }));
  input.value = '';
  clearPendingImage();
  currentToolGroup = null;
  autoResize();
  renderContextBadge();
  syncSendState();
  saveReconnectSession();
  setTimeout(loadSessions, 600);
  return true;
}

form.addEventListener('submit', (e) => {
  e.preventDefault();
  if (pendingAssistantResponse) {
    if (!ws || ws.readyState !== WebSocket.OPEN) return;
    stopRequested = true;
    ws.send(JSON.stringify({ type: 'stop', chat_id: chatId }));
    syncSendState();
    return;
  }
  const text = input.value.trim();
  sendUserMessage(text, false);
});

input.addEventListener('keydown', (e) => {
  if (e.key === 'Enter' && !e.shiftKey) {
    e.preventDefault();
    const text = input.value.trim();
    if (pendingAssistantResponse && text) {
      sendUserMessage(text, true);
      return;
    }
    form.requestSubmit();
  }
});

input.addEventListener('input', () => {
  autoResize();
  if (petController) {
    petController.noteDraftActivity();
  }
});
input.addEventListener('paste', (e) => {
  if (imageUploadBusy) return;
  const file = findPastedImageFile(e.clipboardData);
  if (file) {
    e.preventDefault();
    uploadImageFile(file);
  }
});
messages.addEventListener('scroll', () => {
  stickToBottom = isNearBottom();
  syncScrollButton();
});
scrollToBottomBtn.addEventListener('click', () => {
  scrollToBottom(true);
  stickToBottom = true;
  syncScrollButton();
});
if (themeSelect) {
  themeSelect.addEventListener('change', (e) => applyTheme(e.target.value));
}
if (terminalSecuritySelect) {
  terminalSecuritySelect.addEventListener('change', (e) => setTerminalSecurityLevel(e.target.value));
}
if (attachmentRemove) {
  attachmentRemove.addEventListener('click', () => clearPendingImage());
}
if (newSessionBtn) {
  newSessionBtn.addEventListener('click', () => startNewSession());
}
if (sessionList) {
  sessionList.addEventListener('click', (e) => {
    const deleteButton = e.target.closest('.session-delete');
    if (deleteButton) {
      e.stopPropagation();
      deleteSession(deleteButton.dataset.chatId);
      return;
    }

    const item = e.target.closest('.session-open');
    if (item) {
      switchSession(item.dataset.chatId);
    }
  });
}
if (petChooserButton) {
  petChooserButton.addEventListener('click', () => togglePetChooser());
}
if (petChooserMenu) {
  petChooserMenu.addEventListener('click', (e) => {
    const choice = e.target.closest('.pet-choice');
    if (!choice) return;
    selectPetPackage(choice.dataset.packageId);
  });
}
document.addEventListener('pointerdown', (e) => {
  if (!petChooserOpen || !petChooser) return;
  if (petChooser.contains(e.target)) return;
  setPetChooserOpen(false);
});
document.addEventListener('keydown', (e) => {
  if (e.key === 'Escape' && petChooserOpen) {
    setPetChooserOpen(false);
    if (petChooserButton) petChooserButton.focus();
  }
});
interactiveSubmit.addEventListener('click', () => submitInteractivePrompt(false));
interactiveCancel.addEventListener('click', () => submitInteractivePrompt(true));
interactiveInput.addEventListener('keydown', (e) => {
  if (e.key === 'Enter') {
    e.preventDefault();
    submitInteractivePrompt(false);
  }
  if (e.key === 'Escape') {
    e.preventDefault();
    submitInteractivePrompt(true);
  }
});

if (reconnectToastAction) {
  reconnectToastAction.addEventListener('click', () => ensureReconnectController()?.handleReconnect?.());
}

window.addEventListener('beforeunload', () => saveReconnectSession());

function saveReconnectSession() {
  try {
    writeStorage(RECONNECT_SESSION_KEY, JSON.stringify({
      chat_id: chatId,
      last_seq: lastMessageSeq,
      timestamp: Date.now(),
      has_messages: messages.childElementCount > 0,
    }));
  } catch (_) {}
}

async function loadAgentList() {
  const select = document.getElementById('agentSelect');
  if (!select) return;

  try {
    const resp = await fetch('/api/agents', { cache: 'no-store' });
    if (!resp.ok) return;
    const data = await resp.json();
    const agents = data?.agents;
    if (!Array.isArray(agents) || !agents.length) return;

    /* 保存当前选中值 */
    const saved = localStorage.getItem('agent_id') || 'boss';

    /* 重建选项列表 */
    select.innerHTML = '';
    for (const a of agents) {
      const opt = document.createElement('option');
      opt.value = a.id;
      opt.textContent = a.name;
      select.appendChild(opt);
    }

    /* 恢复选中 */
    const exists = agents.some(function(a) { return a.id === saved; });
    select.value = exists ? saved : 'boss';
  } catch (_) {
    /* 网络错误时保持 HTML 中硬编码的 Boss/HR 选项 */
  }
}

/* ── Agent 详情面板：查看/增删工具和技能 ── */

let agentDetailAgentId = '';
let agentDetailPanel = null;
let agentDetailName = null;
let agentToolsetTags = null;
let agentCoreSkillsTags = null;

function ensureAgentDetailRefs() {
  if (agentDetailPanel) return;
  agentDetailPanel = document.getElementById('agentDetail');
  agentDetailName = document.getElementById('agentDetailName');
  agentToolsetTags = document.getElementById('agentToolsetTags');
  agentCoreSkillsTags = document.getElementById('agentCoreSkillsTags');
  document.getElementById('agentDetailClose')?.addEventListener('click', function() {
    if (agentDetailPanel) agentDetailPanel.hidden = true;
  });
  document.getElementById('agentAddToolForm')?.addEventListener('submit', function(e) {
    e.preventDefault();
    var input = document.getElementById('agentAddToolInput');
    var val = (input?.value || '').trim();
    if (val) { addAgentTag('toolset', val); if (input) input.value = ''; }
  });
  document.getElementById('agentAddSkillForm')?.addEventListener('submit', function(e) {
    e.preventDefault();
    var input = document.getElementById('agentAddSkillInput');
    var val = (input?.value || '').trim();
    if (val) { addAgentTag('core_skills', val); if (input) input.value = ''; }
  });
  /* 删除按钮 */
  document.getElementById('agentDeleteBtn')?.addEventListener('click', function() {
    showRetireModal();
  });
  /* 交接弹窗 */
  document.getElementById('retireCancel')?.addEventListener('click', hideRetireModal);
  document.getElementById('retireConfirm')?.addEventListener('click', confirmRetire);
}

function renderTags(container, text, field) {
  if (!container) return;
  container.innerHTML = '';
  var items = (text || '').split(/\\s+/).filter(Boolean);
  for (var i = 0; i < items.length; i++) {
    var tag = document.createElement('span');
    tag.className = 'tag-chip';
    tag.textContent = items[i];
    var rm = document.createElement('button');
    rm.className = 'tag-remove';
    rm.textContent = '×';
    rm.title = '移除 ' + items[i];
    rm.setAttribute('aria-label', '移除 ' + items[i]);
    rm.addEventListener('click', (function(tagName, f) {
      return function() { removeAgentTag(f, tagName); };
    })(items[i], field));
    tag.appendChild(rm);
    container.appendChild(tag);
  }
}

async function loadAgentDetail(agentId) {
  ensureAgentDetailRefs();
  if (!agentDetailPanel) return;
  if (!agentId || agentId === 'boss') {
    agentDetailPanel.hidden = true;
    return;
  }
  try {
    var resp = await fetch('/api/agents/detail?agent_id=' + encodeURIComponent(agentId), { cache: 'no-store' });
    if (!resp.ok) { agentDetailPanel.hidden = true; return; }
    var data = await resp.json();
    agentDetailAgentId = data.agent_id || agentId;
    if (agentDetailName) agentDetailName.textContent = data.name || agentId;
    renderTags(agentToolsetTags, data.toolset || '', 'toolset');
    renderTags(agentCoreSkillsTags, data.core_skills || '', 'core_skills');
    agentDetailPanel.hidden = false;
  } catch (_) {
    agentDetailPanel.hidden = true;
  }
}

async function saveAgentDetail() {
  if (!agentDetailAgentId) return;
  try {
    var tools = [];
    var chips = agentToolsetTags?.querySelectorAll('.tag-chip');
    if (chips) for (var i = 0; i < chips.length; i++) {
      var t = chips[i].textContent.replace(/×/g, '').trim();
      if (t) tools.push(t);
    }
    var skills = [];
    chips = agentCoreSkillsTags?.querySelectorAll('.tag-chip');
    if (chips) for (var j = 0; j < chips.length; j++) {
      var s = chips[j].textContent.replace(/×/g, '').trim();
      if (s) skills.push(s);
    }
    await fetch('/api/agents/update', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        agent_id: agentDetailAgentId,
        toolset: tools.join(' '),
        core_skills: skills.join(' '),
      }),
    });
  } catch (_) {}
}

async function addAgentTag(field, value) {
  if (!value || !agentDetailAgentId) return;
  var container = field === 'toolset' ? agentToolsetTags : agentCoreSkillsTags;
  if (!container) return;
  /* 去重 */
  var existing = container.querySelectorAll('.tag-chip');
  for (var i = 0; i < existing.length; i++) {
    var t = existing[i].textContent.replace(/×/g, '').trim();
    if (t === value) return;
  }
  /* 添加 tag */
  var tag = document.createElement('span');
  tag.className = 'tag-chip';
  tag.textContent = value;
  var rm = document.createElement('button');
  rm.className = 'tag-remove';
  rm.textContent = '×';
  rm.addEventListener('click', function() { removeAgentTag(field, value); });
  tag.appendChild(rm);
  container.appendChild(tag);
  await saveAgentDetail();
}

async function removeAgentTag(field, value) {
  if (!agentDetailAgentId) return;
  var container = field === 'toolset' ? agentToolsetTags : agentCoreSkillsTags;
  if (!container) return;
  var chips = container.querySelectorAll('.tag-chip');
  for (var i = 0; i < chips.length; i++) {
    var t = chips[i].textContent.replace(/×/g, '').trim();
    if (t === value) { chips[i].remove(); break; }
  }
  await saveAgentDetail();
}

/* ── Agent 删除 + 交接 ── */

function showRetireModal() {
  var modal = document.getElementById('retireModal');
  var targetSelect = document.getElementById('retireTargetSelect');
  if (!modal || !targetSelect || !agentDetailAgentId) return;

  /* 填充目标 Agent 列表（排除自己和 boss） */
  var agentSelect = document.getElementById('agentSelect');
  targetSelect.innerHTML = '';
  if (agentSelect) {
    var options = agentSelect.options;
    for (var i = 0; i < options.length; i++) {
      if (options[i].value !== agentDetailAgentId && options[i].value !== 'boss') {
        var opt = document.createElement('option');
        opt.value = options[i].value;
        opt.textContent = options[i].textContent;
        targetSelect.appendChild(opt);
      }
    }
  }
  /* 默认选第一个 */
  if (targetSelect.options.length > 0) targetSelect.selectedIndex = 0;

  modal.hidden = false;
  modal.removeAttribute('aria-hidden');
  modal.classList.add('show');
}

function hideRetireModal() {
  var modal = document.getElementById('retireModal');
  if (modal) { modal.hidden = true; modal.setAttribute('aria-hidden', 'true'); modal.classList.remove('show'); }
}

async function confirmRetire() {
  var targetSelect = document.getElementById('retireTargetSelect');
  var transferSkills = document.getElementById('retireTransferSkills');
  var transferTools = document.getElementById('retireTransferTools');
  var targetId = targetSelect?.value;
  if (!targetId || !agentDetailAgentId) { hideRetireModal(); return; }

  try {
    await fetch('/api/agents/retire', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        agent_id: agentDetailAgentId,
        target_agent_id: targetId,
        transfer_skills: transferSkills?.checked ? 'true' : 'false',
        transfer_tools: transferTools?.checked ? 'true' : 'false',
        reason: '用户手动删除',
      }),
    });
  } catch (_) {}

  hideRetireModal();
  /* 隐藏详情面板 */
  var panel = document.getElementById('agentDetail');
  if (panel) panel.hidden = true;
  agentDetailAgentId = '';
  /* 刷新 Agent 列表 */
  loadAgentList();
}

/* agentSelect change → 加载详情 */
window._onAgentSelectChange = function(agentId) {
  loadAgentDetail(agentId);
};

async function initApp() {
  /* 1. 立即建 WebSocket 连接，不依赖任何 fetch */
  ensureConnectionStatePresenter()?.setStatus?.('connecting');
  ensureSocketConnection();

  /* 2. 同步 UI 初始化 */
  applyTheme(storedTheme);
  autoResize();
  syncEmptyState();
  syncEmptyComposerLayout();
  syncScrollButton();
  syncSendState();

  /* 3. 异步加载数据（不阻塞） */
  refreshUiConfig().then(() => {
    activePetPackageId = resolveInitialPetPackageId(uiConfig);
    renderPetChooser(activePetPackageId);
    attachPetController();
    syncTerminalSecurityControl();
  }).catch(function() {});

  loadAgentList();
  loadSessions();
  refreshContextStats();

  /* 4. 异步恢复上次会话 */
  try {
    const savedStr = readStorage(RECONNECT_SESSION_KEY, '');
    if (savedStr) {
      const saved = JSON.parse(savedStr);
      if (saved && saved.chat_id && saved.has_messages) {
        setActiveChatId(saved.chat_id);
        selectedSessionId = saved.chat_id;
        lastMessageSeq = Number(saved.last_seq) || 0;
        await ensureSessionStateRuntime()?.restoreSessionViewState?.(chatId, {
          requireCurrentChat: true,
          applyHistory: true,
          renderEmptyHistory: false,
          renderSessions: false,
          saveReconnect: false,
          refreshContextStats: false,
          restoreSubagent: true,
          showReconnectToast: true,
        });
      }
    }
  } catch (_) {}
}

window.addEventListener('error', (event) => {
  const message = String(event?.message || '').trim();
  if (!message) return;
  lastConnectionErrorText = message;
  if (connectionState !== 'connected') {
    ensureConnectionStatePresenter()?.setStatus?.(false);
  }
});

initApp();
