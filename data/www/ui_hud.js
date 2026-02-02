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
    last: { progress: null, running: false }
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
            <div class="small text-muted">Zeit</div>
            <div class="fw-semibold"><span id="dsElapsed">—</span> <span class="text-muted">/ ETA</span> <span id="dsEta">—</span></div>
          </div>
          <div>
            <div class="small text-muted">Ø Geschwindigkeit</div>
            <div class="fw-semibold" id="dsSpeed">—</div>
          </div>
          <div>
            <div class="small text-muted">Strecke gesamt</div>
            <div class="fw-semibold" id="dsTotal">—</div>
          </div>
          <div>
            <div class="small text-muted">Gemalt / Rest</div>
            <div class="fw-semibold"><span id="dsDone">—</span> <span class="text-muted">/</span> <span id="dsLeft">—</span></div>
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
      speed: card.querySelector("#dsSpeed"),
      total: card.querySelector("#dsTotal"),
      done: card.querySelector("#dsDone"),
      left: card.querySelector("#dsLeft")
    };
    return els;
  }

  function update() {
    const ui = ensureUi();
    if (!ui) return;

    const isBatch = !!state.batch.active;
    const modeTxt = isBatch ? `SVG-SERIE (${state.batch.index + 1}/${state.batch.total})` : "NORMAL";
    const nameTxt = isBatch ? (state.batch.currentName || "") : "";

    ui.badge.textContent = isBatch ? `SVG-SERIE: ${state.batch.index + 1}/${state.batch.total}` : "NORMAL";
    ui.mode.textContent = `Modus: ${modeTxt}`;
    ui.name.textContent = nameTxt;

    const total = Number(state.job.totalDistanceMm);
    const startDist = Number(state.job.startDistMm) || 0;

    if (!Number.isFinite(total) || total <= 0) {
      ui.total.textContent = "—";
      ui.done.textContent = "—";
      ui.left.textContent = "—";
    } else {
      const range = Math.max(0, total - startDist);
      const progress = Number(state.last.progress);
      const p = Number.isFinite(progress) ? Math.max(0, Math.min(100, progress)) : 0;

      const done = (p / 100) * range;
      const left = Math.max(0, range - done);

      ui.total.textContent = fmtDist(range);
      ui.done.textContent = fmtDist(done);
      ui.left.textContent = fmtDist(left);

      // Zeit / Speed / ETA
      if (state.timing.startedAt) {
        const elapsedSec = Math.max(1, state.timing.accumSec || 0);
        ui.elapsed.textContent = fmtTime(elapsedSec);

        const avg = done / elapsedSec; // mm/s
        ui.speed.textContent = fmtSpeed(avg);

        if (avg > 0.0001) {
          ui.eta.textContent = fmtTime(left / avg);
        } else {
          ui.eta.textContent = "—";
        }
      } else {
        ui.elapsed.textContent = "—";
        ui.speed.textContent = "—";
        ui.eta.textContent = "—";
      }
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

    const now = Date.now();
    const running = !!d.running && !d.paused;
    state.last.running = running;

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
