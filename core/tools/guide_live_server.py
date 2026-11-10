#!/usr/bin/env python3
"""Interactive web console for the ported guide core.

Spawns guide_live (one command line in, one JSON state line out) and serves a
live simulator page on http://127.0.0.1:8765. No third-party packages.

Usage: python core/tools/guide_live_server.py [guide_live-executable] [--port N]
"""
import json
import subprocess
import sys
import threading
import webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

HERE = Path(__file__).resolve().parent
BUILD = HERE.parent / "build"


class LiveProcess:
    def __init__(self, exe):
        self.exe = str(exe)
        self.proc = None
        self.lock = threading.Lock()
        self.start()

    def start(self):
        self.proc = subprocess.Popen(
            [self.exe], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            text=True, bufsize=1, encoding="utf-8", errors="replace")
        self.proc.stdout.readline()  # calibration status line

    def command(self, cmd):
        with self.lock:
            for _ in range(2):
                try:
                    self.proc.stdin.write(cmd + "\n")
                    self.proc.stdin.flush()
                    line = self.proc.stdout.readline()
                except (BrokenPipeError, OSError):
                    line = ""
                if line:
                    return line.strip()
                self.start()  # child died; restart and retry once
        return json.dumps({"error": "guide_live stopped"})


def find_tool():
    for name in ("guide_live.exe", "guide_live"):
        candidate = BUILD / name
        if candidate.exists():
            return candidate
    raise SystemExit("guide_live not built; run: cmake --build core/build")


class Handler(BaseHTTPRequestHandler):
    live = None

    def _send(self, body, ctype="application/json; charset=utf-8"):
        data = body.encode("utf-8") if isinstance(body, str) else body
        self.send_response(200)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        url = urlparse(self.path)
        if url.path in ("/", "/index.html"):
            self._send(PAGE, "text/html; charset=utf-8")
        elif url.path == "/api/cmd":
            cmd = parse_qs(url.query).get("c", ["status"])[0]
            self._send(self.live.command(cmd))
        else:
            self.send_error(404)

    def log_message(self, *_):
        pass


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    port = 8765
    for i, a in enumerate(sys.argv):
        if a == "--port" and i + 1 < len(sys.argv):
            port = int(sys.argv[i + 1])
            args = [x for x in args if x != sys.argv[i + 1]]

    tool = Path(args[0]) if args else find_tool()
    Handler.live = LiveProcess(tool)
    url = f"http://127.0.0.1:{port}/"
    print(f"GuideCore live: {url}  (Ctrl+C to stop)")
    threading.Timer(0.4, lambda: webbrowser.open(url)).start()
    ThreadingHTTPServer(("127.0.0.1", port), Handler).serve_forever()


