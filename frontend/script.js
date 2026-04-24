/* =============================================================================
   DiskSim — script.js
   Handles: API calls · Canvas animation · Comparison chart · UI state
   ============================================================================= */

'use strict';

const API_BASE = 'http://127.0.0.1:5000';

/* ── DOM Refs ─────────────────────────────────────────────────────────────── */
const $ = id => document.getElementById(id);

const dom = {
  /* Inputs */
  diskSize:   $('diskSize'),
  headPos:    $('headPos'),
  queueInput: $('queueInput'),
  animSpeed:  $('animSpeed'),
  speedValue: $('speedValue'),

  /* Buttons */
  btnRun:     $('btnRun'),
  btnCompare: $('btnCompare'),
  btnRandom:  $('btnRandom'),

  /* Status */
  statusDot:  $('status-dot'),
  statusText: $('status-text'),

  /* Viz panel */
  vizTitle:   $('vizTitle'),
  vizMeta:    $('vizMeta'),
  canvas:     $('vizCanvas'),
  canvasIdle: $('canvasIdle'),
  canvasLoader: $('canvasLoader'),
  playbackBar:  $('playbackBar'),
  btnPlayPause: $('btnPlayPause'),
  playPauseIcon: $('playPauseIcon'),
  btnRestart:   $('btnRestart'),
  pbFill:       $('pbFill'),
  pbStep:       $('pbStep'),

  /* Stats */
  statsPanel:   $('statsPanel'),
  resAlgo:      $('resAlgo'),
  resSeek:      $('resSeek'),
  resCount:     $('resCount'),
  resAvg:       $('resAvg'),
  seqTrack:     $('sequenceTrack'),

  /* Compare */
  comparePanel: $('comparePanel'),
  compareTableBody: $('compareTableBody'),
  compareChart:     $('compareChart'),
  bestBadge:        $('bestBadge'),

  /* Toast / footer */
  toastContainer: $('toastContainer'),
  themeToggle:    $('themeToggle'),
};

/* ── State ────────────────────────────────────────────────────────────────── */
const state = {
  animFrame: null,     // requestAnimationFrame handle
  animPaused: false,
  animStep: 0,
  sequence: [],
  seekTime: 0,
  diskSize: 200,
  head: 50,
  animSpeed: 1,
  compareChartInstance: null,
};

/* ═══════════════════════════════════════════════════════════════════════════
   HELPERS
   ═══════════════════════════════════════════════════════════════════════════ */

function getAlgorithm() {
  return document.querySelector('input[name="algorithm"]:checked')?.value ?? 'fcfs';
}

function getDirection() {
  return parseInt(document.querySelector('input[name="direction"]:checked')?.value ?? '1', 10);
}

function parseQueue(raw) {
  return raw
    .split(/[\s,]+/)
    .map(s => s.trim())
    .filter(Boolean)
    .map(Number)
    .filter(n => !isNaN(n));
}

function readInputs() {
  return {
    algorithm: getAlgorithm(),
    head:      parseInt(dom.headPos.value,  10) || 0,
    disk_size: parseInt(dom.diskSize.value, 10) || 200,
    direction: getDirection(),
    queue:     parseQueue(dom.queueInput.value),
  };
}

function setStatus(state, label) {
  dom.statusDot.className  = 'status-dot' + (state ? ` ${state}` : '');
  dom.statusText.textContent = label;
}

/* ── Toast ────────────────────────────────────────────────────────────────── */
function showToast(msg, type = 'info') {
  const el = document.createElement('div');
  el.className = `toast toast--${type}`;
  el.textContent = msg;
  dom.toastContainer.appendChild(el);
  setTimeout(() => {
    el.classList.add('fade-out');
    el.addEventListener('animationend', () => el.remove());
  }, 3200);
}

/* ── Validation ───────────────────────────────────────────────────────────── */
function validateInputs({ head, disk_size, queue }) {
  if (isNaN(disk_size) || disk_size < 2)   { showToast('Disk size must be ≥ 2.', 'error'); return false; }
  if (isNaN(head) || head < 0)             { showToast('Head position must be ≥ 0.', 'error'); return false; }
  if (head >= disk_size)                   { showToast('Head must be less than disk size.', 'error'); return false; }
  if (queue.some(r => r < 0 || r >= disk_size)) {
    showToast(`All requests must be in [0, ${disk_size - 1}].`, 'error'); return false;
  }
  return true;
}

