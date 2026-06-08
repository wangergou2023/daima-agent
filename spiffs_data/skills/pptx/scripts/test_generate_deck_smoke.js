const fs = require('fs');
const os = require('os');
const path = require('path');
const { spawnSync } = require('child_process');

const scriptPath = path.join(__dirname, 'generate_deck.js');
const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), 'pptx-generate-smoke-'));
const specPath = path.join(tmpDir, 'deck_spec.json');
const outputPath = path.join(tmpDir, 'trigonometry_intro.pptx');

const spec = {
  title: '三角函数入门',
  topic: '三角函数',
  audience: '高中一年级学生',
  language: 'zh-CN',
  theme: 'math',
  slides: [
    {
      title: '三角函数是什么',
      body: ['用角度描述直角三角形边长之间的比例', '把几何问题转化为可计算的函数关系'],
      visual: 'unit-circle'
    },
    {
      title: '三个基本函数',
      body: ['sin 表示对边 / 斜边', 'cos 表示邻边 / 斜边', 'tan 表示对边 / 邻边'],
      visual: 'formula-cards'
    },
    {
      title: '特殊角速查',
      body: ['30°, 45°, 60° 是最常见的计算入口', '先记住比例关系，再处理复杂角度'],
      visual: 'chart'
    },
    {
      title: '单位圆视角',
      body: ['角度对应圆上的点', '横坐标是 cos，纵坐标是 sin'],
      visual: 'unit-circle'
    },
    {
      title: '学习路径',
      body: ['先掌握定义', '再熟悉图像', '最后应用到建模问题'],
      visual: 'timeline'
    }
  ]
};

fs.writeFileSync(specPath, JSON.stringify(spec, null, 2));

const result = spawnSync(process.execPath, [scriptPath, specPath, outputPath], {
  encoding: 'utf8',
  env: { ...process.env, NODE_PATH: '' }
});

if (result.status !== 0) {
  process.stderr.write(result.stdout);
  process.stderr.write(result.stderr);
  process.exit(result.status || 1);
}

const stat = fs.statSync(outputPath);
if (stat.size <= 0) {
  throw new Error(`Expected non-empty PPTX at ${outputPath}`);
}

const unzipResult = spawnSync('unzip', ['-t', outputPath], { encoding: 'utf8' });
if (unzipResult.status !== 0) {
  process.stderr.write(unzipResult.stdout);
  process.stderr.write(unzipResult.stderr);
  process.exit(unzipResult.status || 1);
}

console.log(`Smoke PPTX generated: ${outputPath} (${stat.size} bytes)`);
