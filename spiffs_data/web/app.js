const messages = document.getElementById('messages');
const chatShell = document.querySelector('.chat-shell');
const input = document.getElementById('input');
const form = document.getElementById('composer');
const statusEl = document.getElementById('status');
const dot = document.getElementById('dot');
const ctxBadge = document.getElementById('ctxBadge');
const themeSelect = document.getElementById('themeSelect');
const sudoModal = document.getElementById('sudoModal');
const sudoPrompt = document.getElementById('sudoPrompt');
const sudoInput = document.getElementById('sudoInput');
const sudoCancel = document.getElementById('sudoCancel');
const sudoSubmit = document.getElementById('sudoSubmit');
const sendBtn = document.getElementById('sendBtn');
const scrollToBottomBtn = document.getElementById('scrollToBottom');
const petDock = document.getElementById('petDock');
const petChooser = document.getElementById('petChooser');
const petChooserButton = document.getElementById('petChooserButton');
const petChooserLabel = document.getElementById('petChooserLabel');
const petChooserMenu = document.getElementById('petChooserMenu');
const petButton = document.getElementById('petButton');
const petRunner = document.getElementById('petRunner');
const petSprite = document.getElementById('petSprite');
const petBubble = document.getElementById('petBubble');

const CHAT_ID_KEY = 'daima_chat_id';
const THEME_KEY = 'daima_theme';
const PET_PACKAGE_KEY = 'daima_pet_package_id';
const NEAR_BOTTOM_PX = 72;
const EMPTY_MULTILINE_HEIGHT = 84;
const DEFAULT_UI_CONFIG = Object.freeze({
  pet: {
    default_package_id: 'guga.codex-pet',
    packages: [],
  },
});

const storedId = localStorage.getItem(CHAT_ID_KEY);
const storedTheme = localStorage.getItem(THEME_KEY) || 'warm';
const chatId = storedId || `web_${Math.random().toString(36).slice(2, 8)}`;
if (!storedId) {
  localStorage.setItem(CHAT_ID_KEY, chatId);
} else {
  localStorage.setItem(CHAT_ID_KEY, storedId);
}

let ws;
let reconnectTimer;
let pingTimer;
let statsTimer;
let baseStats = { model: 'unknown', used_tokens: 0, context_limit_tokens: 0 };
let pendingSudoRequestId = '';
let stickToBottom = true;
let currentToolGroup = null;
let isConnected = false;
let pendingAssistantResponse = false;
let uiConfig = DEFAULT_UI_CONFIG;
let petController = null;
let availablePetPackages = normalizePetPackages(DEFAULT_UI_CONFIG);
let activePetPackageId = '';
let petChooserOpen = false;

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
  const storedPackageId = localStorage.getItem(PET_PACKAGE_KEY);
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
  if (!window.DaimaPet || typeof window.DaimaPet.createPetController !== 'function') {
    return null;
  }
  return window.DaimaPet.createPetController({
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
        };
      }
    }
  } catch (_) {}
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
  localStorage.setItem(PET_PACKAGE_KEY, packageId);
  renderPetChooser(activePetPackageId);
  attachPetController();
}