/* ═══════════════════════════════════════════════════════════════════════════
   API LAYER
   ═══════════════════════════════════════════════════════════════════════════ */

async function apiRunAlgorithm(params) {
  const res = await fetch(`${API_BASE}/run-algorithm`, {
    method:  'POST',
    headers: { 'Content-Type': 'application/json' },
    body:    JSON.stringify(params),
  });
  const data = await res.json();
  if (!res.ok || data.error) throw new Error(data.error || `HTTP ${res.status}`);
  return data;
}

async function apiCompareAll(params) {
  const res = await fetch(`${API_BASE}/compare-all`, {
    method:  'POST',
    headers: { 'Content-Type': 'application/json' },
    body:    JSON.stringify(params),
  });
  const data = await res.json();
  if (!res.ok) throw new Error(data.error || `HTTP ${res.status}`);
  return data;
}

/* ═══════════════════════════════════════════════════════════════════════════
   CANVAS VISUALIZER
   ═══════════════════════════════════════════════════════════════════════════ */

const CANVAS_H        = 340;
const PAD_LEFT        = 52;
const PAD_RIGHT       = 24;
const PAD_TOP         = 32;
const PAD_BOT         = 30;
const TRACK_Y_START   = PAD_TOP + 12;
const TRACK_Y_END     = CANVAS_H - PAD_BOT - 12;
const TRACK_Y_STEP    = 28;   // vertical spacing between cylinders drawn
const SEQ_DOT_R       = 5;    // radius of sequence dots

/* Return CSS variable value */
function cssVar(name) {
  return getComputedStyle(document.documentElement).getPropertyValue(name).trim();
}

function resizeCanvas() {
  const canvas = dom.canvas;
  const wrap   = canvas.parentElement;
  const W      = wrap.clientWidth;
  canvas.width  = W;
  canvas.height = CANVAS_H;
}

