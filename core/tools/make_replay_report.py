#!/usr/bin/env python3
"""Run guide_replay and produce a self-contained HTML acceptance report.

Usage: python core/tools/make_replay_report.py [replay-executable] [output.html]
"""
import json
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
BUILD = HERE.parent / "build"


def find_tool():
    if len(sys.argv) > 1:
        return Path(sys.argv[1])
    for name in ("guide_replay.exe", "guide_replay"):
        candidate = BUILD / name
        if candidate.exists():
            return candidate
    raise SystemExit("guide_replay not built; run: cmake --build core/build")


def main():
    tool = find_tool()
    data = json.loads(subprocess.check_output([str(tool)]))
    out = Path(sys.argv[2]) if len(sys.argv) > 2 else BUILD / "replay-report.html"
    html = (TEMPLATE.replace("__DATA__", json.dumps(data, separators=(",", ":")))
            .replace("__SETTLE__", str(data["settle"])))
    out.write_text(html, encoding="utf-8")
    print(out)


TEMPLATE = r"""<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>GuideCore M0 · 帧级导星回放验收</title>
<style>
  :root{--bg:#0f1419;--panel:#161d24;--line:#2a3441;--text:#c9d1d9;--dim:#8b949e;
        --ra:#58a6ff;--dec:#f0883e;--drift:#8b949e;--corr:#3fb950;--bad:#f85149}
  *{box-sizing:border-box}
  body{margin:0;background:var(--bg);color:var(--text);font:14px/1.5 system-ui,"Segoe UI",sans-serif}
  header{padding:20px 24px 6px}
  h1{margin:0;font-size:20px;font-weight:600}
  .sub{color:var(--dim);font-size:13px;margin-top:4px}
  .bar{display:flex;flex-wrap:wrap;align-items:center;gap:10px;margin:14px 24px 0}
  select{background:var(--panel);color:var(--text);border:1px solid var(--line);border-radius:6px;padding:6px 10px;font:inherit}
  .chip{background:var(--panel);border:1px solid var(--line);border-radius:6px;padding:7px 12px;font-size:13px}
  .chip b{font-weight:600} .chip span{color:var(--dim);margin-right:6px}
  .good{color:var(--corr)}
  main{padding:12px 24px 24px;display:grid;gap:16px}
  .panel{background:var(--panel);border:1px solid var(--line);border-radius:8px;padding:12px 14px;min-width:0}
  .panel h2{margin:0 0 8px;font-size:13px;font-weight:600;color:var(--dim);letter-spacing:.04em}
  canvas{display:block;width:100%;height:auto;border-radius:4px}
  .legend{display:flex;flex-wrap:wrap;gap:14px;color:var(--dim);font-size:12px;margin-top:8px}
  .legend i{display:inline-block;width:16px;height:3px;vertical-align:3px;margin-right:6px;border-radius:2px}
  table{width:100%;border-collapse:collapse;font-size:13px}
  th,td{text-align:right;padding:6px 8px;border-bottom:1px solid var(--line);white-space:nowrap}
  th:first-child,td:first-child{text-align:left}
  th{color:var(--dim);font-weight:500}
  tr.sel td{background:#58a6ff14}
  .note{color:var(--dim);font-size:12px;padding:0 24px 22px}
  .note code{color:#9fb0c0;background:#161d24;border:1px solid var(--line);border-radius:4px;padding:1px 4px}
</style>
</head>
<body>
<header>
  <h1>GuideCore M0 · 帧级导星回放验收</h1>
  <div class="sub">每一帧都是真实渲染的 121×121 星图：<code>GuideStar::AutoFind</code> 选星 → 逐帧 <code>Star::Find</code> 重找 →
    <code>star - lockPos</code> 偏移 → PHD2 导星算法输出修正。赤道仪模型为理想响应 + 1 帧延迟。</div>
</header>

<div class="bar">
  <select id="algo"></select>
  <div class="chip"><span>帧数</span><b id="cFrames"></b></div>
  <div class="chip"><span>质心误差均值</span><b class="good" id="cErr"></b></div>
  <div class="chip"><span>丢星帧</span><b id="cLost"></b></div>
  <div class="chip"><span>残差 RMS (RA)</span><b id="cRA"></b></div>
  <div class="chip"><span>残差 RMS (DEC)</span><b id="cDEC"></b></div>
  <div class="chip"><span>峰值 (RA)</span><b id="cPeak"></b></div>
</div>

<main>
  <section class="panel">
    <h2>残差 · star − lock（像素）</h2>
    <canvas id="cRes" width="960" height="260"></canvas>
    <div class="legend">
      <span><i style="background:var(--ra)"></i>RA</span>
      <span><i style="background:var(--dec)"></i>DEC</span>
      <span><i style="background:var(--dim);opacity:.6"></i>settle（前 __SETTLE__ 帧不计）</span>
    </div>
  </section>

  <section class="panel">
    <h2>累计原始漂移 vs 累计修正（像素）</h2>
    <canvas id="cCum" width="960" height="260"></canvas>
    <div class="legend">
      <span><i style="background:var(--drift)"></i>原始漂移 RA / DEC</span>
      <span><i style="background:var(--corr)"></i>累计修正 RA / DEC</span>
    </div>
  </section>

  <section class="panel">
    <h2>每帧修正量（像素） · 星点 SNR / HFD（已归一化到修正量刻度）</h2>
    <canvas id="cCorr" width="960" height="260"></canvas>
    <div class="legend">
      <span><i style="background:var(--ra)"></i>RA 修正</span>
      <span><i style="background:var(--dec)"></i>DEC 修正</span>
      <span><i style="background:var(--corr)"></i>SNR</span>
      <span><i style="background:var(--bad)"></i>HFD</span>
    </div>
  </section>

  <section class="panel">
    <h2>算法汇总（settle 后）</h2>
    <table>
      <thead><tr><th>算法</th><th>RMS RA</th><th>RMS DEC</th><th>峰值 RA</th><th>峰值 DEC</th>
        <th>质心误差</th><th>丢星帧</th></tr></thead>
      <tbody id="tbody"></tbody>
    </table>
  </section>
</main>

<div class="note">
  合成星图：背景 800 ADU + σ=6 噪声，高斯星 σ=1.6 px / 峰值 12000 ADU，SNR≈300，HFD≈3.8 px。
  漂移：RA 0.12 px/帧 + 0.9 px 周期误差；DEC 0.04 px/帧 + 0.45 px 周期误差。
  参数取 PHD2 默认（minMove=0.2、searchRegion=15、MinHFD=1.5、MaxHFD=20）。所有数字来自 C++ 内核，浏览器只绘图。
</div>

<script>
const D = __DATA__;
const C = {ra:'#58a6ff', dec:'#f0883e', drift:'#8b949e', corr:'#3fb950', bad:'#f85149'};
const SETTLE = D.settle;

function lineChart(cv, series, opts) {
  opts = opts || {};
  const ctx = cv.getContext('2d');
  const W = cv.width, H = cv.height, L = 48, R = 12, T = 10, B = 24;
  ctx.clearRect(0, 0, W, H);
  const n = series[0].values.length;
  let ymin = opts.ymin, ymax = opts.ymax;
  if (ymin === undefined) {
    ymin = Infinity; ymax = -Infinity;
    for (const s of series) for (const v of s.values) {
      if (v === null || !isFinite(v)) continue;
      ymin = Math.min(ymin, v); ymax = Math.max(ymax, v);
    }
    if (ymin === Infinity) { ymin = 0; ymax = 1; }
    if (opts.zero !== false) { ymin = Math.min(ymin, 0); ymax = Math.max(ymax, 0); }
    if (ymax - ymin < 1e-6) { ymax += 0.1; ymin -= 0.1; }
    const pad = (ymax - ymin) * 0.12;
    ymin -= pad; ymax += pad;
  }
  const X = i => L + (W - L - R) * (n <= 1 ? 0 : i / (n - 1));
  const Y = v => T + (H - T - B) * (1 - (v - ymin) / (ymax - ymin));

  ctx.font = '11px system-ui'; ctx.textBaseline = 'middle';
  for (let k = 0; k <= 4; k++) {
    const v = ymin + (ymax - ymin) * k / 4, y = Y(v);
    ctx.strokeStyle = '#2a3441'; ctx.lineWidth = 1;
    ctx.beginPath(); ctx.moveTo(L, y); ctx.lineTo(W - R, y); ctx.stroke();
    ctx.fillStyle = '#8b949e'; ctx.textAlign = 'right';
    ctx.fillText(Math.abs(v) >= 100 ? v.toFixed(0) : v.toFixed(2), L - 6, y);
  }
  ctx.textAlign = 'center'; ctx.textBaseline = 'top';
  for (let k = 0; k <= 5; k++) {
    const i = Math.round((n - 1) * k / 5);
    ctx.fillStyle = '#8b949e';
    ctx.fillText(String(i), X(i), H - B + 5);
  }
  if (opts.zero !== false && ymin < 0 && ymax > 0) {
    ctx.strokeStyle = '#3d4b5c'; ctx.setLineDash([4, 4]);
    ctx.beginPath(); ctx.moveTo(L, Y(0)); ctx.lineTo(W - R, Y(0)); ctx.stroke();
    ctx.setLineDash([]);
  }
  if (SETTLE > 0 && SETTLE < n) {
    ctx.strokeStyle = '#8b949e88'; ctx.setLineDash([4, 4]);
    ctx.beginPath(); ctx.moveTo(X(SETTLE), T); ctx.lineTo(X(SETTLE), H - B); ctx.stroke();
    ctx.setLineDash([]);
  }
  for (const s of series) {
    ctx.strokeStyle = s.color; ctx.lineWidth = s.width || 1.6;
    ctx.setLineDash(s.dash || []); ctx.beginPath();
    let pen = false;
    for (let i = 0; i < n; i++) {
      const v = s.values[i];
      if (v === null || !isFinite(v)) { pen = false; continue; }
      const x = X(i), y = Y(v);
      if (pen) ctx.lineTo(x, y); else { ctx.moveTo(x, y); pen = true; }
    }
    ctx.stroke(); ctx.setLineDash([]);
  }
}

function cumulative(a, scale) {
  let s = 0;
  return a.map(v => (s += v * scale));
}

let current = 0;
function render() {
  const a = D.algorithms[current];
  document.getElementById('cFrames').textContent = D.frames;
  document.getElementById('cErr').textContent = a.meanErr.toFixed(4) + ' px';
  document.getElementById('cLost').textContent = a.lost;
  document.getElementById('cRA').textContent = a.rmsRA.toFixed(3) + ' px';
  document.getElementById('cDEC').textContent = a.rmsDec.toFixed(3) + ' px';
  document.getElementById('cPeak').textContent = a.peakRA.toFixed(3) + ' px';

  lineChart(document.getElementById('cRes'), [
    {values: a.resRA, color: C.ra},
    {values: a.resDec, color: C.dec},
  ]);

  const rawRA = D.drift.ra.map(v => v - D.drift.ra[0]);
  const rawDec = D.drift.dec.map(v => v - D.drift.dec[0]);
  lineChart(document.getElementById('cCum'), [
    {values: rawRA, color: C.drift, dash: [5, 4], width: 1.2},
    {values: rawDec, color: C.drift, dash: [2, 3], width: 1.2},
    {values: cumulative(a.corrRA, 1), color: C.ra, width: 1.8},
    {values: cumulative(a.corrDec, 1), color: C.dec, width: 1.8},
  ], {zero: true});

  // corrections on the left scale, SNR/HFD normalized onto the same panel
  const maxCorr = Math.max(0.1, ...a.corrRA.map(Math.abs), ...a.corrDec.map(Math.abs));
  let snrMax = 0, hfdMax = 0;
  for (const v of a.snr) snrMax = Math.max(snrMax, v);
  for (const v of a.hfd) hfdMax = Math.max(hfdMax, v);
  const snrScaled = a.snr.map(v => v / (snrMax || 1) * maxCorr);
  const hfdScaled = a.hfd.map(v => v / (hfdMax || 1) * maxCorr);
  lineChart(document.getElementById('cCorr'), [
    {values: a.corrRA, color: C.ra},
    {values: a.corrDec, color: C.dec},
    {values: snrScaled, color: C.corr, dash: [3, 3]},
    {values: hfdScaled, color: C.bad, dash: [1, 3]},
  ]);

  document.getElementById('tbody').innerHTML = D.algorithms.map((x, i) => `<tr class="${i === current ? 'sel' : ''}">
      <td>${x.name}</td>
      <td>${x.rmsRA.toFixed(3)}</td><td>${x.rmsDec.toFixed(3)}</td>
      <td>${x.peakRA.toFixed(3)}</td><td>${x.peakDec.toFixed(3)}</td>
      <td>${x.meanErr.toFixed(4)}</td><td>${x.lost}</td>
    </tr>`).join('');
}

const sel = document.getElementById('algo');
D.algorithms.forEach((a, i) => {
  const o = document.createElement('option');
  o.value = i; o.textContent = a.name;
  sel.appendChild(o);
});
sel.addEventListener('change', () => { current = +sel.value; render(); });
render();
</script>
</body>
</html>
"""


if __name__ == "__main__":
    main()
