#!/usr/bin/env node

const fs = require('fs');
const path = require('path');

function requirePptxGen() {
  const candidates = [
    'pptxgenjs',
    path.resolve(__dirname, '../../../../workspace/node_modules/pptxgenjs'),
    '/home/wangergou/.daima/spiffs_data/workspace/node_modules/pptxgenjs'
  ];
  const errors = [];
  for (const candidate of candidates) {
    try {
      return require(candidate);
    } catch (err) {
      errors.push(`${candidate}: ${err.code || err.message}`);
    }
  }
  throw new Error(`Cannot load pptxgenjs. Install it in Daima workspace with: npm install pptxgenjs\n${errors.join('\n')}`);
}

const pptxgen = requirePptxGen();

const pptx = new pptxgen();
const ShapeType = pptx.ShapeType || {};

const W = 13.333;
const H = 7.5;
const COLORS = {
  ink: '17202A',
  muted: '53616F',
  paper: 'F7F4EA',
  panel: 'FFFFFF',
  deep: '17324D',
  teal: '118A8A',
  coral: 'D95D39',
  gold: 'F2C14E',
  mint: 'D8EAD7',
  line: 'D7DED9'
};
const FONT_HEAD = 'Microsoft YaHei';
const FONT_BODY = 'Microsoft YaHei';

function usage() {
  console.error('Usage: node generate_deck.js deck_spec.json output.pptx');
  process.exit(2);
}

function readSpec(filePath) {
  let parsed;
  try {
    parsed = JSON.parse(fs.readFileSync(filePath, 'utf8'));
  } catch (err) {
    throw new Error(`Cannot read deck spec ${filePath}: ${err.message}`);
  }
  if (!parsed || typeof parsed !== 'object') {
    throw new Error('deck_spec.json must be a JSON object');
  }
  return parsed;
}

function text(value, fallback) {
  if (typeof value !== 'string') return fallback;
  const trimmed = value.trim();
  return trimmed || fallback;
}

function array(value) {
  if (!Array.isArray(value)) return [];
  return value.map((item) => String(item).trim()).filter(Boolean);
}

function normalizeSlides(spec) {
  const title = text(spec.title, text(spec.topic, '主题演示'));
  const topic = text(spec.topic, title);
  const audience = text(spec.audience, '学习者');
  const provided = Array.isArray(spec.slides) ? spec.slides : [];
  const slides = provided.map((slide, index) => ({
    title: text(slide && slide.title, `${topic} ${index + 1}`),
    body: array(slide && (slide.body || slide.bullets || slide.points)),
    visual: text(slide && slide.visual, '')
  })).filter((slide) => slide.title || slide.body.length);

  const defaults = [
    {
      title: `${topic}是什么`,
      body: [`面向${audience}，先建立直观概念`, '把定义、图像和应用连接起来理解'],
      visual: 'concept'
    },
    {
      title: '核心概念',
      body: ['抓住最重要的三个关键词', '用一个例子把抽象关系变成可计算步骤'],
      visual: 'formula-cards'
    },
    {
      title: '关键公式',
      body: ['先记住基础形式，再理解适用条件', '计算时关注单位、符号和边界'],
      visual: 'formula-cards'
    },
    {
      title: '图像与规律',
      body: ['观察变化趋势和周期', '把图像特征对应到实际含义'],
      visual: 'chart'
    },
    {
      title: '常见误区',
      body: ['不要只背结论，要能解释来源', '遇到复杂题先回到定义'],
      visual: 'comparison'
    },
    {
      title: '练习路径',
      body: ['定义题用于打基础', '综合题用于训练迁移', '应用题用于建立模型意识'],
      visual: 'timeline'
    }
  ];

  for (const item of defaults) {
    if (slides.length >= 5) break;
    slides.push(item);
  }
  return slides.slice(0, 8);
}

function addRect(slide, opts) {
  slide.addShape(ShapeType.rect || 'rect', opts);
}

function addEllipse(slide, opts) {
  slide.addShape(ShapeType.ellipse || 'ellipse', opts);
}

function addLine(slide, opts) {
  slide.addShape(ShapeType.line || 'line', opts);
}

