#!/usr/bin/env python3
"""Run star_sim and produce a self-contained HTML acceptance report.

Usage: python core/tools/make_star_report.py [sim-executable] [output.html]
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
    for name in ("star_sim.exe", "star_sim"):
        candidate = BUILD / name
        if candidate.exists():
            return candidate
    raise SystemExit("star_sim not built; run: cmake --build core/build")


def main():
    sim = find_sim()
    data = json.loads(subprocess.check_output([str(sim)]))
    out = Path(sys.argv[2]) if len(sys.argv) > 2 else BUILD / "star-report.html"

    html = (TEMPLATE.replace("__DATA__", json.dumps(data, separators=(",", ":")))
            .replace("__TRUTH__", str(data["truthCount"]))
            .replace("__DET__", str(data["detectedCount"]))
            .replace("__MEANERR__", f"{data['meanError']:.3f}")
            .replace("__MAXERR__", f"{data['maxError']:.3f}"))
    out.write_text(html, encoding="utf-8")
    print(out)


TEMPLATE = r"""<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>GuideCore M0 · PHD2 星点检测验收</title>
<style>
  :root{--bg:#0f1419;--panel:#161d24;--line:#2a3441;--text:#c9d1d9;--dim:#8b949e;
        --det:#3fb950;--truth:#e6edf3;--primary:#f85149;--hfd:#f0883e;--box:#58a6ff}
  *{box-sizing:border-box}
  body{margin:0;background:var(--bg);color:var(--text);font:14px/1.5 system-ui,"Segoe UI",sans-serif}
  header{padding:20px 24px 8px}
  h1{margin:0;font-size:20px;font-weight:600}
  .sub{color:var(--dim);font-size:13px;margin-top:4px}
  .chips{display:flex;flex-wrap:wrap;gap:10px;margin:14px 0 0}
  .chip{background:var(--panel);border:1px solid var(--line);border-radius:6px;padding:7px 12px;font-size:13px}
  .chip b{font-weight:600;color:var(--text)} .chip span{color:var(--dim);margin-right:6px}
  main{padding:12px 24px 24px;display:grid;gap:16px;grid-template-columns:1.35fr 1fr}
  .panel{background:var(--panel);border:1px solid var(--line);border-radius:8px;padding:12px 14px;min-width:0}
  .panel h2{margin:0 0 8px;font-size:13px;font-weight:600;color:var(--dim);letter-spacing:.04em}
  .wide{grid-column:1/-1}
  canvas{display:block;width:100%;height:auto;border-radius:4px;background:#000}
  #crop{max-width:430px;margin:0 auto}
  .legend{display:flex;flex-wrap:wrap;gap:14px;color:var(--dim);font-size:12px;margin-top:8px}
  .legend i{display:inline-block;width:10px;height:10px;border-radius:50%;vertical-align:-1px;margin-right:5px}
  table{width:100%;border-collapse:collapse;font-size:13px}
  th,td{text-align:right;padding:6px 8px;border-bottom:1px solid var(--line);white-space:nowrap}
  th:first-child,td:first-child{text-align:left}
  th{color:var(--dim);font-weight:500}
  tr.primary td{background:#f8514918}
  .note{color:var(--dim);font-size:12px;padding:0 24px 22px}
  .note code{color:#9fb0c0;background:#161d24;border:1px solid var(--line);border-radius:4px;padding:1px 4px}
  .good{color:var(--det)}
</style>
</head>
<body>
<header>
  <h1>GuideCore M0 · PHD2 星点检测验收</h1>
  <div class="sub">C++ 移植自 PHD2 v2.6.14 的 <code>GuideStar::AutoFind</code> / <code>Star::Find</code>：3×3 中值去热像素 → 降采样 → PSF 卷积 → 局部极大值 → SNR/HFD 约束 → 质心。</div>
  <div class="chips">
    <div class="chip"><span>生成星点</span><b>__TRUTH__</b></div>
    <div class="chip"><span>检出（maxStars 限制）</span><b>__DET__</b></div>
    <div class="chip"><span>平均质心误差</span><b class="good">__MEANERR__ px</b></div>
    <div class="chip"><span>最大误差</span><b class="good">__MAXERR__ px</b></div>
  </div>
</header>

<main>
  <section class="panel">
    <h2>全画幅 · 640×480（热像素已由 Median3 去除）</h2>
    <canvas id="full" width="640" height="480"></canvas>
    <div class="legend">
      <span><i style="background:var(--det)"></i>检出星点</span>
      <span><i style="background:var(--truth)"></i>真实位置</span>
      <span><i style="background:var(--primary)"></i>主星 + 搜索框</span>
    </div>
  </section>

  <section class="panel">
    <h2>主星放大 · 质心 / HFD</h2>
    <canvas id="crop"></canvas>
    <div class="legend">
      <span><i style="background:var(--det)"></i>质心</span>
      <span><i style="background:var(--truth)"></i>真值</span>
      <span><i style="background:var(--hfd)"></i>HFD 半径</span>
      <span><i style="background:var(--box)"></i>搜索区域</span>
    </div>
  </section>

  <section class="panel wide">
    <h2>检出明细（误差 = 与最近真值的欧氏距离）</h2>
    <table>
      <thead><tr><th>#</th><th>检出 X</th><th>检出 Y</th><th>真值 X</th><th>真值 Y</th><th>dx</th><th>dy</th><th>误差</th><th>HFD</th><th>SNR</th><th>Peak</th></tr></thead>
      <tbody id="tbody"></tbody>
    </table>
  </section>
</main>

<div class="note">
参数与 PHD2 默认一致：searchRegion=15、MinHFD=1.5、MaxHFD=20、MinSNR=6；本页 maxStars=9（PHD2 DEFAULT_MAX_STAR_COUNT）以展示多星列表。
合成图：背景 800 ADU + σ=8 噪声，24 颗高斯星（σ=1.2–2.2 px），另加 3 个 6 万 ADU 热像素（远离真实星点）。
</div>

<script>
const D = __DATA__;
const C = {det:'#3fb950', truth:'#e6edf3', primary:'#f85149', hfd:'#f0883e', box:'#58a6ff'};

function drawFull() {
  const cv = document.getElementById('full');
  const ctx = cv.getContext('2d');
  const off = document.createElement('canvas');
  off.width = D.previewW; off.height = D.previewH;
  const octx = off.getContext('2d');
  const im = octx.createImageData(D.previewW, D.previewH);
  for (let i = 0; i < D.preview.length; i++) {
    const v = D.preview[i];
    im.data[i * 4] = im.data[i * 4 + 1] = im.data[i * 4 + 2] = v;
    im.data[i * 4 + 3] = 255;
  }
  octx.putImageData(im, 0, 0);
  ctx.imageSmoothingEnabled = true;
  ctx.drawImage(off, 0, 0, cv.width, cv.height);

  // truth markers
  ctx.strokeStyle = C.truth; ctx.lineWidth = 1; ctx.globalAlpha = 0.55;
  for (const t of D.truth) {
    ctx.beginPath();
    ctx.moveTo(t.x - 6, t.y); ctx.lineTo(t.x + 6, t.y);
    ctx.moveTo(t.x, t.y - 6); ctx.lineTo(t.x, t.y + 6);
    ctx.stroke();
  }
  ctx.globalAlpha = 1;

  // detections
  D.detections.forEach((d, i) => {
    const primary = i === 0;
    ctx.strokeStyle = primary ? C.primary : C.det;
    ctx.lineWidth = primary ? 2 : 1.5;
    ctx.beginPath();
    ctx.arc(d.x, d.y, primary ? 18 : 13, 0, Math.PI * 2);
    ctx.stroke();
    ctx.beginPath();
    ctx.moveTo(d.x - 7, d.y); ctx.lineTo(d.x + 7, d.y);
    ctx.moveTo(d.x, d.y - 7); ctx.lineTo(d.x, d.y + 7);
    ctx.stroke();
    if (primary) {
      const r = D.params.searchRegion;
      ctx.setLineDash([5, 4]);
      ctx.strokeRect(d.x - r, d.y - r, 2 * r, 2 * r);
      ctx.setLineDash([]);
      ctx.fillStyle = C.primary;
      ctx.font = '12px system-ui';
      ctx.fillText('primary', d.x + 22, d.y - 14);
    }
  });
}

function drawCrop() {
  const d = D.detections[0];
  const cv = document.getElementById('crop');
  const px = 6;
  cv.width = D.cropSize * px; cv.height = D.cropSize * px;
  const ctx = cv.getContext('2d');
  ctx.imageSmoothingEnabled = false;
  for (let y = 0; y < D.cropSize; y++) {
    for (let x = 0; x < D.cropSize; x++) {
      const v = D.crop[y * D.cropSize + x];
      ctx.fillStyle = `rgb(${v},${v},${v})`;
      ctx.fillRect(x * px, y * px, px, px);
    }
  }

  const ox = (d.x - D.cropX) * px, oy = (d.y - D.cropY) * px;
  const truth = d.truth >= 0 ? D.truth[d.truth] : null;

  // search region
  const r = D.params.searchRegion * px;
  ctx.strokeStyle = C.box; ctx.lineWidth = 1.5; ctx.setLineDash([6, 4]);
  ctx.strokeRect(ox - r, oy - r, 2 * r, 2 * r);
  ctx.setLineDash([]);

  // HFD circle
  ctx.strokeStyle = C.hfd; ctx.lineWidth = 1.5;
  ctx.beginPath(); ctx.arc(ox, oy, (d.hfd / 2) * px, 0, Math.PI * 2); ctx.stroke();

  // truth cross
  if (truth) {
    const tx = (truth.x - D.cropX) * px, ty = (truth.y - D.cropY) * px;
    ctx.strokeStyle = C.truth; ctx.lineWidth = 1.5;
    ctx.beginPath();
    ctx.moveTo(tx - 8, ty); ctx.lineTo(tx + 8, ty);
    ctx.moveTo(tx, ty - 8); ctx.lineTo(tx, ty + 8);
    ctx.stroke();
  }

  // centroid crosshair
  ctx.strokeStyle = C.det; ctx.lineWidth = 1.5;
  ctx.beginPath();
  ctx.moveTo(0, oy); ctx.lineTo(cv.width, oy);
  ctx.moveTo(ox, 0); ctx.lineTo(ox, cv.height);
  ctx.stroke();
  ctx.beginPath(); ctx.arc(ox, oy, 5, 0, Math.PI * 2); ctx.stroke();

  ctx.fillStyle = C.det; ctx.font = '12px system-ui';
  ctx.fillText(`(${d.x.toFixed(2)}, ${d.y.toFixed(2)})  HFD ${d.hfd.toFixed(2)}  SNR ${d.snr.toFixed(0)}`, 8, 16);
}

function fillTable() {
  document.getElementById('tbody').innerHTML = D.detections.map((d, i) => {
    const t = d.truth >= 0 ? D.truth[d.truth] : null;
    return `<tr class="${i === 0 ? 'primary' : ''}">
      <td>${i === 0 ? '★' : i + 1}</td>
      <td>${d.x.toFixed(2)}</td><td>${d.y.toFixed(2)}</td>
      <td>${t ? t.x.toFixed(2) : '—'}</td><td>${t ? t.y.toFixed(2) : '—'}</td>
      <td>${d.dx.toFixed(3)}</td><td>${d.dy.toFixed(3)}</td>
      <td class="good">${d.err.toFixed(3)}</td>
      <td>${d.hfd.toFixed(2)}</td><td>${d.snr.toFixed(0)}</td><td>${d.peak}</td>
    </tr>`;
  }).join('');
}

drawFull();
drawCrop();
fillTable();
</script>
</body>
</html>
"""


if __name__ == "__main__":
    main()