function applyTheme(theme) {
  const next = theme || 'warm';
  document.body.dataset.theme = next;
  if (themeSelect) themeSelect.value = next;
  localStorage.setItem(THEME_KEY, next);
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
  sendBtn.disabled = !isConnected || !input.value.trim();
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

function makeMessageNode(role, text) {
  const row = document.createElement('article');
  row.className = `message-row ${role}`;

  const card = document.createElement('div');
  card.className = `message-card ${role}`;

  if (role === 'assistant') {
    const label = document.createElement('div');
    label.className = 'message-label';
    label.textContent = 'Daima';
    card.appendChild(label);
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
    return;
  }
  currentToolGroup = null;
  appendNode(makeMessageNode(role, text));
}

function openSudoPrompt(requestId, promptText) {
  pendingSudoRequestId = requestId || '';
  sudoPrompt.textContent = promptText || 'A command needs elevated privileges.';
  sudoInput.value = '';
  sudoModal.classList.add('show');
  setTimeout(() => sudoInput.focus(), 30);
}

function closeSudoPrompt() {
  sudoModal.classList.remove('show');
  pendingSudoRequestId = '';
  sudoInput.value = '';
}

function submitSudoPrompt(cancelled) {
  if (!ws || ws.readyState !== WebSocket.OPEN || !pendingSudoRequestId) return;
  ws.send(JSON.stringify({
    type: 'sudo_password',
    chat_id: chatId,
    request_id: pendingSudoRequestId,
    password: cancelled ? '' : sudoInput.value,
    cancelled: !!cancelled,
  }));
  closeSudoPrompt();
}

function setStatus(online) {
  isConnected = online;
  statusEl.textContent = online ? '已连接' : '未连接';
  dot.classList.toggle('on', online);
  syncSendState();
}

function connect() {
  const proto = location.protocol === 'https:' ? 'wss' : 'ws';
  ws = new WebSocket(`${proto}://${location.host}`);
  ws.onopen = () => {
    setStatus(true);
    if (petController) {
      petController.handleSocketOpen();
    }
    refreshContextStats();
    if (pingTimer) clearInterval(pingTimer);
    pingTimer = setInterval(() => {
      if (ws && ws.readyState === WebSocket.OPEN) {
        ws.send(JSON.stringify({ type: 'ping', ts: Date.now(), chat_id: chatId }));
      }
    }, 15000);
    if (statsTimer) clearInterval(statsTimer);
    statsTimer = setInterval(() => refreshContextStats(), 8000);
  };
  ws.onclose = () => {
    setStatus(false);
    if (pingTimer) {
      clearInterval(pingTimer);
      pingTimer = null;
    }
    if (statsTimer) {
      clearInterval(statsTimer);
      statsTimer = null;
    }
    if (!reconnectTimer) {
      reconnectTimer = setTimeout(() => {
        reconnectTimer = null;
        connect();
      }, 1500);
    }
  };
  ws.onerror = () => setStatus(false);
  ws.onmessage = (evt) => {
    try {
      const data = JSON.parse(evt.data);
      if (data.type === 'pong') return;
      if (data.type === 'sudo_request') {
        openSudoPrompt(data.request_id, data.prompt || 'Please enter your sudo password to continue this command.');
        return;
      }
      if (data.type === 'tool' && data.content) {
        if (petController) {
          petController.handleToolMessage();
        }
        addMessage('tool', data.content);
        return;
      }
      if (data.type === 'pet_response') {
        if (petController) {
          petController.handlePetResponse(data);
        }
        return;
      }
      if (data.type === 'response' && data.content) {
        pendingAssistantResponse = false;
        const assistantText = petController
          ? petController.consumeAssistantText(data.content)
          : data.content;
        addMessage('assistant', assistantText);
        refreshContextStats();
        return;
      }
    } catch (_) {
      pendingAssistantResponse = false;
      const assistantText = petController
        ? petController.consumeAssistantText(evt.data)
        : evt.data;
      addMessage('assistant', assistantText);
    }
  };
}

form.addEventListener('submit', (e) => {
  e.preventDefault();
  const text = input.value.trim();
  if (!text || !ws || ws.readyState !== WebSocket.OPEN) return;
  pendingAssistantResponse = true;
  if (petController) {
    petController.markAssistantPending();
  }
  addMessage('user', text);
  ws.send(JSON.stringify({ type: 'message', content: text, chat_id: chatId }));
  input.value = '';
  currentToolGroup = null;
  autoResize();
  renderContextBadge();
});

input.addEventListener('keydown', (e) => {
  if (e.key === 'Enter' && !e.shiftKey) {
    e.preventDefault();
    form.requestSubmit();
  }
});

input.addEventListener('input', () => {
  autoResize();
  if (petController) {
    petController.noteDraftActivity();
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
sudoSubmit.addEventListener('click', () => submitSudoPrompt(false));
sudoCancel.addEventListener('click', () => submitSudoPrompt(true));
sudoInput.addEventListener('keydown', (e) => {
  if (e.key === 'Enter') {
    e.preventDefault();
    submitSudoPrompt(false);
  }
  if (e.key === 'Escape') {
    e.preventDefault();
    submitSudoPrompt(true);
  }
});

async function initApp() {
  await refreshUiConfig();
  activePetPackageId = resolveInitialPetPackageId(uiConfig);
  renderPetChooser(activePetPackageId);
  attachPetController();

  setStatus(false);
  applyTheme(storedTheme);
  autoResize();
  refreshContextStats();
  syncEmptyState();
  syncEmptyComposerLayout();
  syncScrollButton();
  syncSendState();
  connect();
}

initApp();
