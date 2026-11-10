#!/usr/bin/env python3
"""Run cal_sim and produce a self-contained HTML acceptance report.

Usage: python core/tools/make_cal_report.py [sim-executable] [output.html]
"""
import json
import math
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
BUILD = HERE.parent / "build"


def find_tool():
    if len(sys.argv) > 1:
        return Path(sys.argv[1])
    for name in ("cal_sim.exe", "cal_sim"):
        candidate = BUILD / name
        if candidate.exists():
            return candidate
    raise SystemExit("cal_sim not built; run: cmake --build core/build")


def main():
    data = json.loads(subprocess.check_output([str(find_tool())]))
    r = data["result"]
    out = Path(sys.argv[2]) if len(sys.argv) > 2 else BUILD / "cal-report.html"

    html = (TEMPLATE
            .replace("__DATA__", json.dumps(data, separators=(",", ":")))
            .replace("__PULSES__", str(data["pulses"]))
            .replace("__XANGLE__", f"{math.degrees(r['xAngle']):.2f}")
            .replace("__YANGLE__", f"{math.degrees(r['yAngle']):.2f}")
            .replace("__XRATE__", f"{r['xRate'] * 1000:.3f}")
            .replace("__YRATE__", f"{r['yRate'] * 1000:.3f}")
            .replace("__ORTHO__", f"{r['orthoErrorDeg']:.2f}")
            .replace("__HOME__", f"{r['homeDist']:.2f}"))
    out.write_text(html, encoding="utf-8")
    print(out)