function addHeader(slide, title, index) {
  slide.background = { color: COLORS.paper };
  addRect(slide, { x: 0, y: 0, w: W, h: 0.18, fill: { color: COLORS.teal }, line: { color: COLORS.teal } });
  slide.addText(title, {
    x: 0.62, y: 0.42, w: 8.7, h: 0.5, margin: 0,
    fontFace: FONT_HEAD, fontSize: 25, bold: true, color: COLORS.ink,
    fit: 'shrink'
  });
  slide.addText(String(index).padStart(2, '0'), {
    x: 11.65, y: 0.38, w: 0.9, h: 0.42, margin: 0,
    fontFace: FONT_BODY, fontSize: 16, bold: true, color: COLORS.teal,
    align: 'right'
  });
}

function addBody(slide, body, x, y, w, h) {
  const lines = body.length ? body : ['梳理概念', '观察规律', '完成应用'];
  slide.addText(lines.map((item) => ({ text: item, options: { breakLine: true } })), {
    x, y, w, h, margin: 0.08,
    fontFace: FONT_BODY, fontSize: 15.5, color: COLORS.ink,
    breakLine: false, fit: 'shrink',
    bullet: { type: 'ul' },
    paraSpaceAfterPt: 8
  });
}

function addTitleSlide(spec, slideCount) {
  const slide = pptx.addSlide();
  slide.background = { color: COLORS.deep };
  addRect(slide, { x: 0, y: 5.55, w: W, h: 1.95, fill: { color: COLORS.teal, transparency: 8 }, line: { color: COLORS.teal } });
  addEllipse(slide, { x: 8.9, y: 0.75, w: 2.2, h: 2.2, fill: { color: COLORS.gold, transparency: 10 }, line: { color: COLORS.gold } });
  addEllipse(slide, { x: 9.35, y: 1.2, w: 1.3, h: 1.3, fill: { color: COLORS.deep, transparency: 0 }, line: { color: COLORS.deep } });
  addLine(slide, { x: 9.99, y: 1.86, w: 1.45, h: 0, line: { color: COLORS.paper, width: 2 } });
  addLine(slide, { x: 9.99, y: 1.86, w: 0, h: -1.45, line: { color: COLORS.paper, width: 2 } });
  slide.addText(text(spec.title, text(spec.topic, '主题演示')), {
    x: 0.75, y: 1.55, w: 7.9, h: 0.75, margin: 0,
    fontFace: FONT_HEAD, fontSize: 38, bold: true, color: 'FFFFFF',
    fit: 'shrink'
  });
  slide.addText(`${text(spec.topic, '核心主题')} · ${text(spec.audience, '学习者')} · ${slideCount}页`, {
    x: 0.8, y: 2.55, w: 7.4, h: 0.42, margin: 0,
    fontFace: FONT_BODY, fontSize: 16, color: 'EAF3F0',
    fit: 'shrink'
  });
  slide.addText('概念 / 公式 / 图像 / 应用', {
    x: 0.8, y: 5.95, w: 6.8, h: 0.42, margin: 0,
    fontFace: FONT_BODY, fontSize: 17, bold: true, color: 'FFFFFF'
  });
}

function addFormulaCards(slide, labels) {
  const cards = labels.length ? labels.slice(0, 3) : ['sin = 对边 / 斜边', 'cos = 邻边 / 斜边', 'tan = 对边 / 邻边'];
  cards.forEach((label, i) => {
    const y = 1.5 + i * 1.28;
    const color = [COLORS.teal, COLORS.coral, COLORS.gold][i % 3];
    addRect(slide, { x: 7.55, y, w: 4.55, h: 0.92, fill: { color: COLORS.panel }, line: { color: COLORS.line, width: 1 } });
    addEllipse(slide, { x: 7.78, y: y + 0.22, w: 0.48, h: 0.48, fill: { color }, line: { color } });
    slide.addText(label, {
      x: 8.48, y: y + 0.2, w: 3.25, h: 0.38, margin: 0,
      fontFace: FONT_BODY, fontSize: 14, bold: true, color: COLORS.ink,
      fit: 'shrink'
    });
  });
}

function addUnitCircle(slide) {
  addEllipse(slide, { x: 7.72, y: 1.35, w: 3.65, h: 3.65, fill: { color: 'FFFFFF', transparency: 100 }, line: { color: COLORS.teal, width: 2 } });
  addLine(slide, { x: 7.45, y: 3.18, w: 4.2, h: 0, line: { color: COLORS.muted, width: 1 } });
  addLine(slide, { x: 9.55, y: 1.08, w: 0, h: 4.2, line: { color: COLORS.muted, width: 1 } });
  addLine(slide, { x: 9.55, y: 3.18, w: 1.36, h: -1.1, line: { color: COLORS.coral, width: 3 } });
  addEllipse(slide, { x: 10.78, y: 1.95, w: 0.22, h: 0.22, fill: { color: COLORS.coral }, line: { color: COLORS.coral } });
  slide.addText('(cos θ, sin θ)', {
    x: 9.92, y: 1.62, w: 2.0, h: 0.28, margin: 0,
    fontFace: FONT_BODY, fontSize: 12, color: COLORS.coral, bold: true,
    fit: 'shrink'
  });
  slide.addText('θ', {
    x: 10.05, y: 2.82, w: 0.35, h: 0.28, margin: 0,
    fontFace: FONT_HEAD, fontSize: 14, color: COLORS.ink, bold: true
  });
}

