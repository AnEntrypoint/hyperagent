const esc = (value) =>
  String(value)
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;');

const cell = (value) => String(value).replace(/\|/g, '\\|').replace(/\r?\n/g, ' ');

const rich = (text) => esc(text).replace(/\*\*(.+?)\*\*/g, '<strong>$1</strong>');

export const barChartSvg = ({ title, labels, values, unit }) => {
  const width = 720;
  const height = 280;
  const pad = { top: 40, right: 16, bottom: 60, left: 56 };
  const plotW = width - pad.left - pad.right;
  const plotH = height - pad.top - pad.bottom;
  const max = Math.max(1, ...values);
  const step = plotW / Math.max(1, labels.length);
  const barW = Math.max(2, step * 0.72);
  const bars = values
    .map((value, index) => {
      const h = (value / max) * plotH;
      const x = pad.left + index * step + (step - barW) / 2;
      const y = pad.top + plotH - h;
      return `<rect x="${x.toFixed(1)}" y="${y.toFixed(1)}" width="${barW.toFixed(1)}" height="${h.toFixed(1)}" fill="#001489"><title>${esc(labels[index])}: ${value} ${esc(unit)}</title></rect>`;
    })
    .join('');
  const every = Math.max(1, Math.ceil(labels.length / 14));
  const ticks = labels
    .map((label, index) =>
      index % every === 0
        ? `<text x="${(pad.left + index * step + step / 2).toFixed(1)}" y="${pad.top + plotH + 18}" font-size="10" text-anchor="middle">${esc(label)}</text>`
        : '',
    )
    .join('');
  return (
    `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 ${width} ${height}" width="100%" role="img" aria-label="${esc(title)}" font-family="Arial, Helvetica, sans-serif" fill="#000000">` +
    `<text x="${pad.left}" y="22" font-size="14" font-weight="bold" fill="#001489">${esc(title)}</text>` +
    `<line x1="${pad.left}" y1="${pad.top + plotH}" x2="${width - pad.right}" y2="${pad.top + plotH}" stroke="#000000"/>` +
    `<text x="${pad.left - 8}" y="${pad.top + 4}" font-size="10" text-anchor="end">${max}</text>` +
    `<text x="${pad.left - 8}" y="${pad.top + plotH + 4}" font-size="10" text-anchor="end">0</text>` +
    bars +
    ticks +
    `</svg>`
  );
};

export const chartSvgs = (report) =>
  Object.fromEntries(report.charts.map((chart) => [chart.name, barChartSvg(chart)]));

const markdownBlock = (block) => {
  if (block.type === 'h2') return [`## ${block.text}`];
  if (block.type === 'h3') return [`### ${block.text}`];
  if (block.type === 'p') return [block.text];
  if (block.type === 'note') return [`> ${block.text}`];
  if (block.type === 'ul') return block.items.map((item) => `- ${item}`);
  if (block.type === 'table') {
    return [
      `| ${block.head.map(cell).join(' | ')} |`,
      `| ${block.head.map(() => '---').join(' | ')} |`,
      ...block.rows.map((row) => `| ${row.map(cell).join(' | ')} |`),
    ];
  }
  if (block.type === 'chart') return [`![${cell(block.alt)}](charts/${block.name}.svg)`];
  throw new Error(`unknown block type: ${block.type}`);
};

export const toMarkdown = (report) => {
  const out = [
    `# ${report.title}`,
    '',
    `_${report.meta}_`,
    '',
    `> **BLUF.** ${report.headline}`,
    '>',
    ...report.bluf.map((line) => `> - ${line}`),
    '',
  ];
  for (const block of report.blocks) out.push(...markdownBlock(block), '');
  return `${out.join('\n').trimEnd()}\n`;
};

const htmlBlock = (block, svgs) => {
  if (block.type === 'h2') return `<h2>${esc(block.text)}</h2>`;
  if (block.type === 'h3') return `<h3>${esc(block.text)}</h3>`;
  if (block.type === 'p') return `<p>${rich(block.text)}</p>`;
  if (block.type === 'note') return `<p class="note">${rich(block.text)}</p>`;
  if (block.type === 'ul') return `<ul>${block.items.map((item) => `<li>${rich(item)}</li>`).join('')}</ul>`;
  if (block.type === 'table') {
    const head = block.head.map((heading) => `<th>${esc(heading)}</th>`).join('');
    const body = block.rows
      .map((row) => `<tr>${row.map((value) => `<td>${rich(value)}</td>`).join('')}</tr>`)
      .join('');
    return `<table><thead><tr>${head}</tr></thead><tbody>${body}</tbody></table>`;
  }
  if (block.type === 'chart') return `<figure>${svgs[block.name]}</figure>`;
  throw new Error(`unknown block type: ${block.type}`);
};

const CSS = `
@page { size: A4; margin: 18mm; }
body { font-family: Arial, Helvetica, sans-serif; color: #000000; max-width: 900px; margin: 32px auto; padding: 0 20px; line-height: 1.5; font-size: 14px; }
h1 { color: #001489; font-size: 24px; margin: 0 0 4px; }
h2 { color: #001489; font-size: 18px; margin: 28px 0 8px; padding-bottom: 4px; border-bottom: 2px solid #007749; page-break-after: avoid; }
h3 { color: #001489; font-size: 15px; margin: 18px 0 6px; page-break-after: avoid; }
.meta { color: #444444; font-size: 12px; margin: 0 0 16px; }
.bluf { border-left: 6px solid #FFB81C; background: #F4F6FB; padding: 12px 18px; margin: 16px 0 8px; }
.bluf p { margin: 0 0 6px; }
.bluf ul { margin: 0; padding-left: 18px; }
ul { padding-left: 18px; }
table { border-collapse: collapse; width: 100%; margin: 8px 0 14px; font-size: 13px; }
th { background: #001489; color: #FFFFFF; text-align: left; padding: 6px 8px; }
td { border-bottom: 1px solid #DDDDDD; padding: 6px 8px; vertical-align: top; }
tr { page-break-inside: avoid; }
figure { margin: 10px 0 18px; page-break-inside: avoid; }
figure svg { width: 100%; height: auto; }
`;

export const toHtml = (report, svgs) => {
  const body = report.blocks.map((block) => htmlBlock(block, svgs)).join('\n');
  const bluf = report.bluf.map((line) => `<li>${rich(line)}</li>`).join('');
  return `<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>${esc(report.title)}</title>
<style>${CSS}</style>
</head>
<body>
<h1>${esc(report.title)}</h1>
<p class="meta">${esc(report.meta)}</p>
<section class="bluf"><p><strong>BLUF.</strong> ${rich(report.headline)}</p><ul>${bluf}</ul></section>
${body}
</body>
</html>
`;
};
