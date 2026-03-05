// ui_hud.js
// Anzeige während des Malens:
// - Modus: NORMAL oder SVG-SERIE (Index/Total + aktueller Name)
// - Strecke: Gesamt / Gemalt / Rest
// - Zeit: Laufzeit / Restzeit (ETA)
// - Geschwindigkeit: Durchschnitt (aus Strecke/Zeit)

(function () {
  "use strict";

  const state = {
    batch: { active: false, index: 0, total: 0, currentName: "" },
    job: { totalDistanceMm: null, startDistMm: 0 },
    timing: { startedAt: null, lastRunTs: null, accumSec: 0 },
    geom: { bbox: null, heightMm: null },
    warn: { message: "" },
    last: {
      progress: null,
      running: false,
      x: 0,
      y: 0,
      distSofarMm: null,
      distTotalMm: null,
      distDrawMm: null,
      distTravelMm: null,
      curSpeedMmS: null,
      printSteps: null,
      accelSteps: null,
      segApprox: null,
      _lastDistForSpeed: null,
      _lastSpeedTs: null
    }
  };

  let els = null;

  function fmtTime(sec) {
    sec = Math.max(0, Math.floor(Number(sec) || 0));
    const h = Math.floor(sec / 3600);
    const m = Math.floor((sec % 3600) / 60);
    const s = sec % 60;
    if (h > 0) return `${h}:${String(m).padStart(2,"0")}:${String(s).padStart(2,"0")}`;
    return `${m}:${String(s).padStart(2,"0")}`;
  }

  function fmtDist(mm) {
    const m = (Number(mm) || 0) / 1000;
    if (m < 10) return `${m.toFixed(2)} m`;
    if (m < 100) return `${m.toFixed(1)} m`;
    return `${Math.round(m)} m`;
  }

  function fmtSpeed(mmPerSec) {
    const v = Number(mmPerSec) || 0;
    if (v <= 0.0001) return "—";
    if (v < 10) return `${v.toFixed(2)} mm/s`;
    if (v < 100) return `${v.toFixed(1)} mm/s`;
    return `${v.toFixed(0)} mm/s`;
  }

  function ensureUi() {
    if (els) return els;

    const slide = document.getElementById("drawingBegan");
    if (!slide) return null;

    // Card unterhalb des Canvas (vor Buttons)
    const refCard = slide.querySelector(".job-canvas-card");
    if (!refCard) return null;

    // Mode-Badge im Canvas
    const wrap = refCard.querySelector(".job-canvas-wrap");
    let badge = document.getElementById("drawModeBadge");
    if (!badge) {
      badge = document.createElement("div");
      badge.id = "drawModeBadge";
      badge.style.cssText = `
        position:absolute; left:12px; top:12px; z-index:20;
        padding:8px 10px; border-radius:12px;
        background:rgba(0,0,0,0.55);
        border:1px solid rgba(255,255,255,0.16);
        color:rgba(255,255,255,0.92);
        font: 13px/1.2 system-ui, -apple-system, Segoe UI, Roboto, Arial;
        pointer-events:none;
        backdrop-filter: blur(10px);
        -webkit-backdrop-filter: blur(10px);
      `;
      wrap.appendChild(badge);
    }

    // Stats-Card
    let card = document.getElementById("drawStatsCard");
    if (!card) {
      card = document.createElement("div");
      card.id = "drawStatsCard";
      card.className = "telemetry card p-3 mb-3";
      card.style.marginTop = "10px";
      card.innerHTML = `
        <div class="d-flex align-items-center justify-content-between">
          <div class="fw-semibold" id="dsMode">Modus: —</div>
          <div class="small text-muted" id="dsName"></div>
        </div>

        <div class="mt-2" style="display:grid; grid-template-columns:1fr 1fr; gap:10px;">
          <div>
            <div class="small text-muted">Geschwindigkeit (aktuell / Ø)</div>
            <div class="fw-semibold"><span id="dsCurSpeed">—</span> <span class="text-muted">/</span> <span id="dsSpeed">—</span></div>
          </div>
          <div>
            <div class="small text-muted">Fortschritt</div>
            <div class="fw-semibold"><span id="dsProgress">—</span> <span class="text-muted">·</span> <span id="dsSeg">—</span></div>
          </div>

          <div>
            <div class="small text-muted">Strecke (sofar / rest)</div>
            <div class="fw-semibold"><span id="dsDone">—</span> <span class="text-muted">/</span> <span id="dsLeft">—</span></div>
          </div>
          <div>
            <div class="small text-muted">Position (X/Y)</div>
            <div class="fw-semibold" id="dsXY">—</div>
          </div>

          <div>
            <div class="small text-muted">Pen-Speed</div>
            <div class="fw-semibold" id="dsPenSpeed">—</div>
          </div>
          <div>
            <div class="small text-muted">Beschleunigung</div>
            <div class="fw-semibold" id="dsAccel">—</div>
          </div>

          <div style="grid-column: 1 / span 2;">
            <div class="small text-muted">Ausmaße (aus Commands)</div>
            <div class="fw-semibold" id="dsDims">—</div>
          </div>

          <div style="grid-column: 1 / span 2; display:none;" id="dsWarnWrap">
            <div class="small text-muted">Hinweis</div>
            <div class="fw-semibold" id="dsWarn">—</div>
          </div>
        </div>
      `;

      // Einfügen direkt nach Canvas-Card
      refCard.insertAdjacentElement("afterend", card);
    }

    els = {
      badge,
      mode: card.querySelector("#dsMode"),
      name: card.querySelector("#dsName"),
      elapsed: card.querySelector("#dsElapsed"),
      eta: card.querySelector("#dsEta"),
      curSpeed: card.querySelector("#dsCurSpeed"),
      progress: card.querySelector("#dsProgress"),
      seg: card.querySelector("#dsSeg"),
      speed: card.querySelector("#dsSpeed"),
      done: card.querySelector("#dsDone"),
      left: card.querySelector("#dsLeft"),
      xy: card.querySelector("#dsXY"),
      penSpeed: card.querySelector("#dsPenSpeed"),
      accel: card.querySelector("#dsAccel"),
      dims: card.querySelector("#dsDims"),
      warnWrap: card.querySelector("#dsWarnWrap"),
      warn: card.querySelector("#dsWarn")
    };
    return els;
  }

  async function loadDiagOnce() {
    if (loadDiagOnce._done) return loadDiagOnce._diag;
    loadDiagOnce._done = true;
    try {
      const r = await fetch("/diag", { cache: "no-store" });
      if (!r.ok) return null;
      const j = await r.json();
      loadDiagOnce._diag = j;
      state.last.printSteps = Number(j?.printSpeedSteps);
      state.last.accelSteps = Number(j?.acceleration);
      return j;
    } catch {
      return null;
    }
  }

  function update() {
    const ui = ensureUi();
    if (!ui) return;

    // non-blocking: try to read diag once, so we can show accel/pen-speed baselines
    loadDiagOnce().catch(()=>{});

    const isBatch = !!state.batch.active;
    const modeTxt = isBatch ? `SVG-SERIE (${state.batch.index + 1}/${state.batch.total})` : "NORMAL";
    const nameTxt = isBatch ? (state.batch.currentName || "") : "";

    ui.badge.textContent = isBatch ? `SVG-SERIE: ${state.batch.index + 1}/${state.batch.total}` : "NORMAL";
    ui.mode.textContent = `Modus: ${modeTxt}`;
    ui.name.textContent = nameTxt;

    const total = Number(state.job.totalDistanceMm);
    const startDist = Number(state.job.startDistMm) || 0;

    // Progress + distances: prefer firmware sofar/total if available, else fallback to %
    const prog = Number(state.last.progress);
    const p = Number.isFinite(prog) ? Math.max(0, Math.min(100, prog)) : 0;
    ui.progress.textContent = `${p.toFixed(0)}%`;

    const approxSeg = Number.isFinite(state.geom?.lineCount)
      ? Math.max(0, Math.min(state.geom.lineCount, Math.round((p / 100) * state.geom.lineCount)))
      : null;
    ui.seg.textContent = (approxSeg !== null) ? `Seg ${approxSeg}` : "Seg —";

    // Prefer firmware distance when present
    const sofar = Number(state.last.distSofarMm);
    const totalFw = Number(state.last.distTotalMm);
    let leftMm = null;
    let doneMm = null;

    if (Number.isFinite(sofar) && Number.isFinite(totalFw) && totalFw > 0) {
      doneMm = Math.max(0, sofar - startDist);
      leftMm = Math.max(0, totalFw - sofar);
    } else if (Number.isFinite(total) && total > 0) {
      const range = Math.max(0, total - startDist);
      doneMm = (p / 100) * range;
      leftMm = Math.max(0, range - doneMm);
    }

    ui.done.textContent = (doneMm !== null) ? fmtDist(doneMm) : "—";
    ui.left.textContent = (leftMm !== null) ? fmtDist(leftMm) : "—";

    // Current speed (from distance delta)
    ui.curSpeed.textContent = fmtSpeed(state.last.curSpeedMmS);

    // Avg speed
    const teleAvg = Number(state.last.avg_speed_mms);
    ui.speed.textContent = fmtSpeed(teleAvg);

    // XY
    ui.xy.textContent = `X ${Number(state.last.x || 0).toFixed(1)}  ·  Y ${Number(state.last.y || 0).toFixed(1)} mm`;

    // Pen speed + accel
    ui.penSpeed.textContent = Number.isFinite(state.last.printSteps) ? `${Math.round(state.last.printSteps)} steps/s` : "—";
    ui.accel.textContent = Number.isFinite(state.last.accelSteps) ? `${Math.round(state.last.accelSteps)} (steps/s²)` : "—";

    // Ausmaße aus Commands (bbox)
    try {
      const b = state.geom.bbox;
      if (b && Number.isFinite(b.maxX) && Number.isFinite(b.maxY)) {
        const maxX = Number(b.maxX) || 0;
        const maxY = Number(b.maxY) || 0;
        const minX = Number.isFinite(b.minX) ? Number(b.minX) : 0;
        const minY = Number.isFinite(b.minY) ? Number(b.minY) : 0;
        const wMm = Math.max(0, maxX - minX);
        const hMm = Math.max(0, maxY - minY);
        ui.dims.textContent = `maxX ${maxX.toFixed(1)} mm   maxY ${maxY.toFixed(1)} mm   Bild ${wMm.toFixed(1)} × ${hMm.toFixed(1)} mm`;
      } else {
        ui.dims.textContent = "—";
      }
    } catch {
      ui.dims.textContent = "—";
    }

    // Hinweise (nicht blockierend)
    const wmsg = String(state.warn.message || "").trim();
    if (wmsg.length > 0) {
      ui.warnWrap.style.display = "";
      ui.warn.textContent = wmsg;
    } else {
      ui.warnWrap.style.display = "none";
    }
  }

  // ---- Events ----
  window.addEventListener("svgBatch:state", (e) => {
    const d = e?.detail || {};
    state.batch.active = !!d.active;
    state.batch.index = Number(d.index || 0);
    state.batch.total = Number(d.total || 0);
    state.batch.currentName = String(d.currentName || "");
    update();
  });

  window.addEventListener("svgBatch:error", (e) => {
    // Fehler sichtbar im Badge
    const ui = ensureUi();
    if (!ui) return;
    const msg = String(e?.detail?.message || "Unbekannter Fehler");
    ui.badge.textContent = `SVG-SERIE FEHLER`;
    ui.mode.textContent = `Modus: FEHLER`;
    ui.name.textContent = msg;
  });

  window.addEventListener("mural:jobModel", (e) => {
    const d = e?.detail || {};
    const total = Number(d.totalDistanceMm);
    if (Number.isFinite(total) && total > 0) state.job.totalDistanceMm = total;
    if (d.bbox) state.geom.bbox = d.bbox;
    if (Number.isFinite(Number(d.heightMm))) state.geom.heightMm = Number(d.heightMm);
    if (Number.isFinite(Number(d.lineCount))) state.geom.lineCount = Number(d.lineCount);
    update();
  });

  window.addEventListener("mural:warn", (e) => {
    const d = e?.detail || {};
    state.warn.message = String(d.message || "");
    update();
  });

  window.addEventListener("mural:jobStarted", (e) => {
    const d = e?.detail || {};
    const now = Date.now();
    state.timing.startedAt = now;
    state.timing.lastRunTs = now;
    state.timing.accumSec = 0;
    state.job.startDistMm = Number(d.startDistMm || 0);
    state.last.running = true;
    update();
  });

  window.addEventListener("mural:jobStopped", () => {
    state.last.running = false;
    state.timing.lastRunTs = null;
    update();
  });

  window.addEventListener("mural:telemetry", (e) => {
    const d = e?.detail || {};
    state.last.progress = d.progress;
    state.last.x = Number(d.x || 0);
    state.last.y = Number(d.y || 0);

    state.last.distSofarMm = Number(d.dist_sofar_mm);
    state.last.distTotalMm = Number(d.dist_total_mm);
    state.last.distDrawMm = Number(d.dist_draw_mm);
    state.last.distTravelMm = Number(d.dist_travel_mm);
    state.last.printSteps = Number(d.printSteps);
    state.last.avg_speed_mms = Number(d.avg_speed_mms);

    const now = Date.now();
    const running = !!d.running && !d.paused;
    state.last.running = running;

    // Current speed estimate from distance delta (firmware distance), only while running
    try {
      const distNow = Number(d.dist_sofar_mm);
      if (running && Number.isFinite(distNow)) {
        const lastDist = state.last._lastDistForSpeed;
        const lastTs = state.last._lastSpeedTs;
        if (Number.isFinite(lastDist) && Number.isFinite(lastTs)) {
          const dt = Math.max(0.05, (now - lastTs) / 1000);
          const dv = distNow - lastDist;
          state.last.curSpeedMmS = Math.max(0, dv / dt);
        }
        state.last._lastDistForSpeed = distNow;
        state.last._lastSpeedTs = now;
      }
    } catch {}

    if (state.timing.startedAt && running) {
      if (!state.timing.lastRunTs) state.timing.lastRunTs = now;
      const dt = Math.max(0, (now - state.timing.lastRunTs) / 1000);
      state.timing.accumSec += dt;
      state.timing.lastRunTs = now;
    } else {
      // Pause/Stop -> Zeit nicht weiterlaufen lassen
      state.timing.lastRunTs = null;
    }

    // Falls wir mitten im Job einsteigen: Startpunkt setzen
    if (running && !state.timing.startedAt) {
      state.timing.startedAt = now;
      state.timing.lastRunTs = now;
      state.timing.accumSec = 0;
    }
    update();
  });

  // Initial render attempt
  document.addEventListener("DOMContentLoaded", () => {
    setTimeout(update, 200);
  });

})();