function addChart(slide) {
  const baseX = 7.25;
  const baseY = 5.05;
  addLine(slide, { x: baseX, y: baseY, w: 4.9, h: 0, line: { color: COLORS.muted, width: 1 } });
  addLine(slide, { x: baseX, y: 1.55, w: 0, h: 3.5, line: { color: COLORS.muted, width: 1 } });
  const points = [
    [7.45, 3.55], [8.05, 2.65], [8.65, 2.05], [9.25, 2.65],
    [9.85, 3.55], [10.45, 4.45], [11.05, 5.0], [11.65, 4.45]
  ];
  for (let i = 0; i < points.length - 1; i++) {
    addLine(slide, {
      x: points[i][0], y: points[i][1],
      w: points[i + 1][0] - points[i][0], h: points[i + 1][1] - points[i][1],
      line: { color: COLORS.teal, width: 2.5 }
    });
  }
  slide.addText('周期变化', {
    x: 8.25, y: 1.18, w: 2.9, h: 0.34, margin: 0,
    fontFace: FONT_BODY, fontSize: 13, bold: true, color: COLORS.teal,
    align: 'center'
  });
}

function addTimeline(slide, items) {
  const labels = items.slice(0, 4);
  while (labels.length < 4) labels.push(['理解定义', '记忆公式', '观察图像', '解决问题'][labels.length]);
  addLine(slide, { x: 7.4, y: 3.15, w: 4.55, h: 0, line: { color: COLORS.teal, width: 2 } });
  labels.forEach((label, i) => {
    const x = 7.35 + i * 1.55;
    addEllipse(slide, { x, y: 2.88, w: 0.55, h: 0.55, fill: { color: i === 0 ? COLORS.coral : COLORS.teal }, line: { color: i === 0 ? COLORS.coral : COLORS.teal } });
    slide.addText(String(i + 1), {
      x: x + 0.12, y: 3.01, w: 0.3, h: 0.2, margin: 0,
      fontFace: FONT_BODY, fontSize: 10, bold: true, color: 'FFFFFF',
      align: 'center'
    });
    slide.addText(label, {
      x: x - 0.28, y: 3.65, w: 1.12, h: 0.62, margin: 0,
      fontFace: FONT_BODY, fontSize: 10.5, color: COLORS.ink,
      align: 'center', fit: 'shrink'
    });
  });
}

function addComparison(slide) {
  addRect(slide, { x: 7.2, y: 1.55, w: 2.25, h: 3.65, fill: { color: 'FFFFFF' }, line: { color: COLORS.line } });
  addRect(slide, { x: 9.8, y: 1.55, w: 2.25, h: 3.65, fill: { color: COLORS.mint }, line: { color: COLORS.line } });
  slide.addText('常见做法', { x: 7.55, y: 1.9, w: 1.5, h: 0.28, margin: 0, fontFace: FONT_BODY, fontSize: 13, bold: true, color: COLORS.coral, align: 'center' });
  slide.addText('推荐做法', { x: 10.15, y: 1.9, w: 1.5, h: 0.28, margin: 0, fontFace: FONT_BODY, fontSize: 13, bold: true, color: COLORS.teal, align: 'center' });
  slide.addText('背结论', { x: 7.55, y: 3.1, w: 1.5, h: 0.3, margin: 0, fontFace: FONT_BODY, fontSize: 18, bold: true, color: COLORS.ink, align: 'center' });
  slide.addText('看关系', { x: 10.15, y: 3.1, w: 1.5, h: 0.3, margin: 0, fontFace: FONT_BODY, fontSize: 18, bold: true, color: COLORS.ink, align: 'center' });
}