TEMPLATE = r"""<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>GuideCore M0 · PHD2 校准状态机验收</title>
<style>
  :root{--bg:#0f1419;--panel:#161d24;--line:#2a3441;--text:#c9d1d9;--dim:#8b949e;
        --w:#58a6ff;--e:#3fb950;--n:#f0883e;--s:#bc8cff;--start:#e6edf3;--axis:#8b949e}
  *{box-sizing:border-box}
  body{margin:0;background:var(--bg);color:var(--text);font:14px/1.5 system-ui,"Segoe UI",sans-serif}
  header{padding:20px 24px 8px}
  h1{margin:0;font-size:20px;font-weight:600}
  .sub{color:var(--dim);font-size:13px;margin-top:4px}
  .chips{display:flex;flex-wrap:wrap;gap:10px;margin:14px 0 0}
  .chip{background:var(--panel);border:1px solid var(--line);border-radius:6px;padding:7px 12px;font-size:13px}
  .chip b{font-weight:600} .chip span{color:var(--dim);margin-right:6px}
  .good{color:var(--e)}
  main{padding:12px 24px 24px;display:grid;gap:16px;grid-template-columns:1.25fr 1fr}
  .panel{background:var(--panel);border:1px solid var(--line);border-radius:8px;padding:12px 14px;min-width:0}
  .panel h2{margin:0 0 8px;font-size:13px;font-weight:600;color:var(--dim);letter-spacing:.04em}
  .wide{grid-column:1/-1}
  canvas{display:block;width:100%;height:auto;border-radius:4px;background:#0b0f14}
  .legend{display:flex;flex-wrap:wrap;gap:14px;color:var(--dim);font-size:12px;margin-top:8px}
  .legend i{display:inline-block;width:16px;height:3px;vertical-align:3px;margin-right:6px;border-radius:2px}
  table{width:100%;border-collapse:collapse;font-size:13px}
  th,td{text-align:right;padding:5px 8px;border-bottom:1px solid var(--line);white-space:nowrap}
  th:first-child,td:first-child,th:nth-child(2),td:nth-child(2){text-align:left}
  th{color:var(--dim);font-weight:500}
  .note{color:var(--dim);font-size:12px;padding:0 24px 22px}
  .note code{color:#9fb0c0;background:#161d24;border:1px solid var(--line);border-radius:4px;padding:1px 4px}
</style>
</head>
<body>
<header>
  <h1>GuideCore M0 · PHD2 校准状态机验收</h1>
  <div class="sub">移植自 <code>Scope::UpdateCalibrationState</code>：West 测量 → East 回中 → North 回差清除 → North 测量 →
    South 回中 → South 微调回起点。合成赤道仪按脉冲方向/毫秒移动星点，C++ 内核完全按帧驱动。</div>
  <div class="chips">
    <div class="chip"><span>脉冲数</span><b>__PULSES__</b></div>
    <div class="chip"><span>xAngle</span><b>__XANGLE__°</b></div>
    <div class="chip"><span>yAngle</span><b>__YANGLE__°</b></div>
    <div class="chip"><span>xRate</span><b>__XRATE__ px/s</b></div>
    <div class="chip"><span>yRate</span><b>__YRATE__ px/s</b></div>
    <div class="chip"><span>正交误差</span><b>__ORTHO__°</b></div>
    <div class="chip"><span>回到起点</span><b class="good">__HOME__ px</b></div>
  </div>
</header>

<main>
  <section class="panel">
    <h2>星点路径（相机像素）· 箭头 = 脉冲方向</h2>
    <canvas id="path" width="760" height="560"></canvas>
    <div class="legend">
      <span><i style="background:var(--w)"></i>West</span>
      <span><i style="background:var(--e)"></i>East</span>
      <span><i style="background:var(--n)"></i>North</span>
      <span><i style="background:var(--s)"></i>South</span>
      <span><i style="background:var(--start)"></i>起点 / 终点</span>
      <span><i style="background:var(--axis);opacity:.6"></i>测得的 X / Y 轴</span>
    </div>
  </section>

  <section class="panel">
    <h2>脉冲时序</h2>
    <table>
      <thead><tr><th>#</th><th>方向</th><th>状态</th><th>ms</th><th>X</th><th>Y</th></tr></thead>
      <tbody id="tbody"></tbody>
    </table>
  </section>
</main>

<div class="note">
  模拟参数：5.75″/s 导星速度等效 0.02 px/ms（随机取定），RA 轴在相机中的方向 0.4 rad，
  DEC 轴偏 0.05 rad（≈2.9° 非正交），步长 750 ms、目标距离 25 px（PHD2 默认）。
  所有坐标、角度和速率来自 C++ 内核。
</div>

<script>
const D = __DATA__;
const R = D.result;
const C = {West:'#58a6ff', East:'#3fb950', North:'#f0883e', South:'#bc8cff'};
const cv = document.getElementById('path');
const ctx = cv.getContext('2d');
const W = cv.width, H = cv.height, PAD = 44;

const pts = D.path;
let minX = Infinity, maxX = -Infinity, minY = Infinity, maxY = -Infinity;
for (const p of pts) {
  minX = Math.min(minX, p.x); maxX = Math.max(maxX, p.x);
  minY = Math.min(minY, p.y); maxY = Math.max(maxY, p.y);
}
const span = Math.max(maxX - minX, maxY - minY, 1) * 1.15;
const cx = (minX + maxX) / 2, cy = (minY + maxY) / 2;
const scale = Math.min((W - 2 * PAD) / span, (H - 2 * PAD) / span);
const X = x => W / 2 + (x - cx) * scale;
const Y = y => H / 2 - (y - cy) * scale;

// grid
ctx.strokeStyle = '#1b232c'; ctx.lineWidth = 1;
for (let g = Math.floor(minX / 10) * 10; g <= maxX + 10; g += 10) {
  ctx.beginPath(); ctx.moveTo(X(g), 0); ctx.lineTo(X(g), H); ctx.stroke();
}
for (let g = Math.floor(minY / 10) * 10; g <= maxY + 10; g += 10) {
  ctx.beginPath(); ctx.moveTo(0, Y(g)); ctx.lineTo(W, Y(g)); ctx.stroke();
}

// measured axes through the start point
function axis(angle, label) {
  const len = span * 0.38;
  const x0 = pts[0].x, y0 = pts[0].y;
  ctx.strokeStyle = '#8b949e'; ctx.globalAlpha = 0.5; ctx.setLineDash([6, 4]); ctx.lineWidth = 1.2;
  ctx.beginPath();
  ctx.moveTo(X(x0 - Math.cos(angle) * len), Y(y0 - Math.sin(angle) * len));
  ctx.lineTo(X(x0 + Math.cos(angle) * len), Y(y0 + Math.sin(angle) * len));
  ctx.stroke();
  ctx.setLineDash([]); ctx.globalAlpha = 1;
  ctx.fillStyle = '#8b949e'; ctx.font = '12px system-ui';
  ctx.fillText(label, X(x0 + Math.cos(angle) * len) + 4, Y(y0 + Math.sin(angle) * len) - 4);
}
axis(R.xAngle, 'X 轴');
axis(R.yAngle, 'Y 轴');

// path arrows
for (let i = 1; i < pts.length; i++) {
  const a = pts[i - 1], b = pts[i];
  ctx.strokeStyle = C[b.dir] || '#e6edf3';
  ctx.fillStyle = C[b.dir] || '#e6edf3';
  ctx.lineWidth = 2;
  ctx.beginPath(); ctx.moveTo(X(a.x), Y(a.y)); ctx.lineTo(X(b.x), Y(b.y)); ctx.stroke();

  const dx = X(b.x) - X(a.x), dy = Y(b.y) - Y(a.y);
  const len = Math.hypot(dx, dy);
  if (len > 8) {
    const ux = dx / len, uy = dy / len;
    const mx = X(b.x), my = Y(b.y);
    ctx.beginPath();
    ctx.moveTo(mx, my);
    ctx.lineTo(mx - ux * 10 - uy * 5, my - uy * 10 + ux * 5);
    ctx.lineTo(mx - ux * 10 + uy * 5, my - uy * 10 - ux * 5);
    ctx.closePath(); ctx.fill();
  }
}

// start / end markers
function marker(p, color, label) {
  ctx.strokeStyle = color; ctx.lineWidth = 2;
  ctx.beginPath(); ctx.arc(X(p.x), Y(p.y), 6, 0, Math.PI * 2); ctx.stroke();
  ctx.fillStyle = color; ctx.font = '12px system-ui';
  ctx.fillText(label, X(p.x) + 9, Y(p.y) - 8);
}
marker(pts[0], '#e6edf3', '起点');
marker(pts[pts.length - 1], '#3fb950', '终点');

document.getElementById('tbody').innerHTML = pts.slice(1).map((p, i) =>
  `<tr><td>${i + 1}</td><td style="color:${C[p.dir] || '#c9d1d9'}">${p.dir}</td><td>${p.state}</td>
   <td>${p.ms}</td><td>${p.x.toFixed(2)}</td><td>${p.y.toFixed(2)}</td></tr>`).join('');
</script>
</body>
</html>
"""


if __name__ == "__main__":
    main()