function drawFrame(ctx, sequence, step, diskSize, head) {
  const W = ctx.canvas.width;
  const H = ctx.canvas.height;

  const accent      = cssVar('--accent');
  const accent2     = cssVar('--accent2');
  const accent3     = cssVar('--accent3');
  const textDim     = cssVar('--text-dim');
  const textMid     = cssVar('--text-mid');
  const bgVoid      = cssVar('--bg-void');
  const borderDim   = cssVar('--border-dim');
  const borderMid   = cssVar('--border-mid');
  const accentGlow  = cssVar('--accent-glow');

  ctx.clearRect(0, 0, W, H);
  ctx.fillStyle = bgVoid;
  ctx.fillRect(0, 0, W, H);

  const drawW = W - PAD_LEFT - PAD_RIGHT;

  /* ── cylinder → x mapping ─────────────────────────────────── */
  const cylX = cyl => PAD_LEFT + (cyl / (diskSize - 1)) * drawW;

  /* ── Y positions for the up-to-step path ─────────────────── */
  /* Each step occupies a horizontal band — we draw a zigzag chart */
  const visibleSeq = sequence.slice(0, step + 1);
  const numSteps   = visibleSeq.length;
  const bandH      = (CANVAS_H - PAD_TOP - PAD_BOT) / Math.max(numSteps, 1);

  const stepY = i => PAD_TOP + i * bandH + bandH * 0.5;

  /* ── Draw cylinder axis labels ────────────────────────────── */
  ctx.font = `11px ${cssVar('--font-mono') || 'monospace'}`;
  ctx.fillStyle = textDim;
  ctx.textAlign = 'center';

  /* Tick count: spread ~8 ticks across the axis */
  const tickCount = Math.min(10, diskSize);
  for (let i = 0; i <= tickCount; i++) {
    const cyl = Math.round((diskSize - 1) * i / tickCount);
    const x   = cylX(cyl);
    ctx.fillStyle = borderMid;
    ctx.fillRect(x, PAD_TOP - 6, 1, CANVAS_H - PAD_TOP - PAD_BOT + 6);
    ctx.fillStyle = textDim;
    ctx.fillText(cyl, x, PAD_TOP - 10);
  }

  /* ── Draw path lines ──────────────────────────────────────── */
  for (let i = 0; i < numSteps - 1; i++) {
    const x1 = cylX(visibleSeq[i]);
    const y1 = stepY(i);
    const x2 = cylX(visibleSeq[i + 1]);
    const y2 = stepY(i + 1);

    /* Gradient segment */
    const grad = ctx.createLinearGradient(x1, y1, x2, y2);
    grad.addColorStop(0, accent + 'aa');
    grad.addColorStop(1, accent2 + 'aa');

    ctx.beginPath();
    ctx.moveTo(x1, y1);
    ctx.lineTo(x2, y2);
    ctx.strokeStyle = grad;
    ctx.lineWidth   = 2;
    ctx.setLineDash([]);
    ctx.stroke();

    /* Arrow tip */
    const angle = Math.atan2(y2 - y1, x2 - x1);
    const AL = 8, AW = 5;
    ctx.beginPath();
    ctx.moveTo(x2, y2);
    ctx.lineTo(
      x2 - AL * Math.cos(angle) + AW * Math.sin(angle),
      y2 - AL * Math.sin(angle) - AW * Math.cos(angle)
    );
    ctx.lineTo(
      x2 - AL * Math.cos(angle) - AW * Math.sin(angle),
      y2 - AL * Math.sin(angle) + AW * Math.cos(angle)
    );
    ctx.closePath();
    ctx.fillStyle = accent2;
    ctx.fill();
  }

  /* ── Draw dots for each stop ──────────────────────────────── */
  for (let i = 0; i < numSteps; i++) {
    const x = cylX(visibleSeq[i]);
    const y = stepY(i);
    const isHead = i === 0;
    const isCurrent = i === numSteps - 1;

    /* Glow halo for head and current */
    if (isHead || isCurrent) {
      ctx.beginPath();
      ctx.arc(x, y, SEQ_DOT_R + 5, 0, Math.PI * 2);
      ctx.fillStyle = (isHead ? accent2 : accent) + '30';
      ctx.fill();
    }

    ctx.beginPath();
    ctx.arc(x, y, SEQ_DOT_R, 0, Math.PI * 2);
    ctx.fillStyle = isHead ? accent2 : isCurrent ? accent : (accent + '99');
    ctx.fill();
    ctx.strokeStyle = bgVoid;
    ctx.lineWidth = 1.5;
    ctx.stroke();

    /* Label: cylinder number */
    ctx.font = `10px ${cssVar('--font-mono') || 'monospace'}`;
    ctx.textAlign = i % 2 === 0 ? 'left' : 'right';
    const labelX = x + (i % 2 === 0 ? 8 : -8);
    ctx.fillStyle = isCurrent ? accent : isHead ? accent2 : textMid;
    ctx.fillText(visibleSeq[i], labelX, y + 4);
  }

  /* ── Step labels on left axis ─────────────────────────────── */
  ctx.textAlign = 'right';
  ctx.font = `9px ${cssVar('--font-mono') || 'monospace'}`;
  for (let i = 0; i < numSteps; i++) {
    const y = stepY(i);
    ctx.fillStyle = i === numSteps - 1 ? accent : textDim;
    ctx.fillText(i === 0 ? 'HEAD' : `S${i}`, PAD_LEFT - 6, y + 3);
  }

  /* ── Boundary lines (0 and disk_size-1) ─────────────────── */
  const drawBoundary = (cyl, color) => {
    const x = cylX(cyl);
    ctx.beginPath();
    ctx.setLineDash([4, 3]);
    ctx.strokeStyle = color + '55';
    ctx.lineWidth = 1;
    ctx.moveTo(x, PAD_TOP - 2);
    ctx.lineTo(x, CANVAS_H - PAD_BOT + 2);
    ctx.stroke();
    ctx.setLineDash([]);
  };
  drawBoundary(0, accent3);
  drawBoundary(diskSize - 1, accent3);
}