function addConceptVisual(slide, slideInfo) {
  const visual = slideInfo.visual.toLowerCase();
  if (visual.includes('circle') || visual.includes('unit') || visual.includes('圆')) {
    addUnitCircle(slide);
  } else if (visual.includes('chart') || visual.includes('graph') || visual.includes('图')) {
    addChart(slide);
  } else if (visual.includes('time') || visual.includes('path') || visual.includes('流程') || visual.includes('路径')) {
    addTimeline(slide, slideInfo.body);
  } else if (visual.includes('compare') || visual.includes('误区')) {
    addComparison(slide);
  } else if (visual.includes('formula') || visual.includes('公式')) {
    addFormulaCards(slide, slideInfo.body);
  } else {
    addRect(slide, { x: 7.55, y: 1.55, w: 4.35, h: 3.65, fill: { color: COLORS.mint }, line: { color: COLORS.line } });
    addEllipse(slide, { x: 8.0, y: 2.08, w: 1.25, h: 1.25, fill: { color: COLORS.teal }, line: { color: COLORS.teal } });
    addEllipse(slide, { x: 10.1, y: 2.08, w: 1.25, h: 1.25, fill: { color: COLORS.gold }, line: { color: COLORS.gold } });
    addLine(slide, { x: 8.95, y: 2.7, w: 1.35, h: 0, line: { color: COLORS.deep, width: 3 } });
    slide.addText('定义 → 图像 → 应用', {
      x: 8.1, y: 4.15, w: 3.6, h: 0.36, margin: 0,
      fontFace: FONT_BODY, fontSize: 15, bold: true, color: COLORS.deep,
      align: 'center'
    });
  }
}

function addContentSlide(slideInfo, index) {
  const slide = pptx.addSlide();
  addHeader(slide, slideInfo.title, index);
  addRect(slide, { x: 0.68, y: 1.34, w: 5.75, h: 4.95, fill: { color: COLORS.panel }, line: { color: COLORS.line, width: 1 } });
  addBody(slide, slideInfo.body, 1.0, 1.75, 4.95, 3.85);
  addConceptVisual(slide, slideInfo);
  slide.addText('关键记忆点', {
    x: 0.92, y: 5.88, w: 1.5, h: 0.22, margin: 0,
    fontFace: FONT_BODY, fontSize: 10.5, bold: true, color: COLORS.teal
  });
}

function addClosingSlide(spec, slides) {
  const slide = pptx.addSlide();
  slide.background = { color: COLORS.deep };
  addRect(slide, { x: 0.75, y: 0.75, w: 11.8, h: 5.95, fill: { color: 'FFFFFF', transparency: 92 }, line: { color: COLORS.teal, width: 2 } });
  slide.addText('总结', {
    x: 0.92, y: 1.12, w: 2.2, h: 0.55, margin: 0,
    fontFace: FONT_HEAD, fontSize: 32, bold: true, color: 'FFFFFF'
  });
  slide.addText(`${text(spec.topic, text(spec.title, '主题'))}的学习重点`, {
    x: 0.98, y: 2.05, w: 5.6, h: 0.4, margin: 0,
    fontFace: FONT_BODY, fontSize: 18, bold: true, color: 'EAF3F0'
  });
  addTimeline(slide, ['概念', '公式', '图像', '应用']);
  slide.addText(slides.slice(0, 3).map((s) => s.title).join(' / '), {
    x: 0.98, y: 5.58, w: 6.5, h: 0.36, margin: 0,
    fontFace: FONT_BODY, fontSize: 13, color: 'EAF3F0',
    fit: 'shrink'
  });
}

async function main() {
  const [, , specArg, outputArg] = process.argv;
  if (!specArg || !outputArg) usage();

  const specPath = path.resolve(specArg);
  const outputPath = path.resolve(outputArg);
  const spec = readSpec(specPath);
  const slides = normalizeSlides(spec);

  pptx.layout = 'LAYOUT_WIDE';
  pptx.author = 'Daima';
  pptx.company = 'Daima';
  pptx.subject = text(spec.topic, 'Generated presentation');
  pptx.title = text(spec.title, text(spec.topic, 'Generated presentation'));
  pptx.lang = text(spec.language, 'zh-CN');
  pptx.theme = {
    headFontFace: FONT_HEAD,
    bodyFontFace: FONT_BODY,
    lang: text(spec.language, 'zh-CN')
  };

  addTitleSlide(spec, slides.length + 2);
  slides.forEach((slide, i) => addContentSlide(slide, i + 1));
  addClosingSlide(spec, slides);

  fs.mkdirSync(path.dirname(outputPath), { recursive: true });
  await pptx.writeFile({ fileName: outputPath });
  console.log(`PPTX generated: ${outputPath}`);
}

main().catch((err) => {
  console.error(err.message);
  process.exit(1);
});
