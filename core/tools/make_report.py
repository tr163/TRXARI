#!/usr/bin/env python3
"""Run guide_sim and produce a self-contained HTML acceptance report.

Usage: python core/tools/make_report.py [sim-executable] [output.html]
"""
import json
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
BUILD = HERE.parent / "build"


def find_sim():
    if len(sys.argv) > 1:
        return Path(sys.argv[1])
    for name in ("guide_sim.exe", "guide_sim"):
        candidate = BUILD / name
        if candidate.exists():
            return candidate
    raise SystemExit("guide_sim not built; run: cmake --build core/build")


def main():
    sim = find_sim()
    data = json.loads(subprocess.check_output([str(sim)]))
    out = Path(sys.argv[2]) if len(sys.argv) > 2 else BUILD / "guide-report.html"

    html = (TEMPLATE.replace("__DATA__", json.dumps(data, separators=(",", ":")))
            .replace("__SETTLE__", str(data["settle"])))
    out.write_text(html, encoding="utf-8")
    print(out)


TEMPLATE = r"""<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>GuideCore M0 可视化验收</title>
<style>
  :root{--bg:#0f1419;--panel:#161d24;--line:#2a3441;--text:#c9d1d9;--dim:#8b949e;
        --raw:#6e7681;--res:#58a6ff;--corr:#f0883e;--ok:#3fb950;--bad:#f85149}
  *{box-sizing:border-box}
  body{margin:0;background:var(--bg);color:var(--text);font:14px/1.5 system-ui,"Segoe UI",sans-serif}
  header{padding:20px 24px 8px}
  h1{margin:0;font-size:20px;font-weight:600}
  .sub{color:var(--dim);font-size:13px;margin-top:4px}
  main{padding:8px 24px 32px;display:grid;gap:16px;grid-template-columns:1fr 1fr}
  .panel{background:var(--panel);border:1px solid var(--line);border-radius:8px;padding:12px 14px}
  .panel h2{margin:0 0 8px;font-size:13px;font-weight:600;color:var(--dim);letter-spacing:.04em}
  .wide{grid-column:1/-1}
  canvas{display:block;width:100%;height:240px}
  .controls{display:flex;gap:12px;align-items:center;margin-bottom:8px}
  select{background:var(--bg);color:var(--text);border:1px solid var(--line);border-radius:6px;padding:6px 10px;font-size:13px}
  .legend{display:flex;gap:16px;color:var(--dim);font-size:12px;margin-top:6px}
  .legend i{display:inline-block;width:18px;height:2px;vertical-align:middle;margin-right:5px}
  table{width:100%;border-collapse:collapse;font-size:13px}
  th,td{text-align:right;padding:6px 8px;border-bottom:1px solid var(--line)}
  th:first-child,td:first-child{text-align:left}
  th{color:var(--dim);font-weight:500}
  tr.active td{background:#1f6feb22}
  .good{color:var(--ok)} .muted{color:var(--dim)}
  .note{color:var(--dim);font-size:12px;padding:0 24px 24px}
</style>
</head>
<body>
<header>
  <h1>GuideCore M0 · 导星算法闭环验收</h1>
  <div class="sub">合成导星曲线（周期误差 + 慢漂 + 视宁度噪声），理想赤道仪 1 帧响应延迟；数字由 C++ 内核实时计算。</div>
</header>

<main>
  <section class="panel wide">
    <div class="controls">
      <label for="algo">算法</label>
      <select id="algo"></select>
      <span id="summary" class="muted"></span>
    </div>
  </section>

  <section class="panel">
    <h2>RA · 原始漂移 vs 导星后残差</h2>
    <canvas id="raChart"></canvas>
    <div class="legend"><span><i style="background:var(--raw)"></i>未导星</span><span><i style="background:var(--res)"></i>残差</span></div>
  </section>

  <section class="panel">
    <h2>DEC · 原始漂移 vs 导星后残差</h2>
    <canvas id="decChart"></canvas>
    <div class="legend"><span><i style="background:var(--raw)"></i>未导星</span><span><i style="background:var(--res)"></i>残差</span></div>
  </section>

  <section class="panel">
    <h2>RA · 导星修正量</h2>
    <canvas id="corrChart"></canvas>
    <div class="legend"><span><i style="background:var(--corr)"></i>修正脉冲（px）</span></div>
  </section>

  <section class="panel">
    <h2>各算法 RMS 对比（RA）</h2>
    <canvas id="barChart"></canvas>
  </section>

  <section class="panel wide">
    <h2>汇总（跳过前 __SETTLE__ 帧）</h2>
    <table>
      <thead><tr><th>算法</th><th>RA RMS</th><th>RA 峰值</th><th>DEC RMS</th><th>DEC 峰值</th><th>RA 改善</th></tr></thead>
      <tbody id="tbody"></tbody>
    </table>
  </section>
</main>

<div class="note">说明：当前验收范围是“算法内核 + 闭环控制”，星点检测、相机与赤道仪驱动尚未接入，因此使用合成的星点位移输入。</div>

<script>
const DATA = __DATA__;
const COLORS = {raw:'#6e7681', res:'#58a6ff', corr:'#f0883e', ok:'#3fb950', dim:'#8b949e', grid:'#2a3441', text:'#c9d1d9'};
const rawRA = DATA.raw.ra, rawDec = DATA.raw.dec;
const algos = DATA.algorithms;
let current = algos[0].name;

const sel = document.getElementById('algo');
for (const a of algos) {
  const o = document.createElement('option');
  o.value = o.textContent = a.name;
  sel.appendChild(o);
}
sel.onchange = () => { current = sel.value; render(); };

function byName(name) { return algos.find(a => a.name === name); }

function fitCanvas(c) {
  const dpr = window.devicePixelRatio || 1;
  const w = c.clientWidth, h = c.clientHeight;
  c.width = Math.round(w * dpr); c.height = Math.round(h * dpr);
  const ctx = c.getContext('2d');
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  return {ctx, w, h};
}

function drawSeries(canvas, series) {
  const {ctx, w, h} = fitCanvas(canvas);
  const pad = {l:42, r:8, t:8, b:20};
  const n = series[0].data.length;
  let lo = Infinity, hi = -Infinity;
  for (const s of series) for (const v of s.data) { if (v < lo) lo = v; if (v > hi) hi = v; }
  if (lo === hi) { lo -= 1; hi += 1; }
  const span = hi - lo;
  lo -= span * 0.08; hi += span * 0.08;
  const X = i => pad.l + (w - pad.l - pad.r) * i / (n - 1);
  const Y = v => pad.t + (h - pad.t - pad.b) * (hi - v) / (hi - lo);

  ctx.clearRect(0, 0, w, h);
  ctx.font = '11px system-ui';
  ctx.textAlign = 'right'; ctx.textBaseline = 'middle';
  for (let k = 0; k <= 4; k++) {
    const v = lo + (hi - lo) * k / 4, y = Y(v);
    ctx.strokeStyle = COLORS.grid; ctx.lineWidth = 1;
    ctx.beginPath(); ctx.moveTo(pad.l, y); ctx.lineTo(w - pad.r, y); ctx.stroke();
    ctx.fillStyle = COLORS.dim; ctx.fillText(v.toFixed(2), pad.l - 6, y);
  }
  if (lo < 0 && hi > 0) {
    ctx.strokeStyle = '#3d4a5a'; ctx.beginPath();
    ctx.moveTo(pad.l, Y(0)); ctx.lineTo(w - pad.r, Y(0)); ctx.stroke();
  }
  ctx.textAlign = 'center'; ctx.textBaseline = 'top';
  ctx.fillStyle = COLORS.dim;
  for (let k = 0; k <= 4; k++) {
    const i = Math.round((n - 1) * k / 4);
    ctx.fillText(i, X(i), h - pad.b + 4);
  }

  for (const s of series) {
    ctx.strokeStyle = s.color; ctx.lineWidth = s.width || 1.4;
    if (s.dash) ctx.setLineDash([4, 3]); else ctx.setLineDash([]);
    ctx.beginPath();
    for (let i = 0; i < n; i++) {
      const x = X(i), y = Y(s.data[i]);
      i ? ctx.lineTo(x, y) : ctx.moveTo(x, y);
    }
    ctx.stroke();
  }
  ctx.setLineDash([]);
}

function drawBars(canvas) {
  const {ctx, w, h} = fitCanvas(canvas);
  const pad = {l:92, r:56, t:10, b:10};
  const rows = [{name: '未导星', rms: DATA.baseline.rmsRA, base: true}]
    .concat(algos.map(a => ({name: a.name, rms: a.rmsRA})));
  const max = Math.max(...rows.map(r => r.rms)) * 1.1;
  const bh = (h - pad.t - pad.b) / rows.length;

  ctx.clearRect(0, 0, w, h);
  ctx.font = '12px system-ui'; ctx.textBaseline = 'middle';
  rows.forEach((r, i) => {
    const y = pad.t + bh * i + bh * 0.18;
    const barH = bh * 0.64;
    const bw = (w - pad.l - pad.r) * r.rms / max;
    ctx.fillStyle = 'rgba(110,118,129,.18)';
    ctx.fillRect(pad.l, y, w - pad.l - pad.r, barH);
    ctx.fillStyle = r.base ? COLORS.raw : (r.name === current ? COLORS.ok : COLORS.res);
    ctx.fillRect(pad.l, y, Math.max(1, bw), barH);
    ctx.fillStyle = COLORS.text; ctx.textAlign = 'right';
    ctx.fillText(r.name, pad.l - 8, y + barH / 2);
    ctx.textAlign = 'left'; ctx.fillStyle = COLORS.dim;
    ctx.fillText(r.rms.toFixed(3), pad.l + bw + 6, y + barH / 2);
  });
}

function render() {
  const a = byName(current);
  document.getElementById('summary').textContent =
    `RA RMS ${a.rmsRA.toFixed(3)} px · 峰值 ${a.peakRA.toFixed(2)} px · 未导星 RMS ${DATA.baseline.rmsRA.toFixed(3)} px`;
  drawSeries(document.getElementById('raChart'), [
    {data: rawRA, color: COLORS.raw, dash: true, width: 1.2},
    {data: a.resRA, color: COLORS.res, width: 1.6}
  ]);
  drawSeries(document.getElementById('decChart'), [
    {data: rawDec, color: COLORS.raw, dash: true, width: 1.2},
    {data: a.resDec, color: COLORS.res, width: 1.6}
  ]);
  drawSeries(document.getElementById('corrChart'), [{data: a.corrRA, color: COLORS.corr, width: 1.2}]);
  drawBars(document.getElementById('barChart'));

  const base = DATA.baseline.rmsRA;
  document.getElementById('tbody').innerHTML = algos.map(x => {
    const improve = (1 - x.rmsRA / base) * 100;
    return `<tr class="${x.name === current ? 'active' : ''}">
      <td>${x.name}</td>
      <td>${x.rmsRA.toFixed(3)}</td>
      <td>${x.peakRA.toFixed(2)}</td>
      <td>${x.rmsDec.toFixed(3)}</td>
      <td>${x.peakDec.toFixed(2)}</td>
      <td class="${improve > 50 ? 'good' : 'muted'}">${improve.toFixed(1)}%</td>
    </tr>`;
  }).join('');
}

window.addEventListener('resize', render);
render();
</script>
</body>
</html>
"""


if __name__ == "__main__":
    main()