/* ── Animation controller ─────────────────────────────────────────────────── */
function startAnimation(sequence, diskSize, head) {
  cancelAnimation();

  state.sequence = sequence;
  state.diskSize = diskSize;
  state.head     = head;
  state.animStep = 0;
  state.animPaused = false;
  dom.playPauseIcon.textContent = '⏸';

  dom.playbackBar.hidden  = false;
  dom.canvasIdle.setAttribute('aria-hidden', 'true');

  const ctx    = dom.canvas.getContext('2d');
  const total  = sequence.length - 1;

  /* Steps per second = base 1.2 × speed multiplier */
  const BASE_SPS = 1.2;

  let lastTime = null;
  let accumMs  = 0;

  function tick(timestamp) {
    if (!lastTime) lastTime = timestamp;
    const dt = timestamp - lastTime;
    lastTime  = timestamp;

    if (!state.animPaused) {
      accumMs += dt;
      const msPerStep = 1000 / (BASE_SPS * state.animSpeed);
      while (accumMs >= msPerStep && state.animStep < total) {
        state.animStep++;
        accumMs -= msPerStep;
      }
    }

    drawFrame(ctx, sequence, state.animStep, diskSize, head);

    /* Update progress */
    const pct = total > 0 ? (state.animStep / total) * 100 : 100;
    dom.pbFill.style.width = pct + '%';
    dom.pbStep.textContent = `${state.animStep} / ${total}`;

    /* Highlight sequence tokens */
    const items = dom.seqTrack.querySelectorAll('.seq-item');
    items.forEach((el, i) => {
      el.classList.toggle('active', i === state.animStep);
      el.classList.toggle('head',   i === 0);
    });
    if (items[state.animStep]) {
      items[state.animStep].scrollIntoView({ block: 'nearest', behavior: 'smooth' });
    }

    if (state.animStep < total) {
      state.animFrame = requestAnimationFrame(tick);
    } else {
      setStatus('', 'DONE');
    }
  }

  resizeCanvas();
  state.animFrame = requestAnimationFrame(tick);
}

function cancelAnimation() {
  if (state.animFrame) {
    cancelAnimationFrame(state.animFrame);
    state.animFrame = null;
  }
}

/* ═══════════════════════════════════════════════════════════════════════════
   RUN ALGORITHM
   ═══════════════════════════════════════════════════════════════════════════ */

async function handleRun() {
  const inputs = readInputs();
  if (!validateInputs(inputs)) return;

  setStatus('active', 'RUNNING');
  dom.btnRun.disabled = true;

  /* Show loader, hide idle */
  dom.canvasIdle.setAttribute('aria-hidden', 'true');
  dom.canvasLoader.hidden = false;
  dom.statsPanel.hidden   = true;
  dom.comparePanel.hidden = true;
  dom.playbackBar.hidden  = true;

  try {
    const data = await apiRunAlgorithm(inputs);

    dom.canvasLoader.hidden = false;

    /* Fill stats */
    const seq    = data.sequence;
    const seek   = data.seek_time;
    const count  = seq.length - 1;  /* exclude initial head */
    const avg    = count > 0 ? (seek / count).toFixed(1) : '0';

    dom.resAlgo.textContent  = inputs.algorithm.toUpperCase();
    dom.resSeek.textContent  = seek;
    dom.resCount.textContent = count;
    dom.resAvg.textContent   = avg;
    dom.vizTitle.textContent = `Head Movement — ${inputs.algorithm.toUpperCase()}`;
    dom.vizMeta.textContent  = `seek: ${seek} | steps: ${count}`;

    /* Build sequence tokens */
    dom.seqTrack.innerHTML = '';
    seq.forEach((cyl, i) => {
      const el = document.createElement('span');
      el.className = 'seq-item' + (i === 0 ? ' head' : '');
      el.textContent = cyl;
      dom.seqTrack.appendChild(el);
    });

    dom.statsPanel.hidden   = false;
    dom.canvasLoader.hidden = true;

    /* Kick off canvas animation */
    startAnimation(seq, inputs.disk_size, inputs.head);
    setStatus('active', 'ANIMATING');

    showToast(`${inputs.algorithm.toUpperCase()} complete · seek time: ${seek}`, 'success');
  } catch (err) {
    dom.canvasLoader.hidden = true;
    dom.canvasIdle.setAttribute('aria-hidden', 'false');
    setStatus('error', 'ERROR');
    showToast(`Error: ${err.message}`, 'error');
  } finally {
    dom.btnRun.disabled = false;
  }
}

/* ═══════════════════════════════════════════════════════════════════════════
   COMPARE ALL
   ═══════════════════════════════════════════════════════════════════════════ */