PAGE = r"""<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>GuideCore Live · 交互导星模拟</title>
<style>
  :root{--bg:#0f1419;--panel:#161d24;--line:#2a3441;--text:#c9d1d9;--dim:#8b949e;
        --ra:#58a6ff;--dec:#f0883e;--ok:#3fb950;--bad:#f85149;--warn:#d29922}
  *{box-sizing:border-box}
  body{margin:0;background:var(--bg);color:var(--text);font:14px/1.5 system-ui,"Segoe UI",sans-serif}
  header{padding:18px 24px 4px;display:flex;flex-wrap:wrap;gap:12px;align-items:baseline}
  h1{margin:0;font-size:19px;font-weight:600}
  .sub{color:var(--dim);font-size:13px}
  .layout{display:grid;grid-template-columns:minmax(300px,420px) 1fr;gap:16px;padding:14px 24px 24px;align-items:start}
  @media (max-width:860px){.layout{grid-template-columns:1fr}}
  .panel{background:var(--panel);border:1px solid var(--line);border-radius:8px;padding:12px 14px;min-width:0}
  .panel h2{margin:0 0 10px;font-size:12px;font-weight:600;color:var(--dim);letter-spacing:.05em}
  #star{width:100%;display:block;image-rendering:pixelated;border-radius:6px;background:#000;border:1px solid var(--line)}
  .row{display:flex;flex-wrap:wrap;gap:8px;align-items:center;margin-top:10px}
  button{background:#1f2933;color:var(--text);border:1px solid var(--line);border-radius:6px;
         padding:7px 12px;font:inherit;cursor:pointer}
  button:hover{border-color:#3d4b5c}
  button.primary{background:#1f6feb;border-color:#1f6feb;color:#fff}
  button.primary.stop{background:#7d2c2c;border-color:#7d2c2c}
  select{background:var(--panel);color:var(--text);border:1px solid var(--line);border-radius:6px;padding:6px 8px;font:inherit}
  label.sl{display:flex;align-items:center;gap:8px;font-size:12px;color:var(--dim);flex:1 1 100%}
  label.sl input{flex:1;accent-color:#1f6feb}
  label.sl b{color:var(--text);font-weight:600;min-width:52px;text-align:right;font-variant-numeric:tabular-nums}
  .chips{display:grid;grid-template-columns:repeat(auto-fill,minmax(128px,1fr));gap:8px;margin-top:12px}
  .chip{background:#121820;border:1px solid var(--line);border-radius:6px;padding:6px 9px;font-size:12px}
  .chip span{display:block;color:var(--dim);font-size:11px}
  .chip b{font-size:15px;font-weight:600;font-variant-numeric:tabular-nums}
  canvas.chart{display:block;width:100%;height:auto;border-radius:4px}
  .chartbox+.chartbox{margin-top:12px}
  .foot{color:var(--dim);font-size:12px;padding:0 24px 22px}
  .ok{color:var(--ok)} .bad{color:var(--bad)} .warn{color:var(--warn)}
</style>
</head>
<body>
<header>
  <h1>GuideCore Live · 交互导星模拟</h1>
  <div class="sub">校准 → 相机/赤道仪变换 → 导星算法 → 1 ms 脉冲 → 合成赤道仪（1 帧延迟）</div>
</header>

<div class="layout">
  <section class="panel">
    <h2>导星相机视场（121×121，绿框 = 搜索区 15 px）</h2>
    <canvas id="star" width="121" height="121"></canvas>
    <div class="row">
      <button id="run" class="primary">▶ 开始导星</button>
      <button id="dither">扰动 3 px</button>
      <button id="cal">重新校准</button>
      <button id="reset">重置</button>
    </div>
    <div class="row">
      <select id="algo">
        <option value="0">Identity</option>
        <option value="1" selected>Hysteresis</option>
        <option value="2">ResistSwitch</option>
        <option value="3">Lowpass</option>
        <option value="4">Lowpass2</option>
        <option value="5">ZFilter</option>
      </select>
      <span id="state" class="sub">暂停</span>
    </div>
    <div class="row">
      <label class="sl">minMove <input id="minmove" type="range" min="0" max="1" step="0.05" value="0.2"><b id="minmoveV">0.20 px</b></label>
      <label class="sl">视宁度 σ <input id="seeing" type="range" min="1" max="3" step="0.1" value="1.6"><b id="seeingV">1.6 px</b></label>
      <label class="sl">漂移速率 <input id="drift" type="range" min="0" max="0.4" step="0.01" value="0.12"><b id="driftV">0.12 px/帧</b></label>
    </div>
    <div class="chips">
      <div class="chip"><span>帧</span><b id="cFrame">—</b></div>
      <div class="chip"><span>RMS RA / DEC</span><b id="cRms">—</b></div>
      <div class="chip"><span>峰值 RA / DEC</span><b id="cPeak">—</b></div>
      <div class="chip"><span>本帧脉冲 RA / DEC</span><b id="cPulseNow">—</b></div>
      <div class="chip"><span>SNR / HFD</span><b id="cStar">—</b></div>
      <div class="chip"><span>校准 xAngle / yAngle</span><b id="cAngles">—</b></div>
    </div>
  </section>

  <section class="panel">
    <h2>残差 · star − lock（像素）</h2>
    <canvas id="cRes" class="chart" width="880" height="240"></canvas>
    <div class="chartbox">
      <h2 style="margin-top:10px">送往赤道仪的脉冲（毫秒）</h2>
      <canvas id="cPulse" class="chart" width="880" height="180"></canvas>
    </div>
  </section>
</div>

<div class="foot">
  合成星场 121×121：背景 800 ADU + 噪声，高斯 PSF 峰值 12000 ADU。
  “扰动”给星点加 3 px 视场偏移，观察导星环把它拉回；暂停后调参数不会推进仿真。
  所有质心/算法/脉冲数字均来自 C++ 内核，页面只显示。
</div>

<script>
const $ = id => document.getElementById(id);
const C = {ra:'#58a6ff', dec:'#f0883e'};
let running = false, timer = null, lastFrame = -1;
const hist = {ra:[], dec:[], pra:[], pde:[]};
const MAXH = 300;

function setState(text, cls) {
  const el = $('state');
  el.textContent = text;
  el.className = 'sub ' + (cls || '');
}

async function cmd(c) {
  const r = await fetch('/api/cmd?c=' + encodeURIComponent(c));
  if (!r.ok) throw new Error('HTTP ' + r.status);
  return r.json();
}

function lineChart(cv, series) {
  const ctx = cv.getContext('2d');
  const W = cv.width, H = cv.height, L = 46, R = 10, T = 8, B = 20;
  ctx.clearRect(0, 0, W, H);
  const n = Math.max(1, series[0].values.length);
  let lo = 0, hi = 0;
  for (const s of series) for (const v of s.values) {
    if (!isFinite(v)) continue;
    lo = Math.min(lo, v); hi = Math.max(hi, v);
  }
  if (hi - lo < 1e-6) { hi += 0.1; lo -= 0.1; }
  const pad = (hi - lo) * 0.12; lo -= pad; hi += pad;
  const X = i => L + (W - L - R) * (n <= 1 ? 0 : i / (n - 1));
  const Y = v => T + (H - T - B) * (1 - (v - lo) / (hi - lo));
  ctx.font = '10px system-ui'; ctx.textBaseline = 'middle';
  for (let k = 0; k <= 4; k++) {
    const v = lo + (hi - lo) * k / 4, y = Y(v);
    ctx.strokeStyle = '#2a3441'; ctx.lineWidth = 1;
    ctx.beginPath(); ctx.moveTo(L, y); ctx.lineTo(W - R, y); ctx.stroke();
    ctx.fillStyle = '#8b949e'; ctx.textAlign = 'right';
    ctx.fillText(Math.abs(v) >= 100 ? v.toFixed(0) : v.toFixed(2), L - 5, y);
  }
  ctx.strokeStyle = '#3d4b5c'; ctx.setLineDash([4, 4]);
  ctx.beginPath(); ctx.moveTo(L, Y(0)); ctx.lineTo(W - R, Y(0)); ctx.stroke();
  ctx.setLineDash([]);
  for (const s of series) {
    if (!s.values.length) continue;
    ctx.strokeStyle = s.color; ctx.lineWidth = 1.8; ctx.beginPath();
    for (let i = 0; i < s.values.length; i++) {
      const x = X(i), y = Y(s.values[i]);
      if (i) ctx.lineTo(x, y); else ctx.moveTo(x, y);
    }
    ctx.stroke();
  }
}

function drawStar(d) {
  const cv = $('star'), ctx = cv.getContext('2d');
  const w = d.w, h = d.h;
  const bin = atob(d.img);
  const img = ctx.createImageData(w, h);
  for (let i = 0; i < w * h; i++) {
    const v = bin.charCodeAt(i);
    img.data[i*4] = v; img.data[i*4+1] = v; img.data[i*4+2] = v; img.data[i*4+3] = 255;
  }
  ctx.putImageData(img, 0, 0);
  const lx = d.lockX || w / 2, ly = d.lockY || h / 2;
  ctx.strokeStyle = '#3fb95099'; ctx.lineWidth = 1;
  ctx.strokeRect(lx - 15.5, ly - 15.5, 31, 31);
  ctx.strokeStyle = '#3fb950';
  ctx.beginPath();
  ctx.moveTo(lx - 7, ly); ctx.lineTo(lx - 2, ly);
  ctx.moveTo(lx + 2, ly); ctx.lineTo(lx + 7, ly);
  ctx.moveTo(lx, ly - 7); ctx.lineTo(lx, ly - 2);
  ctx.moveTo(lx, ly + 2); ctx.lineTo(lx, ly + 7);
  ctx.stroke();
}

function apply(d) {
  if (d.error) { setState(d.error, 'bad'); stop(); return; }
  if (d.frame < lastFrame) { hist.ra.length = hist.dec.length = hist.pra.length = hist.pde.length = 0; }
  if (d.frame !== lastFrame) {  // control commands echo the same frame; don't duplicate
    hist.ra.push(d.ra); hist.dec.push(d.dec);
    hist.pra.push(d.pulseRA); hist.pde.push(d.pulseDec);
    for (const k in hist) if (hist[k].length > MAXH) hist[k].shift();
  }
  lastFrame = d.frame;

  $('cFrame').textContent = d.frame;
  $('cRms').textContent = d.rmsRA.toFixed(3) + ' / ' + d.rmsDec.toFixed(3);
  $('cPeak').textContent = d.peakRA.toFixed(2) + ' / ' + d.peakDec.toFixed(2);
  $('cPulseNow').textContent = d.pulseRA + ' / ' + d.pulseDec + ' ms';
  $('cStar').textContent = d.snr.toFixed(0) + ' / ' + d.hfd.toFixed(2);
  $('cAngles').textContent = d.calibrated
      ? (d.xAngle * 180 / Math.PI).toFixed(1) + '° / ' + (d.yAngle * 180 / Math.PI).toFixed(1) + '°'
      : '未校准';
  $('cAngles').className = d.calibrated ? '' : 'bad';

  drawStar(d);
  lineChart($('cRes'), [
    {values: hist.ra, color: C.ra},
    {values: hist.dec, color: C.dec},
  ]);
  lineChart($('cPulse'), [
    {values: hist.pra, color: C.ra},
    {values: hist.pde, color: C.dec},
  ]);

  if (!d.found) setState('丢星，重新选星…', 'warn');
  else if (running) setState('导星中 · ' + d.algo, 'ok');
}

async function tick() {
  if (!running) return;
  try {
    apply(await cmd('step'));
  } catch (e) {
    setState('连接断开：' + e.message, 'bad');
    stop();
    return;
  }
  if (running) timer = setTimeout(tick, 100);
}

function stop() {
  running = false;
  clearTimeout(timer);
  $('run').textContent = '▶ 开始导星';
  $('run').classList.remove('stop');
  if (!/丢星|断开/.test($('state').textContent)) setState('暂停');
}

function start() {
  running = true;
  $('run').textContent = '⏸ 暂停';
  $('run').classList.add('stop');
  setState('导星中', 'ok');
  tick();
}

$('run').onclick = () => running ? stop() : start();

async function send(c) {
  try { apply(await cmd(c)); } catch (e) { setState('连接断开：' + e.message, 'bad'); stop(); }
}

$('algo').onchange = () => send('algo:' + $('algo').value);
$('dither').onclick = () => send('dither');
$('cal').onclick = () => { hist.ra.length = hist.dec.length = hist.pra.length = hist.pde.length = 0; send('calibrate'); };
$('reset').onclick = () => { hist.ra.length = hist.dec.length = hist.pra.length = hist.pde.length = 0; send('reset'); };

function slider(id, fmt, suffix) {
  const el = $(id), out = $(id + 'V');
  const upd = () => { out.textContent = fmt(+el.value) + suffix; };
  el.oninput = upd;
  el.onchange = () => { send(id + ':' + el.value); };
  upd();
}
slider('minmove', v => v.toFixed(2), ' px');
slider('seeing', v => v.toFixed(1), ' px');
slider('drift', v => v.toFixed(2), ' px/帧');

cmd('status').then(apply).catch(e => setState('无法连接内核：' + e.message, 'bad'));
if (new URLSearchParams(location.search).has('run')) start();
</script>
</body>
</html>
"""


if __name__ == "__main__":
    main()