async function handleCompare() {
  const inputs = readInputs();
  if (!validateInputs(inputs)) return;

  setStatus('active', 'COMPARING');
  dom.btnCompare.disabled = true;

  dom.canvasIdle.setAttribute('aria-hidden', 'true');
  dom.canvasLoader.hidden = false;
  dom.comparePanel.hidden = true;
  dom.statsPanel.hidden   = true;
  dom.playbackBar.hidden  = true;
  cancelAnimation();

  try {
    const { head, disk_size, direction, queue } = inputs;
    const data = await apiCompareAll({ head, disk_size, direction, queue });

    dom.canvasLoader.hidden = true;

    renderCompareTable(data);
    renderCompareChart(data);

    dom.comparePanel.hidden = false;

    /* Show best badge */
    if (data.best) {
      dom.bestBadge.textContent = `★ BEST: ${data.best.toUpperCase()}`;
      dom.bestBadge.hidden = false;
    }

    setStatus('', 'DONE');
    showToast(`Comparison complete · best: ${data.best?.toUpperCase()}`, 'success');
  } catch (err) {
    dom.canvasLoader.hidden = true;
    dom.canvasIdle.setAttribute('aria-hidden', 'false');
    setStatus('error', 'ERROR');
    showToast(`Error: ${err.message}`, 'error');
  } finally {
    dom.btnCompare.disabled = false;
  }
}

function renderCompareTable(data) {
  const { results, best, worst } = data;
  const tbody = dom.compareTableBody;
  tbody.innerHTML = '';

  const algoOrder = ['fcfs', 'sstf', 'scan', 'cscan'];
  algoOrder.forEach(algo => {
    const r = results[algo];
    if (!r) return;

    const tr = document.createElement('tr');
    if (algo === best)  tr.classList.add('row-best');

    const statusHtml = algo === best
      ? '<span class="status-chip best">★ BEST</span>'
      : algo === worst
        ? '<span class="status-chip worst">▼ WORST</span>'
        : '<span class="status-chip ok">—</span>';

    tr.innerHTML = `
      <td>${algo.toUpperCase()}</td>
      <td>${r.error ? '—' : r.seek_time}</td>
      <td>${r.error ? '—' : r.sequence.length - 1}</td>
      <td>${r.error ? `<span class="status-chip worst">ERR</span>` : statusHtml}</td>
    `;
    tbody.appendChild(tr);
  });
}

function renderCompareChart(data) {
  const { results } = data;

  const labels = ['FCFS', 'SSTF', 'SCAN', 'C-SCAN'];
  const keys   = ['fcfs', 'sstf', 'scan', 'cscan'];
  const values = keys.map(k => results[k]?.seek_time ?? 0);

  const accent  = cssVar('--accent');
  const accent2 = cssVar('--accent2');
  const accent3 = cssVar('--accent3');
  const textMid = cssVar('--text-mid');
  const textDim = cssVar('--text-dim');
  const bgPanel = cssVar('--bg-panel-alt');

  /* Pick colour per bar — best = accent, worst = accent3 */
  const best  = Object.keys(results).reduce((a, b) => (results[a]?.seek_time ?? Infinity) < (results[b]?.seek_time ?? Infinity) ? a : b);
  const worst = Object.keys(results).reduce((a, b) => (results[a]?.seek_time ?? 0) > (results[b]?.seek_time ?? 0) ? a : b);

  const colors = keys.map(k =>
    k === best  ? accent + 'cc' :
    k === worst ? accent3 + 'cc' :
    accent2 + '88'
  );

  if (state.compareChartInstance) {
    state.compareChartInstance.destroy();
  }

  state.compareChartInstance = new Chart(dom.compareChart, {
    type: 'bar',
    data: {
      labels,
      datasets: [{
        label: 'Seek Time',
        data:  values,
        backgroundColor: colors,
        borderColor:     colors.map(c => c.slice(0, 7)),
        borderWidth: 1,
        borderRadius: 3,
      }],
    },
    options: {
      responsive:          true,
      maintainAspectRatio: false,
      plugins: {
        legend: { display: false },
        tooltip: {
          backgroundColor: bgPanel,
          titleColor:      textMid,
          bodyColor:       accent,
          borderColor:     cssVar('--border-mid'),
          borderWidth:     1,
          callbacks: {
            label: ctx => ` Seek time: ${ctx.raw}`,
          },
        },
      },
      scales: {
        x: {
          ticks:  { color: textMid, font: { family: "'Share Tech Mono', monospace", size: 11 } },
          grid:   { color: cssVar('--border-dim') },
        },
        y: {
          ticks:  { color: textDim, font: { family: "'Share Tech Mono', monospace", size: 10 } },
          grid:   { color: cssVar('--border-dim') },
          beginAtZero: true,
        },
      },
    },
  });
}

/* ═══════════════════════════════════════════════════════════════════════════
   RANDOM INPUTS
   ═══════════════════════════════════════════════════════════════════════════ */

function handleRandom() {
  const diskSize = 200;
  const head     = Math.floor(Math.random() * (diskSize - 1));
  const count    = Math.floor(Math.random() * 8) + 4;
  const queue    = Array.from({ length: count }, () =>
    Math.floor(Math.random() * (diskSize - 1))
  );

  dom.diskSize.value   = diskSize;
  dom.headPos.value    = head;
  dom.queueInput.value = queue.join(', ');

  showToast('Random inputs generated!', 'success');
}

/* ═══════════════════════════════════════════════════════════════════════════
   PLAYBACK CONTROLS
   ═══════════════════════════════════════════════════════════════════════════ */

dom.btnPlayPause.addEventListener('click', () => {
  state.animPaused = !state.animPaused;
  dom.playPauseIcon.textContent = state.animPaused ? '▶' : '⏸';
});

dom.btnRestart.addEventListener('click', () => {
  if (!state.sequence.length) return;
  startAnimation(state.sequence, state.diskSize, state.head);
});

/* ═══════════════════════════════════════════════════════════════════════════
   SPEED SLIDER
   ═══════════════════════════════════════════════════════════════════════════ */

dom.animSpeed.addEventListener('input', () => {
  const v = parseFloat(dom.animSpeed.value);
  state.animSpeed = v;
  dom.speedValue.textContent = `${v}×`;

  /* Update slider track fill */
  const min = parseFloat(dom.animSpeed.min);
  const max = parseFloat(dom.animSpeed.max);
  const pct = ((v - min) / (max - min)) * 100;
  dom.animSpeed.style.background =
    `linear-gradient(to right, var(--accent) ${pct}%, var(--border-subtle) ${pct}%)`;
});

/* Initial slider fill */
(function initSlider() {
  const v   = parseFloat(dom.animSpeed.value);
  const min = parseFloat(dom.animSpeed.min);
  const max = parseFloat(dom.animSpeed.max);
  const pct = ((v - min) / (max - min)) * 100;
  dom.animSpeed.style.background =
    `linear-gradient(to right, var(--accent) ${pct}%, var(--border-subtle) ${pct}%)`;
})();

/* ═══════════════════════════════════════════════════════════════════════════
   THEME TOGGLE
   ═══════════════════════════════════════════════════════════════════════════ */

dom.themeToggle.addEventListener('click', () => {
  const html    = document.documentElement;
  const current = html.getAttribute('data-theme');
  html.setAttribute('data-theme', current === 'dark' ? 'light' : 'dark');
});

/* ═══════════════════════════════════════════════════════════════════════════
   ALGO CARD — keyboard nav
   ═══════════════════════════════════════════════════════════════════════════ */

document.querySelectorAll('.algo-card').forEach(card => {
  card.setAttribute('tabindex', '0');
  card.addEventListener('keydown', e => {
    if (e.key === ' ' || e.key === 'Enter') {
      e.preventDefault();
      card.querySelector('input').checked = true;
    }
  });
});

/* ═══════════════════════════════════════════════════════════════════════════
   CANVAS RESIZE
   ═══════════════════════════════════════════════════════════════════════════ */

let resizeTimer;
window.addEventListener('resize', () => {
  clearTimeout(resizeTimer);
  resizeTimer = setTimeout(() => {
    resizeCanvas();
    if (state.sequence.length) {
      const ctx = dom.canvas.getContext('2d');
      drawFrame(ctx, state.sequence, state.animStep, state.diskSize, state.head);
    }
  }, 120);
});

/* Initial canvas size */
resizeCanvas();

/* ═══════════════════════════════════════════════════════════════════════════
   BUTTON EVENTS
   ═══════════════════════════════════════════════════════════════════════════ */

dom.btnRun.addEventListener('click', handleRun);
dom.btnCompare.addEventListener('click', handleCompare);
dom.btnRandom.addEventListener('click', handleRandom);

/* ── Enter key submits run ──────────────────────────────────────────────── */
document.addEventListener('keydown', e => {
  if (e.key === 'Enter' && !e.shiftKey && document.activeElement !== dom.queueInput) {
    handleRun();
  }
});

/* ── Initial idle state ─────────────────────────────────────────────────── */
setStatus('', 'IDLE');
dom.canvasIdle.removeAttribute('aria-hidden');