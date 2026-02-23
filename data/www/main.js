import * as svgControl from './svgControl.js';
import * as client from './client.js';
import { analyzePngLayers, buildCrassSvgFromPng, buildPngOutlineAndFillJob } from "./png_layers.js";
const enableState = {
  left: false,
  right: false
};

const UI_TUNING = {
  PARSE_LINES_PER_TICK: 300,   // kleiner = weniger Hänger
  DRAW_SEGMENTS_PER_TICK: 250, // kleiner = weniger Hänger
  STATUS_POLL_MS: 350,
  PREVIEW_FPS: 24
};

const nextTick = () => new Promise(r => setTimeout(r, 0));

const UI_TUNING_KEY = "mural_ui_tuning_v1";
const UI_TUNING_DEFAULTS = {
  PARSE_LINES_PER_TICK: 300,
  DRAW_SEGMENTS_PER_TICK: 250,
  STATUS_POLL_MS: 350,
  PREVIEW_FPS: 24
};

function clampInt(v, minV, maxV, fallback) {
  let n = parseInt(v, 10);
  if (!Number.isFinite(n)) n = fallback;
  return Math.max(minV, Math.min(maxV, n));
}

function loadUiTuning() {
  try {
    const raw = localStorage.getItem(UI_TUNING_KEY);
    if (!raw) return null;
    const obj = JSON.parse(raw);
    if (!obj || typeof obj !== "object") return null;
    return obj;
  } catch {
    return null;
  }
}

function saveUiTuning(obj) {
  try {
    localStorage.setItem(UI_TUNING_KEY, JSON.stringify(obj));
  } catch {}
}

function setEnableBtnVisual(btn, isOn, gpio) {
  if (!btn) return;

  btn.classList.toggle("btn-success", isOn);
  btn.classList.toggle("btn-outline-light", !isOn);

  btn.textContent = `Freigabe GPIO ${gpio}: ${isOn ? "AN" : "AUS"}`;
}

(function applySavedUiTuning() {
  const saved = loadUiTuning();
  const src = saved && typeof saved === "object" ? saved : UI_TUNING_DEFAULTS;
  UI_TUNING.PARSE_LINES_PER_TICK   = clampInt(src.PARSE_LINES_PER_TICK,   50, 5000, UI_TUNING_DEFAULTS.PARSE_LINES_PER_TICK);
  UI_TUNING.DRAW_SEGMENTS_PER_TICK = clampInt(src.DRAW_SEGMENTS_PER_TICK, 50, 5000, UI_TUNING_DEFAULTS.DRAW_SEGMENTS_PER_TICK);
  UI_TUNING.STATUS_POLL_MS         = clampInt(src.STATUS_POLL_MS,         80, 5000, UI_TUNING_DEFAULTS.STATUS_POLL_MS);
  UI_TUNING.PREVIEW_FPS            = clampInt(src.PREVIEW_FPS,            5, 60,   UI_TUNING_DEFAULTS.PREVIEW_FPS);
})();

const liveHud = {
  phaseName: "—",
  printSteps: 0,
  penPos: "—",
  penAngle: 0,
  penDownAngle: 80,
  penUpAngle: 80,
  pendingDown: 80,
  pendingUp: 80,
  hasPendingDown: false,
  hasPendingUp: false,
  fwMaxLoopMs: 0
};

const perfStats = {
  parseMs: 0,
  planMs: 0,
  initMs: 0,
  workerMs: 0,

  fwLoopMs: 0,
  fwYieldMs: 0,
  fwMoveMs: 0,
  fwRunnerMs: 0,
  fwPhaseMs: 0,
  fwMaxLoopMs: 0
};

function setPerfStat(key, value) {
  if (!Object.prototype.hasOwnProperty.call(perfStats, key)) return;
  const n = Number(value);
  perfStats[key] = Number.isFinite(n) ? n : 0;
  updatePerfHud();
  updatePerfSettingsMeasured();
}

function fmtMs(v) {
  const n = Number(v);
  if (!Number.isFinite(n) || n <= 0) return "—";
  if (n < 10) return n.toFixed(2) + " ms";
  if (n < 100) return n.toFixed(1) + " ms";
  return Math.round(n) + " ms";
}

function updatePerfHud() {
  const hud = document.getElementById("perfHud");
  if (!hud) return;

  const penTxt = (liveHud.penPos || "—") + (Number.isFinite(Number(liveHud.penAngle)) ? ` (${Number(liveHud.penAngle)}°)` : "");
  const maxLoop = (Number.isFinite(Number(liveHud.fwMaxLoopMs)) && Number(liveHud.fwMaxLoopMs) > 0)
    ? fmtMs(liveHud.fwMaxLoopMs)
    : fmtMs(perfStats.fwMaxLoopMs);

  hud.innerHTML = `
    <div class="perfHudTitle">📡 Live</div>
    <div class="perfHudRow"><span>Phase</span><b>${escapeHtml(liveHud.phaseName || "—")}</b></div>
    <div class="perfHudRow"><span>Print Steps</span><b>${escapeHtml(liveHud.printSteps ?? "—")}</b></div>
    <div class="perfHudRow"><span>Stift</span><b>${escapeHtml(penTxt)}</b></div>
    <div class="perfHudRow"><span>FW MaxLoop</span><b>${escapeHtml(maxLoop)}</b></div>
  `;
}

function updatePerfSettingsMeasured() {
  const setTxt = (id, v) => {
    const el = document.getElementById(id);
    if (!el) return;
    el.textContent = v;
  };

  setTxt("uiPerfMeasuredParse",  fmtMs(perfStats.parseMs));
  setTxt("uiPerfMeasuredPlan",   fmtMs(perfStats.planMs));
  setTxt("uiPerfMeasuredWorker", fmtMs(perfStats.workerMs));

  setTxt("uiPerfMeasuredLoop",   fmtMs(perfStats.fwLoopMs));
  setTxt("uiPerfMeasuredYield",  fmtMs(perfStats.fwYieldMs));
  setTxt("uiPerfMeasuredMove",   fmtMs(perfStats.fwMoveMs));
  setTxt("uiPerfMeasuredRunner", fmtMs(perfStats.fwRunnerMs));
  setTxt("uiPerfMeasuredPhase",  fmtMs(perfStats.fwPhaseMs));
  setTxt("uiPerfMeasuredMax",    fmtMs(perfStats.fwMaxLoopMs));
}

function initPerfSettingsUi() {
  const inpParse = document.getElementById("uiParseLinesTick");
  const inpDraw  = document.getElementById("uiDrawSegTick");
  const inpPoll  = document.getElementById("uiStatusPollMs");
  const inpFps   = document.getElementById("uiPreviewFps");

  const btnSave  = document.getElementById("uiPerfSaveBtn");
  const btnReset = document.getElementById("uiPerfResetBtn");

  const applyToInputs = () => {
    if (inpParse) inpParse.value = String(UI_TUNING.PARSE_LINES_PER_TICK);
    if (inpDraw)  inpDraw.value  = String(UI_TUNING.DRAW_SEGMENTS_PER_TICK);
    if (inpPoll)  inpPoll.value  = String(UI_TUNING.STATUS_POLL_MS);
    if (inpFps)   inpFps.value   = String(UI_TUNING.PREVIEW_FPS);
  };

  applyToInputs();
  updatePerfHud();
  updatePerfSettingsMeasured();

  const applyAndPersist = () => {
    UI_TUNING.PARSE_LINES_PER_TICK   = clampInt(inpParse?.value, 50, 5000, UI_TUNING_DEFAULTS.PARSE_LINES_PER_TICK);
    UI_TUNING.DRAW_SEGMENTS_PER_TICK = clampInt(inpDraw?.value,  50, 5000, UI_TUNING_DEFAULTS.DRAW_SEGMENTS_PER_TICK);
    UI_TUNING.STATUS_POLL_MS         = clampInt(inpPoll?.value,  80, 5000, UI_TUNING_DEFAULTS.STATUS_POLL_MS);
    UI_TUNING.PREVIEW_FPS            = clampInt(inpFps?.value,   5,  60,   UI_TUNING_DEFAULTS.PREVIEW_FPS);

    saveUiTuning({
      PARSE_LINES_PER_TICK: UI_TUNING.PARSE_LINES_PER_TICK,
      DRAW_SEGMENTS_PER_TICK: UI_TUNING.DRAW_SEGMENTS_PER_TICK,
      STATUS_POLL_MS: UI_TUNING.STATUS_POLL_MS,
      PREVIEW_FPS: UI_TUNING.PREVIEW_FPS
    });

    applyToInputs();

    // Poll-Intervalle sofort anpassen
    stopPerfPoll();
    startPerfPoll();
    if (telemetryTimer) {
      stopTelemetry();
      startTelemetry();

    // Event: Job gestartet (für Zeit/Speed/ETA Anzeige)
    try {
      window.dispatchEvent(new CustomEvent("mural:jobStarted", {
        detail: {
          startLine: selectedStartLine,
          startDistMm: (typeof selectedStartDist === "number" ? selectedStartDist : 0),
          totalDistanceMm: (jobModel && jobModel.totalDistance) ? jobModel.totalDistance : null,
          ts: Date.now()
        }
      }));
    } catch {}
    }

    if (window.addMessage) window.addMessage(0, "UI-Performance", "Einstellungen gespeichert");
  };

  if (btnSave) btnSave.onclick = (e) => { e.preventDefault(); applyAndPersist(); };
  if (btnReset) btnReset.onclick = (e) => {
    e.preventDefault();
    UI_TUNING.PARSE_LINES_PER_TICK = UI_TUNING_DEFAULTS.PARSE_LINES_PER_TICK;
    UI_TUNING.DRAW_SEGMENTS_PER_TICK = UI_TUNING_DEFAULTS.DRAW_SEGMENTS_PER_TICK;
    UI_TUNING.STATUS_POLL_MS = UI_TUNING_DEFAULTS.STATUS_POLL_MS;
    UI_TUNING.PREVIEW_FPS = UI_TUNING_DEFAULTS.PREVIEW_FPS;
    saveUiTuning(UI_TUNING_DEFAULTS);
    applyToInputs();
    stopPerfPoll();
    startPerfPoll();
    if (telemetryTimer) { stopTelemetry(); startTelemetry(); }
    if (window.addMessage) window.addMessage(0, "UI-Performance", "Defaults geladen");
  };
}

async function ui_initEnablePinToggles() {
  const btn27 = document.getElementById("drvEnable27");
  const btn33 = document.getElementById("drvEnable33");
  if (!btn27 || !btn33) return;

  const paint = (btn, gpio, on) => {
    btn.classList.toggle("btn-enable-on", !!on);
    btn.classList.toggle("btn-enable-off", !on);
    btn.textContent = `GPIO ${gpio}: ${on ? "AN" : "AUS"}`;
  };

  async function getState() {
    const r = await fetch("/gpio/driverEnable", { cache: "no-store" });
    if (!r.ok) throw new Error("HTTP " + r.status);
    return await r.json(); // {left,right}
  }

  async function setState(leftOn, rightOn) {
    const body = new URLSearchParams({
      left: leftOn ? "1" : "0",
      right: rightOn ? "1" : "0",
    });

    const r = await fetch("/gpio/driverEnable", { method: "POST", body });
    let j = null; try { j = await r.json(); } catch {}
    if (!r.ok) throw new Error(j?.error || ("HTTP " + r.status));
    return j;
  }

  // 1) initial state holen
  try {
    const st = await getState();
    enableState.left = !!st.left;
    enableState.right = !!st.right;
  } catch (e) {
    if (window.addMessage) window.addMessage(2, "Freigabe", String(e?.message || e));
  }

  // 2) anzeigen
  paint(btn27, 27, enableState.left);
  paint(btn33, 33, enableState.right);

  // 3) click handlers (Buttons bleiben da, wir togglen nur state)
  btn27.onclick = async () => {
    btn27.disabled = true;
    try {
      const st = await setState(!enableState.left, enableState.right);
      enableState.left = !!st.left;
      enableState.right = !!st.right;
      paint(btn27, 27, enableState.left);
      paint(btn33, 33, enableState.right);
      if (window.addMessage) window.addMessage(0, "Freigabe", `GPIO 27: ${enableState.left ? "AN" : "AUS"}`);
    } catch (e) {
      if (window.addMessage) window.addMessage(2, "Freigabe", String(e?.message || e));
    } finally {
      btn27.disabled = false;
    }
  };

  btn33.onclick = async () => {
    btn33.disabled = true;
    try {
      const st = await setState(enableState.left, !enableState.right);
      enableState.left = !!st.left;
      enableState.right = !!st.right;
      paint(btn27, 27, enableState.left);
      paint(btn33, 33, enableState.right);
      if (window.addMessage) window.addMessage(0, "Freigabe", `GPIO 33: ${enableState.right ? "AN" : "AUS"}`);
    } catch (e) {
      if (window.addMessage) window.addMessage(2, "Freigabe", String(e?.message || e));
    } finally {
      btn33.disabled = false;
    }
  };
}


const byId = (id) => document.getElementById(id);

function showEl(el, on = true) {
  if (!el) return;
  el.style.display = on ? "" : "none";
}

function setDisabled(el, disabled) {
  if (!el) return;
  el.disabled = !!disabled;
  el.classList.toggle("disabled", !!disabled);
}

function createLayerRow(layer, checked = true) {
  const btn = document.createElement("button");
  btn.type = "button";
  btn.className = "btn btn-outline-light text-start d-flex align-items-center gap-2";
  btn.dataset.hex = layer.hex;
  btn.dataset.checked = checked ? "1" : "0";

  const sw = document.createElement("span");
  sw.style.width = "18px";
  sw.style.height = "18px";
  sw.style.borderRadius = "6px";
  sw.style.border = "1px solid rgba(255,255,255,0.35)";
  sw.style.background = layer.hex;

  const label = document.createElement("span");
  const kind = layer.kind === "black" ? "Schwarz/Outline" : layer.hex.toUpperCase();
  label.innerHTML = `<div class="fw-semibold">${kind}</div><div class="small opacity-75">${layer.count} px</div>`;

  const tick = document.createElement("span");
  tick.className = "ms-auto";
  tick.textContent = checked ? "✓" : "";

  btn.appendChild(sw);
  btn.appendChild(label);
  btn.appendChild(tick);

  btn.addEventListener("click", () => {
    const now = btn.dataset.checked !== "1";
    btn.dataset.checked = now ? "1" : "0";
    tick.textContent = now ? "✓" : "";
    btn.classList.toggle("btn-outline-light", now);
    btn.classList.toggle("btn-outline-secondary", !now);
  });

  return btn;
}

function getSelectedLayerHexes() {
  const list = byId("layerList");
  if (!list) return [];
  const items = Array.from(list.querySelectorAll("button[data-hex]"));
  return items.filter(b => b.dataset.checked === "1").map(b => b.dataset.hex);
}

async function updatePngLayerDetection(file) {
  const tolerance = parseInt(byId("pngTolerance")?.value ?? "70", 10);
  const minPixels = parseInt(byId("pngMinPixels")?.value ?? "40", 10);

  const layerList = byId("layerList");
  if (!layerList) return;

  layerList.innerHTML = `<div class="small text-muted">Analysiere Farben…</div>`;
  setDisabled(byId("startLayerJob"), true);

  try {
    const layers = await analyzePngLayers(file, { tolerance, minPixels });

    if (!layers || layers.length === 0) {
      layerList.innerHTML = `<div class="small text-warning">Keine Farblayer gefunden. Toleranz erhöhen oder Min-Pixel senken.</div>`;
      return;
    }

    layerList.innerHTML = "";
    for (const layer of layers) {
      layerList.appendChild(createLayerRow(layer, true));
    }

    setDisabled(byId("startLayerJob"), false);
  } catch (e) {
    console.error(e);
    layerList.innerHTML = `<div class="small text-danger">Fehler bei Farberkennung: ${String(e?.message ?? e)}</div>`;
  }
}

async function previewPngAsGeneratedSvg(file) {
  const mode = byId("pngMode")?.value ?? "colors";
  const tolerance = parseInt(byId("pngTolerance")?.value ?? "90", 10);
  const hatchStep = parseInt(byId("rasterStep")?.value ?? "4", 10); // missbrauchen wir als Schraffur-Dichte

  let selected = [];
  if (mode === "bw") {
    selected = ["#000000"];
  } else {
    selected = getSelectedLayerHexes();
    if (selected.length === 0) selected = ["#000000"];
  }

  const svgText = await buildCrassSvgFromPng(file, selected, {
    tolerance,
    turdSize: 6,
    hatchStep: clampNum(hatchStep, 1, 12),
    hatchAngle: 35,
    outlineStroke: 1.2,
    hatchStroke: 0.8
  });

  const blob = new Blob([svgText], { type: "image/svg+xml" });
  const genFile = new File([blob], "generated_from_png.svg", { type: "image/svg+xml" });
  return { svgText, genFile };
}

function clampNum(v,a,b){ v=parseInt(v,10); if(isNaN(v)) v=a; return Math.max(a, Math.min(b, v)); }

function initPngUi() {
  const upload = byId("uploadSvg");
  const pngTools = byId("pngTools");
  const rasterOpts = byId("rasterOpts");
  const previewBtn = byId("preview");
  const sourceImg = byId("sourceSvg");
  const layerList = byId("layerList");
  const startLayerJobBtn = byId("startLayerJob");

  if (!upload) {
    console.error("uploadSvg not found");
    return;
  }

  const modeSel = byId("pngMode");
  const tolEl = byId("pngTolerance");
  const minPxEl = byId("pngMinPixels");
  const hatchEl = byId("rasterStep"); // wird als Schraffur-Dichte genutzt

  const clampNum = (v, a, b) => {
    let n = parseInt(v, 10);
    if (!Number.isFinite(n)) n = a;
    return Math.max(a, Math.min(b, n));
  };

  function setImagePreview(file) {
    if (!sourceImg) return;
    const url = URL.createObjectURL(file);
    sourceImg.onload = () => URL.revokeObjectURL(url);
    sourceImg.src = url;
    sourceImg.style.display = "";
  }

  function showPngTools(on) {
    showEl(pngTools, on);
    if (!on) return;
    showEl(rasterOpts, (modeSel?.value ?? "colors") !== "bw");
  }

  const debounced = (() => {
    let t = null;
    return (fn) => {
      clearTimeout(t);
      t = setTimeout(fn, 180);
    };
  })();

  let currentFile = null;
  let lastDetectedLayers = [];

  async function refreshLayers() {
    if (!currentFile || !isPngFile(currentFile)) return;

    const tolerance = clampNum(tolEl?.value ?? "90", 10, 160);
    const minPixels = clampNum(minPxEl?.value ?? "20", 1, 2000);

    if (layerList) layerList.innerHTML = `<div class="small text-muted">Analysiere Farben…</div>`;
    if (startLayerJobBtn) startLayerJobBtn.disabled = true;

    try {
      const layers = await analyzePngLayers(currentFile, { tolerance, minPixels });
      lastDetectedLayers = layers || [];

      if (!layers || layers.length === 0) {
        if (layerList) {
          layerList.innerHTML =
            `<div class="small text-warning">Keine Farblayer gefunden. Toleranz erhöhen oder Min-Pixel senken.</div>`;
        }
        return;
      }

      if (layerList) layerList.innerHTML = "";

      for (const layer of layers) {
        if (layerList) layerList.appendChild(createLayerRow(layer, true));
      }

      if (startLayerJobBtn) startLayerJobBtn.disabled = false;
    } catch (e) {
      console.error(e);
      if (layerList) {
        layerList.innerHTML =
          `<div class="small text-danger">Fehler bei Farberkennung: ${String(e?.message ?? e)}</div>`;
      }
    }
  }

  function getSelectedHexesForMode() {
    const mode = modeSel?.value ?? "colors";
    if (mode === "bw") return ["#000000"];

    const selected = getSelectedLayerHexes();
    if (selected.length > 0) return selected;

    const hasBlack = lastDetectedLayers.some(l => l.hex?.toLowerCase() === "#000000");
    if (hasBlack) return ["#000000"];
    if (lastDetectedLayers.length > 0) return [lastDetectedLayers[0].hex];
    return ["#000000"];
  }

  async function buildCrassSvgForCurrentPng() {
    const tolerance = clampNum(tolEl?.value ?? "90", 10, 180);
    const hatchStep = clampNum(hatchEl?.value ?? "4", 1, 12); // kleiner = dichter = “voller”
    const selected = getSelectedHexesForMode();

    const svgText = await buildCrassSvgFromPng(currentFile, selected, {
      tolerance,
      turdSize: 6,
      hatchStep,
      hatchAngle: 35,
      outlineStroke: 1.2,
      hatchStroke: 0.8
    });

    return svgText;
  }


  async function fetchStateForPng() {
    try {
      const res = await fetch("/getState", { cache: "no-store" });
      if (!res.ok) return null;
      return await res.json();
    } catch {
      return null;
    }
  }

  function loadSvgTextIntoUpload(svgText, filename) {
    cachedUploadedKind = "svg";
    cachedUploadedSvgString = svgText;

    try {
      svgControl.setSvgString(svgText, currentState);
      $(".svg-control").show();
      $("#preview").removeAttr("disabled");
    } catch (e) {
      console.warn("svgControl.setSvgString failed", e);
    }

    try {
      const blob = new Blob([svgText], { type: "image/svg+xml" });
      const genFile = new File([blob], filename || "generated.svg", { type: "image/svg+xml" });
      const dt = new DataTransfer();
      dt.items.add(genFile);
      const input = document.getElementById("uploadSvg");
      if (input) input.files = dt.files;

      // show preview image as svg file (like normal upload)
      setImagePreview(genFile);
    } catch (e) {
      console.warn("Failed to set uploadSvg input", e);
    }
  }

  function waitForPngLayerModal(message) {
    return new Promise((resolve) => {
      const el = document.getElementById("pngLayerModal");
      const txt = document.getElementById("pngLayerModalText");
      const btn = document.getElementById("pngLayerModalContinue");

      if (!el || !btn || !window.bootstrap) {
        // fallback
        try { alert(message); } catch {}
        resolve();
        return;
      }

      try { if (txt) txt.textContent = message || "Bitte Farbe wechseln…"; } catch {}

      const modal = bootstrap.Modal.getOrCreateInstance(el, { backdrop: "static", keyboard: false });
      const onClick = () => {
        try { btn.removeEventListener("click", onClick); } catch {}
        try { modal.hide(); } catch {}
        resolve();
      };
      btn.addEventListener("click", onClick);
      modal.show();
    });
  }

  async function startPngOutlineFillJob() {
    if (!currentFile || !isPngFile(currentFile)) return;

    // Reset batch for a fresh sequence
    try {
      window.__svgBatchActive = true;
      window.__svgBatchIndex = 0;
      batchSettings = null;
      batchAutoUploadPending = false;
    } catch {}

    const tolerance = clampNum(tolEl?.value ?? "90", 10, 180);
    const stepMm = clampNum(hatchEl?.value ?? "1", 1, 12); // now treated as mm
    const selected = getSelectedHexesForMode();

    const st = await fetchStateForPng();
    const safeWidthMm = Number(st?.safeWidth || 0);

    if (window.addMessage) window.addMessage(0, "PNG-Layer", "Erzeuge Outlines + Füllung…");
    $("#transformText").text("PNG → Outlines + Füllung läuft…");

    const job = await buildPngOutlineAndFillJob(currentFile, selected, {
      tolerance,
      turdSize: 12,
      penWidthMm: 1.0,
      fillStepMm: stepMm,
      hatchAngle: 35,
      safeWidthMm
    });

    if (!job?.queue || job.queue.length === 0) {
      alert("PNG-Layer: Keine Daten erzeugt (Farben prüfen)");
      return;
    }

    window.__pngLayerJob = { queue: job.queue, index: 0, active: true };

    // Hook: when drawing finished, advance automatically
    window.onBatchDrawingFinished = () => {
      advancePngLayerJob().catch(err => console.error(err));
    };

    // Load first (OUTLINES) like SVG upload
    const first = job.queue[0];
    loadSvgTextIntoUpload(first.svg, first.name);

    // go to choose-renderer slide (same as SVG)
    const choose = byId("chooseRendererSlide");
    const svgSlide = byId("svgUploadSlide");
    if (choose && svgSlide) {
      svgSlide.style.display = "none";
      choose.style.display = "";
    }

    $("#transformText").text("PNG-Layer bereit: Outlines");
  }

  async function advancePngLayerJob() {
    const j = window.__pngLayerJob;
    if (!j || !j.active) return;

    j.index = Number(j.index || 0) + 1;
    window.__svgBatchIndex = j.index;

    if (j.index >= j.queue.length) {
      j.active = false;
      window.__svgBatchActive = false;
      if (window.addMessage) window.addMessage(0, "PNG-Layer", "Fertig");
      return;
    }

    const item = j.queue[j.index];

    // prompt color change (fill layers)
    if (item.prompt) {
      await waitForPngLayerModal(item.prompt);
    }

    // Load next SVG into upload
    loadSvgTextIntoUpload(item.svg, item.name);

    // If we have batchSettings (from first SVG), auto-apply + auto-render + auto-upload
    try {
      if (batchSettings) {
        if (batchSettings.affine && typeof svgControl.setAffineTransform === "function") {
          svgControl.setAffineTransform(batchSettings.affine);
        }

        $("#infillDensity").val(batchSettings.infillDensity ?? 0);
        $("#turdSize").val(batchSettings.turdSize ?? 2);
        $("#flattenPathsCheckbox").prop("checked", !!batchSettings.flattenPaths);

        rendererKey = (batchSettings.rendererKey === "vrv") ? "vrv" : "path";
        rendererFn = (rendererKey === "vrv") ? render_VectorRasterVector : render_PathTracing;

        $("#svgUploadSlide").hide();
        $("#chooseRendererSlide").hide();
        $("#drawingPreviewSlide").show();

        uploadConvertedCommands = null;
        batchAutoUploadPending = true;
        activateProgressBar();
        $("#acceptSvg").attr("disabled", "disabled");

        await rendererFn();
      } else {
        // no settings yet -> user chooses renderer for first layer
        const choose = byId("chooseRendererSlide");
        const svgSlide = byId("svgUploadSlide");
        if (choose && svgSlide) {
          svgSlide.style.display = "none";
          choose.style.display = "";
        }
      }
    } catch (e) {
      console.error(e);
      if (window.addMessage) window.addMessage(2, "PNG-Layer", "Auto-Render fehlgeschlagen: " + (e?.message || e));
    }
  }

  upload.addEventListener("change", async () => {
    const file = upload.files?.[0];
    currentFile = file ?? null;

    if (!file) {
      showPngTools(false);
      if (previewBtn) previewBtn.disabled = true;
      return;
    }

    setImagePreview(file);

    if (isSvgFile(file)) {
      showPngTools(false);
      if (previewBtn) previewBtn.disabled = false;
      return;
    }

    if (isPngFile(file)) {
      showPngTools(true);
      if (previewBtn) previewBtn.disabled = false;
      await refreshLayers();
      return;
    }

    showPngTools(false);
    if (previewBtn) previewBtn.disabled = true;
  });

  modeSel?.addEventListener("change", async () => {
    if (!currentFile || !isPngFile(currentFile)) return;
    showPngTools(true);
    await refreshLayers();
  });

  // sliders
  tolEl?.addEventListener("input", () => {
    if (!currentFile || !isPngFile(currentFile)) return;
    debounced(() => refreshLayers());
  });

  minPxEl?.addEventListener("input", () => {
    if (!currentFile || !isPngFile(currentFile)) return;
    debounced(() => refreshLayers());
  });

  hatchEl?.addEventListener("input", () => {
  });

    startLayerJobBtn?.addEventListener("click", () => {
    startPngOutlineFillJob().catch(err => {
      console.error(err);
      alert("PNG-Layer fehlgeschlagen: " + String(err?.message ?? err));
    });
  });
previewBtn?.addEventListener("click", async () => {
    if (!currentFile) return;

    if (isSvgFile(currentFile)) {
      const choose = byId("chooseRendererSlide");
      const svgSlide = byId("svgUploadSlide");
      if (choose && svgSlide) {
        svgSlide.style.display = "none";
        choose.style.display = "";
      }
      return;
    }

    if (!isPngFile(currentFile)) return;

    try {
      if (window.addMessage) window.addMessage(0, "PNG → SVG", "Konvertiere…");
      $("#transformText").text("PNG → SVG (crass) läuft…");

      const svgText = await buildCrassSvgForCurrentPng();

      cachedUploadedKind = "svg";
      cachedUploadedSvgString = svgText;

      try {
        svgControl.setSvgString(svgText, currentState);
        $(".svg-control").show();
        $("#preview").removeAttr("disabled");
      } catch (e) {
        console.warn("svgControl.setSvgString failed (state not ready yet?)", e);
      }

      const blob = new Blob([svgText], { type: "image/svg+xml" });
      const genFile = new File([blob], "generated_from_png.svg", { type: "image/svg+xml" });
      const dt = new DataTransfer();
      dt.items.add(genFile);
      upload.files = dt.files;

      setImagePreview(genFile);
      showPngTools(false);
      $("#transformText").text("PNG → SVG (crass) fertig");

      const choose = byId("chooseRendererSlide");
      const svgSlide = byId("svgUploadSlide");
      if (choose && svgSlide) {
        svgSlide.style.display = "none";
        choose.style.display = "";
      }
    } catch (e) {
      console.error(e);
      $("#transformText").text("PNG → SVG fehlgeschlagen (Konsole)");
      alert("PNG → SVG fehlgeschlagen: " + String(e?.message ?? e));
    }
  });
}

const LIVEHUD_KEY = "mural_livehud_visible_v1";
let liveHudVisible = (localStorage.getItem(LIVEHUD_KEY) !== "0");

function setLiveHudVisible(v) {
  liveHudVisible = !!v;
  localStorage.setItem(LIVEHUD_KEY, liveHudVisible ? "1" : "0");
  const hud = document.getElementById("perfHud");
  if (hud) hud.classList.toggle("hidden", !liveHudVisible);

  const btn = document.getElementById("hudToggleSide");
  if (btn) btn.style.opacity = liveHudVisible ? "1" : "0.45";
}

function initLiveHudToggle() {
  const btn = document.getElementById("hudToggleSide");
  if (btn) {
    btn.addEventListener("click", () => setLiveHudVisible(!liveHudVisible));
  }
  setLiveHudVisible(liveHudVisible);
}

function updateLiveHudFromStatus(data) {
  if (!data || typeof data !== "object") return;

  liveHud.phaseName  = data.phaseName ?? liveHud.phaseName ?? "—";
  liveHud.printSteps = (data.printSteps !== undefined) ? data.printSteps : liveHud.printSteps;

  if (data.pen) {
    liveHud.penPos = data.pen.pos ?? data.pen.state ?? liveHud.penPos;
    liveHud.penAngle = (data.pen.angle !== undefined) ? data.pen.angle : liveHud.penAngle;
    liveHud.penDownAngle = (data.pen.downAngle !== undefined) ? data.pen.downAngle : liveHud.penDownAngle;
    liveHud.penUpAngle = (data.pen.upAngle !== undefined) ? data.pen.upAngle : liveHud.penUpAngle;
    liveHud.pendingDown = (data.pen.pendingDown !== undefined) ? data.pen.pendingDown : liveHud.pendingDown;
    liveHud.pendingUp = (data.pen.pendingUp !== undefined) ? data.pen.pendingUp : liveHud.pendingUp;
    liveHud.hasPendingDown = !!data.pen.hasPendingDown;
    liveHud.hasPendingUp = !!data.pen.hasPendingUp;
  }

  // bevorzugt top-level fwMaxLoopMs, sonst aus perf.max_loop_ms
  if (data.fwMaxLoopMs !== undefined) liveHud.fwMaxLoopMs = Number(data.fwMaxLoopMs) || 0;
  else if (data.perf && data.perf.max_loop_ms !== undefined) liveHud.fwMaxLoopMs = Number(data.perf.max_loop_ms) || 0;

  updatePerfHud();
}


function renderPenStageControls() {
  const downSlider = document.getElementById("drawPenDownSlider");
  const upSlider = document.getElementById("drawPenUpSlider");
  const downVal = document.getElementById("drawPenDownValue");
  const upVal = document.getElementById("drawPenUpValue");
  if (!downSlider || !upSlider || !downVal || !upVal) return;

  const down = Number(liveHud.hasPendingDown ? liveHud.pendingDown : liveHud.penDownAngle);
  const up = Number(liveHud.hasPendingUp ? liveHud.pendingUp : liveHud.penUpAngle);

  if (Number.isFinite(down)) {
    downSlider.value = String(Math.max(0, Math.min(80, Math.round(down))));
    downVal.textContent = downSlider.value;
  }
  if (Number.isFinite(up)) {
    upSlider.value = String(Math.max(0, Math.min(80, Math.round(up))));
    upVal.textContent = upSlider.value;
  }
}

let logVisible = false;
let enabledLevels = new Set([0, 1, 2]);

let unseenCount = 0;
let maxSeenTs = 0;
let localLogs = [];

let currentState = null;
let currentWorker = null;

let cachedUploadedSvgString = null;
let cachedUploadedKind = null; // "svg" | "png" | null

function resetUploadedFileState() {
  cachedUploadedSvgString = null;
  cachedUploadedKind = null;
}

function isSvgFile(file) {
  const n = (file?.name || "").toLowerCase();
  return n.endsWith(".svg") || file?.type === "image/svg+xml";
}

function isPngFile(file) {
  const n = (file?.name || "").toLowerCase();
  return n.endsWith(".png") || file?.type === "image/png";
}

function readFileAsDataUrl(file) {
  return new Promise((resolve, reject) => {
    const r = new FileReader();
    r.onload = () => resolve(r.result);
    r.onerror = reject;
    r.readAsDataURL(file);
  });
}

async function pngFileToImageData(file, maxWidthPx) {
  const bmp = await createImageBitmap(file);

  let w = bmp.width;
  let h = bmp.height;

  if (Number.isFinite(maxWidthPx) && maxWidthPx > 0 && w > maxWidthPx) {
    const scale = maxWidthPx / w;
    w = Math.max(1, Math.round(w * scale));
    h = Math.max(1, Math.round(h * scale));
  }

  const canvas = (typeof OffscreenCanvas !== "undefined")
    ? new OffscreenCanvas(w, h)
    : (() => {
      const c = document.createElement("canvas");
      c.width = w; c.height = h;
      return c;
    })();

  const ctx = canvas.getContext("2d", { willReadFrequently: true });
  ctx.drawImage(bmp, 0, 0, w, h);
  return ctx.getImageData(0, 0, w, h);
}

async function vectorizeImageDataToSvg(imageData, turdSize) {
  return new Promise((resolve, reject) => {
    // NICHT currentWorker verwenden, sonst killst du dir Preview-Worker gegenseitig.
    const w = new Worker(`./worker/worker.js?v=${Date.now()}`);

    w.onerror = (err) => {
      try { w.terminate(); } catch {}
      reject(err);
    };

    w.onmessage = (e) => {
      if (e?.data?.type === "status") {
        $("#transformText").text(`PNG → SVG: ${e.data.payload}`);
      } else if (e?.data?.type === "vectorizer") {
        const svg = e.data.payload.svg;
        try { w.terminate(); } catch {}
        resolve(svg);
      }
    };

    w.postMessage({
      type: "vectorize",
      raster: imageData,
      turdSize: Number.isFinite(turdSize) ? turdSize : 2,
    });
  });
}

window.onload = function () {
  init();
};

let uploadConvertedCommands = null;

let jobModel = null;
let selectedStartLine = 0;    // 0-based line index AFTER header lines (d/h)
let selectedStartDist = 0.0;  // distance at start (mm) within full command path
let simIsPlaying = false;
let simRaf = 0;
let simLastTs = 0;
let simSegIx = 0;
let simSegProg = 0.0;
let simDist = 0.0;
let simSpeedMult = 15;

let liveSegIx = 0;
let liveSegProg = 0.0;
let liveDist = 0.0;
let liveStartDist = 0.0;

let diagCache = null;
let plannedPreviewCanvas = null;
let plannedLiveCanvas = null;

const NEON_YELLOW = "rgba(255, 241, 0, 0.95)";
const FAINT_PLAN = "rgba(255,255,255,0.14)";
const FAINT_TRAVEL = "rgba(120,170,255,0.08)";

let fmOpenFile = null; // { vol, path, name, type }


window.addEventListener("resize", () => {
  try {
    if (jobModel) {
      clearTimeout(window.__resizeT);
      window.__resizeT = setTimeout(async () => {
        try { await renderPlannedPreview(); } catch {}
        try { updatePreviewOverlay(); } catch {}
        try { await setupLiveCanvasIfPossible(); } catch {}
      }, 180);
    }
  } catch {}
});

function downloadTextFile(filename, text) {
  const content = (text ?? "").toString();
  const blob = new Blob([content], { type: "text/plain;charset=utf-8" });

  const nav = window.navigator;
  if (nav && typeof nav.msSaveOrOpenBlob === "function") {
    nav.msSaveOrOpenBlob(blob, filename);
    return;
  }

  const url = URL.createObjectURL(blob);
  const a = document.createElement("a");
  a.href = url;
  a.download = filename;
  a.rel = "noopener";
  document.body.appendChild(a);
  a.click();
  a.remove();
  setTimeout(() => URL.revokeObjectURL(url), 1500);
}

async function loadDiagOnce() {
  if (diagCache) return diagCache;
  try {
    const res = await fetch("/diag", { cache: "no-store" });
    if (!res.ok) throw new Error("HTTP " + res.status);
    diagCache = await res.json();
  } catch (e) {
    console.warn("/diag failed", e);
    diagCache = null;
  }
  return diagCache;
}

function estimateMmPerSec(diag) {
  // mm/s = steps/s * (mm/step)
  try {
    const stepsPerRotation = Number(diag?.stepsPerRotation);
    const travelPerRotationMM = Number(diag?.travelPerRotationMM ?? diag?.circumference);
    const printSpeedSteps = Number(diag?.printSpeedSteps);

    if (!Number.isFinite(stepsPerRotation) || stepsPerRotation <= 0) return 60;
    if (!Number.isFinite(travelPerRotationMM) || travelPerRotationMM <= 0) return 60;
    if (!Number.isFinite(printSpeedSteps) || printSpeedSteps <= 0) return 60;

    const mmPerStep = travelPerRotationMM / stepsPerRotation;
    const mmPerSec = printSpeedSteps * mmPerStep;
    // realistisch clampen, sonst killt man Browser-Animationen
    return Math.max(10, Math.min(400, mmPerSec));
  } catch {
    return 60;
  }
}

async function parseCommandsToModel(commandsText, homeX = 0, homeY = 0) {
  const raw = String(commandsText || "");
  const lines = raw.split(/\r?\n/).map(l => l.trim()).filter(l => l.length > 0);
  if (lines.length < 2) throw new Error("Commands zu kurz");
  if (lines[0][0] !== "d") throw new Error("Commands Header fehlt: d...");
  if (lines[1][0] !== "h") throw new Error("Commands Header fehlt: h...");

  const headerTotal = Number.parseFloat(lines[0].slice(1)) || 0;
  const height = Number.parseFloat(lines[1].slice(1)) || 0;

  let penDown = false;
  let cur = { x: Number(homeX) || 0, y: Number(homeY) || 0 };

  let minX = cur.x, maxX = cur.x, minY = cur.y, maxY = cur.y;

  const segments = [];
  let cum = 0;
  let lineIndex = 0; // AFTER header (d/h)

  // Chunked loop: UI bleibt responsiv
  for (let i = 2; i < lines.length; i++) {
    const l = lines[i];
    const c0 = l[0];

    if (c0 === "p") {
      const c1 = l.length > 1 ? l[1] : "0";
      penDown = (c1 === "1");
      lineIndex++;
    } else {
      const sep = l.indexOf(" ");
      if (sep >= 0) {
        const x = Number.parseFloat(l.slice(0, sep)) || 0;
        const y = Number.parseFloat(l.slice(sep + 1)) || 0;

        const dx = x - cur.x;
        const dy = y - cur.y;
        const len = Math.sqrt(dx * dx + dy * dy);

        const cumStart = cum;
        cum += len;

        segments.push({
          x1: cur.x, y1: cur.y, x2: x, y2: y,
          len,
          cumStart,
          cumEnd: cum,
          penDown,
          lineIndex,
        });

        cur = { x, y };
        minX = Math.min(minX, x);
        maxX = Math.max(maxX, x);
        minY = Math.min(minY, y);
        maxY = Math.max(maxY, y);

        lineIndex++;
      } else {
        lineIndex++;
      }
    }

    // Alle N Zeilen kurz Luft geben
    if ((i % UI_TUNING.PARSE_LINES_PER_TICK) === 0) {
      await nextTick();
    }
  }

  const totalDistance = (headerTotal > 0) ? headerTotal : cum;

  return {
    headerTotal,
    totalDistance,
    height,
    segments,
    lineCount: lineIndex,
    bbox: { minX, minY, maxX, maxY },
  };
}


function findStartByPercent(model, percent) {
  const p = Math.max(0, Math.min(100, Number(percent) || 0));
  const targetDist = (p / 100) * model.totalDistance;

  if (!model.segments.length) {
    return { startLine: 0, startDist: 0, percent: 0 };
  }

  for (let i = 0; i < model.segments.length; i++) {
    const seg = model.segments[i];
    if (seg.cumEnd >= targetDist) {
      return { startLine: seg.lineIndex, startDist: seg.cumStart, percent: p };
    }
  }

  const last = model.segments[model.segments.length - 1];
  return { startLine: last.lineIndex, startDist: last.cumStart, percent: 100 };
}

function findSegmentIndexByDistance(model, dist) {
  const d = Math.max(0, Math.min(model.totalDistance, Number(dist) || 0));
  for (let i = 0; i < model.segments.length; i++) {
    if (model.segments[i].cumEnd >= d) return i;
  }
  return Math.max(0, model.segments.length - 1);
}

function computeTransformForCanvas(model, canvasW, canvasH, pad = 16) {
  const b = model.bbox;
  const w = Math.max(1, b.maxX - b.minX);
  const h = Math.max(1, b.maxY - b.minY);

  const scale = Math.min((canvasW - 2*pad) / w, (canvasH - 2*pad) / h);
  const ox = pad - b.minX * scale;
  const oy = pad - b.minY * scale;

  return {
    scale,
    ox,
    oy,
    mmToPx(x, y) {
      return { x: x * scale + ox, y: y * scale + oy };
    }
  };
}

function ensureCanvasSize(canvas) {
  if (!canvas) return { w: 0, h: 0, dpr: 1 };
  const dpr = window.devicePixelRatio || 1;
  const cssW = Math.max(1, Math.floor(canvas.clientWidth));
  const cssH = Math.max(1, Math.floor(canvas.clientHeight));
  const w = Math.floor(cssW * dpr);
  const h = Math.floor(cssH * dpr);
  if (canvas.width !== w || canvas.height !== h) {
    canvas.width = w;
    canvas.height = h;
  }
  return { w, h, dpr };
}

function drawBackground(ctx, w, h) {
  ctx.save();
  ctx.clearRect(0, 0, w, h);
  ctx.fillStyle = "rgba(8, 10, 20, 0.70)";
  ctx.fillRect(0, 0, w, h);

  // dezentes Grid
  ctx.globalAlpha = 0.18;
  ctx.strokeStyle = "rgba(255,255,255,0.22)";
  ctx.lineWidth = 1;
  const step = Math.max(40, Math.floor(Math.min(w, h) / 12));
  for (let x = 0; x < w; x += step) {
    ctx.beginPath();
    ctx.moveTo(x, 0);
    ctx.lineTo(x, h);
    ctx.stroke();
  }
  for (let y = 0; y < h; y += step) {
    ctx.beginPath();
    ctx.moveTo(0, y);
    ctx.lineTo(w, y);
    ctx.stroke();
  }

  ctx.restore();
}

async function drawPlannedPath(ctx, model, tr) {
  ctx.save();
  ctx.lineCap = "round";
  ctx.lineJoin = "round";

  const t0 = performance.now();

  for (let i = 0; i < model.segments.length; i++) {
    const seg = model.segments[i];

    const a = tr.mmToPx(seg.x1, seg.y1);
    const b = tr.mmToPx(seg.x2, seg.y2);

    ctx.strokeStyle = seg.penDown ? FAINT_PLAN : FAINT_TRAVEL;
    ctx.lineWidth = seg.penDown ? 2 : 1;

    ctx.beginPath();
    ctx.moveTo(a.x, a.y);
    ctx.lineTo(b.x, b.y);
    ctx.stroke();

    if ((i % UI_TUNING.DRAW_SEGMENTS_PER_TICK) === 0) {
      await nextTick();
    }
  }

  ctx.restore();

  const t1 = performance.now();
  setPerfStat("planMs", t1 - t0);
}


function drawCrosshair(overlayCtx, x, y, label = "") {
  const w = overlayCtx.canvas.width;
  const h = overlayCtx.canvas.height;

  overlayCtx.save();
  overlayCtx.clearRect(0, 0, w, h);

  // Fadenkreuz
  overlayCtx.strokeStyle = "rgba(255,255,255,0.55)";
  overlayCtx.lineWidth = 1;
  overlayCtx.beginPath();
  overlayCtx.moveTo(x - 18, y);
  overlayCtx.lineTo(x + 18, y);
  overlayCtx.moveTo(x, y - 18);
  overlayCtx.lineTo(x, y + 18);
  overlayCtx.stroke();

  overlayCtx.fillStyle = "rgba(255,255,255,0.85)";
  overlayCtx.beginPath();
  overlayCtx.arc(x, y, 3, 0, Math.PI * 2);
  overlayCtx.fill();

  if (label) {
    overlayCtx.font = "12px system-ui, -apple-system, Segoe UI, Roboto";
    overlayCtx.fillStyle = "rgba(255,255,255,0.85)";
    overlayCtx.fillText(label, Math.min(w - 8, x + 10), Math.max(14, y - 10));
  }

  overlayCtx.restore();
}

function drawStartMarker(overlayCtx, x, y) {
  overlayCtx.save();
  overlayCtx.strokeStyle = "rgba(0, 255, 255, 0.85)";
  overlayCtx.lineWidth = 2;
  overlayCtx.beginPath();
  overlayCtx.arc(x, y, 7, 0, Math.PI * 2);
  overlayCtx.stroke();
  overlayCtx.restore();
}

function drawPausedBanner(overlayCtx) {
  const w = overlayCtx.canvas.width;
  const h = overlayCtx.canvas.height;
  overlayCtx.save();
  overlayCtx.fillStyle = "rgba(0,0,0,0.35)";
  overlayCtx.fillRect(0, 0, w, h);
  overlayCtx.fillStyle = "rgba(255,255,255,0.90)";
  overlayCtx.font = "20px system-ui, -apple-system, Segoe UI, Roboto";
  overlayCtx.textAlign = "center";
  overlayCtx.fillText("PAUSE", w/2, h/2);
  overlayCtx.restore();
}

function advanceDraw(ctx, model, tr, deltaDist, state) {
  let remaining = deltaDist;

  while (remaining > 0 && state.segIx < model.segments.length) {
    const seg = model.segments[state.segIx];
    if (seg.len <= 0.000001) {
      state.segIx++;
      state.segProg = 0;
      continue;
    }

    const segRemainingLen = seg.len * (1 - state.segProg);
    const stepLen = Math.min(remaining, segRemainingLen);
    const stepFrac = stepLen / seg.len;

    const t0 = state.segProg;
    const t1 = state.segProg + stepFrac;

    const sx = seg.x1 + (seg.x2 - seg.x1) * t0;
    const sy = seg.y1 + (seg.y2 - seg.y1) * t0;
    const ex = seg.x1 + (seg.x2 - seg.x1) * t1;
    const ey = seg.y1 + (seg.y2 - seg.y1) * t1;

    const a = tr.mmToPx(sx, sy);
    const b = tr.mmToPx(ex, ey);

    ctx.save();
    ctx.lineCap = "round";
    ctx.lineJoin = "round";
    ctx.strokeStyle = seg.penDown ? NEON_YELLOW : "rgba(255,255,255,0.10)";
    ctx.lineWidth = seg.penDown ? 2.6 : 1.2;
    ctx.beginPath();
    ctx.moveTo(a.x, a.y);
    ctx.lineTo(b.x, b.y);
    ctx.stroke();
    ctx.restore();

    state.segProg = t1;
    remaining -= stepLen;
    state.dist += stepLen;

    if (state.segProg >= 0.999999) {
      state.segIx++;
      state.segProg = 0;
    }
  }
}

function currentSimMmPosition(model, state) {
  if (!model.segments.length) return { x: 0, y: 0 };
  const ix = Math.min(Math.max(0, state.segIx), model.segments.length - 1);
  const seg = model.segments[ix];
  const t = Math.max(0, Math.min(1, state.segProg));
  return {
    x: seg.x1 + (seg.x2 - seg.x1) * t,
    y: seg.y1 + (seg.y2 - seg.y1) * t,
  };
}

async function initJobPreviewFromCommands(commandsText) {
  const t0 = performance.now();

  try {
    const wrap = document.getElementById("jobPreviewWrap");
    if (!wrap) return;

    const homeX = Number(currentState?.homeX ?? 0);
    const homeY = Number(currentState?.homeY ?? 0);

    const tParse0 = performance.now();
    jobModel = await parseCommandsToModel(commandsText, homeX, homeY);

    // Event: JobModel verfügbar (für Live-HUD/Stats)
    try {
      window.dispatchEvent(new CustomEvent("mural:jobModel", {
        detail: { totalDistanceMm: jobModel.totalDistance, headerTotal: jobModel.headerTotal, lineCount: jobModel.lineCount }
      }));
    } catch {}
    const tParse1 = performance.now();
    setPerfStat("parseMs", tParse1 - tParse0);

    // UI aktivieren
    wrap.style.display = "";
    document.getElementById("simPlayPause").disabled = false;
    document.getElementById("startScrubber").disabled = false;
    document.getElementById("simSpeed").disabled = false;
    document.getElementById("downloadCommandsLocal").disabled = false;

    // Default selection
    const sel = findStartByPercent(jobModel, 0);
    selectedStartLine = sel.startLine;
    selectedStartDist = sel.startDist;
    const scrub = document.getElementById("startScrubber");
    if (scrub) scrub.value = "0";
    const scrubTxt = document.getElementById("startScrubberText");
    if (scrubTxt) scrubTxt.textContent = `0%  (startLine ${selectedStartLine})`;

    const sp = document.getElementById("simSpeed");
    if (sp) sp.value = String(simSpeedMult);
    const spTxt = document.getElementById("simSpeedText");
    if (spTxt) spTxt.textContent = `${simSpeedMult}×`;
    
    const moves = jobModel.segments.length;
    const lines = jobModel.lineCount;
    const totalM = (jobModel.totalDistance / 1000).toFixed(2);

    const infoEl = document.getElementById("commandsInfo");
    if (infoEl) {
      infoEl.textContent =
        `Lines: ${lines}, Moves: ${moves}, Total: ${totalM}m`;
    }

    await renderPlannedPreview();
    updatePreviewOverlay();

    const t1 = performance.now();
    setPerfStat("initMs", t1 - t0);

  } catch (e) {
    console.error(e);
    const info = document.getElementById("commandsInfo");
    if (info) info.textContent = "Commands parse error: " + (e?.message || e);
  }
}


async function renderPlannedPreview() {
  if (!jobModel) return;

  const canvas = document.getElementById("jobPreviewCanvas");
  const overlay = document.getElementById("jobPreviewOverlay");
  if (!canvas || !overlay) return;

  const { w, h } = ensureCanvasSize(canvas);
  ensureCanvasSize(overlay);

  const tr = computeTransformForCanvas(jobModel, w, h);

  plannedPreviewCanvas = document.createElement("canvas");
  plannedPreviewCanvas.width = w;
  plannedPreviewCanvas.height = h;

  const pctx = plannedPreviewCanvas.getContext("2d");
  drawBackground(pctx, w, h);

  await drawPlannedPath(pctx, jobModel, tr);

  const ctx = canvas.getContext("2d");
  ctx.drawImage(plannedPreviewCanvas, 0, 0);

  // reset sim state
  simSegIx = findSegmentIndexByDistance(jobModel, selectedStartDist);
  simSegProg = 0;
  simDist = selectedStartDist;
}


function updatePreviewOverlay() {
  if (!jobModel) return;

  const overlay = document.getElementById("jobPreviewOverlay");
  const canvas = document.getElementById("jobPreviewCanvas");
  if (!overlay || !canvas) return;

  const { w, h } = ensureCanvasSize(canvas);
  ensureCanvasSize(overlay);

  const tr = computeTransformForCanvas(jobModel, w, h);
  const octx = overlay.getContext("2d");

  const posMm = currentSimMmPosition(jobModel, { segIx: simSegIx, segProg: simSegProg });
  const posPx = tr.mmToPx(posMm.x, posMm.y);

  drawCrosshair(octx, posPx.x, posPx.y, "Preview");

  // Start marker
  const startSegIx = findSegmentIndexByDistance(jobModel, selectedStartDist);
  const startSeg = jobModel.segments[startSegIx];
  const startPx = tr.mmToPx(startSeg.x1, startSeg.y1);
  drawStartMarker(octx, startPx.x, startPx.y);
}

function stopSim() {
  simIsPlaying = false;
  if (simRaf) cancelAnimationFrame(simRaf);
  simRaf = 0;
}

async function startSim() {
  if (!jobModel) return;

  stopSim();

  const btn = document.getElementById("simPlayPause");
  if (btn) btn.textContent = "Pause Vorschau";

  // reset base
  await renderPlannedPreview();

  // reset sim from selected start
  simSegIx = findSegmentIndexByDistance(jobModel, selectedStartDist);
  simSegProg = 0;
  simDist = selectedStartDist;
  simLastTs = 0;
  simIsPlaying = true;

  const diag = await loadDiagOnce();
  const baseMmPerSec = estimateMmPerSec(diag);

  const canvas = document.getElementById("jobPreviewCanvas");
  const { w, h } = ensureCanvasSize(canvas);
  const tr = computeTransformForCanvas(jobModel, w, h);

  const ctx = canvas.getContext("2d");

  const minFrameMs = 1000 / Math.max(5, UI_TUNING.PREVIEW_FPS);
  let lastFramePaint = 0;

  const tick = (ts) => {
    if (!simIsPlaying) return;
    if (!simLastTs) simLastTs = ts;
    const dt = Math.max(0, (ts - simLastTs) / 1000);
    simLastTs = ts;

    const mmPerSec = baseMmPerSec * Math.max(1, simSpeedMult);
    const deltaDist = dt * mmPerSec;

    advanceDraw(ctx, jobModel, tr, deltaDist, {
      segIx: simSegIx,
      segProg: simSegProg,
      dist: simDist,
    });

    // advanceDraw bekommt state by value, also: wir müssen selbst updaten
    // Lösung: berechnen über globalen state, indem wir einen temporären mutable state verwenden.
  };

  // Fix: mutable state verwenden
  const mutable = { segIx: simSegIx, segProg: simSegProg, dist: simDist };

  const loop = (ts) => {
    if (!simIsPlaying) return;
    if (lastFramePaint && (ts - lastFramePaint) < minFrameMs) {
      simRaf = requestAnimationFrame(loop);
      return;
    }
    lastFramePaint = ts;
    if (!simLastTs) simLastTs = ts;
    const dt = Math.max(0, (ts - simLastTs) / 1000);
    simLastTs = ts;

    const mmPerSec = baseMmPerSec * Math.max(1, simSpeedMult);
    const deltaDist = dt * mmPerSec;

    advanceDraw(ctx, jobModel, tr, deltaDist, mutable);

    // global state synchronisieren
    simSegIx = mutable.segIx;
    simSegProg = mutable.segProg;
    simDist = mutable.dist;

    updatePreviewOverlay();

    // Ende?
    if (simSegIx >= jobModel.segments.length) {
      stopSim();
      const b = document.getElementById("simPlayPause");
      if (b) b.textContent = "Vorschau abspielen";
      return;
    }

    simRaf = requestAnimationFrame(loop);
  };

  simRaf = requestAnimationFrame(loop);
}

async function ensureJobModelLoadedFromDevice(timeoutMs = 6000) {
  if (jobModel) return jobModel;

  const ctrl = new AbortController();
  const t = setTimeout(() => ctrl.abort(), timeoutMs);

  try {
    const res = await fetch("/downloadCommands", { cache: "no-store", signal: ctrl.signal });
    if (!res.ok) throw new Error("HTTP " + res.status);

    const txt = await res.text();
    const homeX = Number(currentState?.homeX ?? 0);
    const homeY = Number(currentState?.homeY ?? 0);

    // Parse kann groß sein: einmal den Event-Loop freigeben, damit UI nicht "hängt"
    await new Promise(r => setTimeout(r, 0));

    jobModel = await parseCommandsToModel(txt, homeX, homeY);

    // Event: JobModel verfügbar (für Live-HUD/Stats)
    try {
      window.dispatchEvent(new CustomEvent("mural:jobModel", {
        detail: { totalDistanceMm: jobModel.totalDistance, headerTotal: jobModel.headerTotal, lineCount: jobModel.lineCount }
      }));
    } catch {}
    return jobModel;
  } catch (e) {
    console.error(e);
    return null;
  } finally {
    clearTimeout(t);
  }
}

async function setupLiveCanvasIfPossible() {
  if (!jobModel) return;

  const canvas = document.getElementById("jobLiveCanvas");
  const overlay = document.getElementById("jobLiveOverlay");
  if (!canvas || !overlay) return;

  const { w, h } = ensureCanvasSize(canvas);
  ensureCanvasSize(overlay);

  const tr = computeTransformForCanvas(jobModel, w, h);

  plannedLiveCanvas = document.createElement("canvas");
  plannedLiveCanvas.width = w;
  plannedLiveCanvas.height = h;

  const pctx = plannedLiveCanvas.getContext("2d");
  drawBackground(pctx, w, h);
  await drawPlannedPath(pctx, jobModel, tr);

  const ctx = canvas.getContext("2d");
  ctx.drawImage(plannedLiveCanvas, 0, 0);

  // StartDist ist immer Segment-Start, nicht "cumEnd"
  liveStartDist = selectedStartDist;

  liveSegIx = findSegmentIndexByDistance(jobModel, liveStartDist);
  liveSegProg = 0;
  liveDist = liveStartDist;

  // overlay initial
  const octx = overlay.getContext("2d");
  octx.clearRect(0, 0, w, h);
}

function updateLiveOverlay(xMm, yMm, progress, paused) {
  if (!jobModel) return;

  const canvas = document.getElementById("jobLiveCanvas");
  const overlay = document.getElementById("jobLiveOverlay");
  if (!canvas || !overlay) return;

  const { w, h } = ensureCanvasSize(canvas);
  ensureCanvasSize(overlay);

  const tr = computeTransformForCanvas(jobModel, w, h);
  const octx = overlay.getContext("2d");

  const p = tr.mmToPx(xMm, yMm);
  drawCrosshair(octx, p.x, p.y, `${Math.round(progress)}%`);

  // Start marker
  const startSegIx = findSegmentIndexByDistance(jobModel, liveStartDist);
  const startSeg = jobModel.segments[startSegIx];
  const startPx = tr.mmToPx(startSeg.x1, startSeg.y1);
  drawStartMarker(octx, startPx.x, startPx.y);

  if (paused) {
    drawPausedBanner(octx);
  }
}

function advanceLiveToProgress(progress) {
  if (!jobModel) return;

  const canvas = document.getElementById("jobLiveCanvas");
  if (!canvas) return;

  const { w, h } = ensureCanvasSize(canvas);
  const tr = computeTransformForCanvas(jobModel, w, h);
  const ctx = canvas.getContext("2d");

  const range = Math.max(0.0, jobModel.totalDistance - liveStartDist);
  const target = liveStartDist + (Math.max(0, Math.min(100, progress)) / 100) * range;

  if (target <= liveDist) return;

  const delta = target - liveDist;
  const mutable = { segIx: liveSegIx, segProg: liveSegProg, dist: liveDist };
  advanceDraw(ctx, jobModel, tr, delta, mutable);
  liveSegIx = mutable.segIx;
  liveSegProg = mutable.segProg;
  liveDist = mutable.dist;
}

async function checkIfExtendedToHome(extendToHomeTime) {
  await new Promise(r => setTimeout(r, extendToHomeTime * 1000));

  const waitPeriod = 2000;
  let done = false;
  while (!done) {
    try {
      const state = await $.get("/getState");
      if (state.phase !== 'ExtendToHome') {
        adaptToState(state);
        done = true;
      } else {
        await new Promise(r => setTimeout(r, waitPeriod));
      }
    } catch (err) {
      alert("Failed to get current phase: " + err);
      location.reload();
    }
  }
}

function init() {
  initPngUi();
  initPerfSettingsUi();
  ui_initEnablePinToggles().catch(() => {});
  startPerfPoll();
  bindServoToolButtons();
  document.addEventListener("DOMContentLoaded", () => {
    // ... deine init Sachen ...
    initPulseWidthSettingsUI();
  });


    document.getElementById('btnGrafOpen')?.addEventListener('click', () => {
      // Variante A: direkt zu /graf/index.html
      window.location.href = '/graf/index.html';

      // Variante B (ohne sichtbaren Seitenwechsel): iframe-/View-Toggle Logik nutzen
    });


  function doneWithPhase(custom) {
    $(".muralSlide").hide();
    $("#loadingSlide").show();
    if (!custom) {
      custom = {
        url: "/doneWithPhase",
        data: {},
        commandName: "Done With Phase",
      };
    }

    $.post(custom.url, custom.data || {}, function(state) {
      adaptToState(state);
    }).fail(function() {
      alert(`${custom.commandName} command failed`);
      location.reload();
    });
  }

  $("#beltsRetracted").click(async function() {
    await client.leftRetractUp();
    await client.rightRetractUp();
    doneWithPhase();
  });

  $("#setDistance").click(function() {
    const inputValue = parseInt($("#distanceInput").val());
    if (isNaN(inputValue)) {
      throw new Error("input value is not a number");
    }

    doneWithPhase({
      url: "/setTopDistance",
      data: {distance: inputValue},
      commandName: "Set Top Distance",
    });
  });

  /*
  Version: 1.0
  Prompt: Button #setSpeeds sendet printSpeed/moveSpeed an /setSpeeds,
  damit die Motor-Geschwindigkeiten zur Laufzeit gesetzt werden können.
  */
  $("#setSpeeds").click(function () {
    const printSpeed = parseInt($("#printSpeedInput").val());
    const moveSpeed  = parseInt($("#moveSpeedInput").val());

    if (isNaN(printSpeed) || isNaN(moveSpeed)) {
      alert("Bitte Zahlen für beide Geschwindigkeiten eingeben.");
      return;
    }

    $.post("/setSpeeds", { printSpeed: printSpeed, moveSpeed: moveSpeed })
      .done(function () {
        alert("Speeds updated");
      })
      .fail(function () {
        alert("Setting speeds failed");
      });
  });

  // Motion tuning: acceleration + jog steps (persisted)
  async function loadMotionTuning() {
    try {
      const t = await $.get("/motionTuning");
      if (t && typeof t === "object") {
        $("#infiniteStepsInput").val(t.infiniteSteps);
        $("#accelerationInput").val(t.acceleration);
      }
    } catch (e) {
      console.warn("motionTuning load failed", e);
    }
  }

  if ($("#setMotionTuning").length) {
    $("#setMotionTuning").click(function () {
      const infiniteSteps = parseInt($("#infiniteStepsInput").val());
      const acceleration  = parseInt($("#accelerationInput").val());

      if (isNaN(infiniteSteps) || isNaN(acceleration)) {
        alert("Bitte Zahlen für beide Felder eingeben.");
        return;
      }

      $.post("/setMotionTuning", { infiniteSteps, acceleration })
        .done(function () {
          alert("Tuning updated");
        })
        .fail(function () {
          alert("Setting tuning failed");
        });
    });

    // initial fill
    loadMotionTuning();
  }

  $("#leftMotorToggle").change(function() {
    if (this.checked) {
      client.leftRetractDown();
    } else {
      client.leftRetractUp();
    }
  });

  $("#rightMotorToggle").change(function() {
    if (this.checked) {
      client.rightRetractDown();
    } else {
      client.rightRetractUp();
    }
  });

  $("#extendToHome").click(function() {
    $(this).prop("disabled", true);
    $("#extendingSpinner").css('visibility', 'visible');
    $.post("/extendToHome", {})
      .always(async function(res) {
        const extendToHomeTime = parseInt(res);
        await checkIfExtendedToHome(extendToHomeTime);
      });
  });

  function getServoValueFromInputValue() {
    const inputValue = parseInt($("#servoRange").val());
    const value = 95 - inputValue;
    let normalizedValue;
    if (value < 0) {
      normalizedValue = 0;
    } else if (value > 95) {
      normalizedValue = 95;
    } else {
      normalizedValue = value;
    }
    return normalizedValue;
  }

  $("#servoRange").on('input', $.throttle(250, function () {
    const servoValue = getServoValueFromInputValue();
    $.post("/setServo", {angle: servoValue});
  }));

  const stepVaule = 5;
  $("#penMinus").click(function() {
    $("#servoRange")[0].stepDown(stepVaule);
    $("#servoRange").trigger('input');
  });

  $("#penPlus").click(function() {
    $("#servoRange")[0].stepUp(stepVaule);
    $("#servoRange").trigger('input');
  });

  $("#setPenDistance").click(function () {
    const inputValue = getServoValueFromInputValue();
    doneWithPhase({
      url: "/setPenDistance",
      data: {angle: inputValue},
      commandName: "Set Pen Distance",
    });
  });

  async function getUploadedSvgString() {
    // Wenn PNG schon vektorisiert wurde, nehmen wir den Cache
    if (cachedUploadedSvgString) {
      return cachedUploadedSvgString;
    }

    const [file] = $("#uploadSvg")[0].files;
    if (!file) return null;

    // SVG direkt lesen (alter Ablauf)
    if (isSvgFile(file)) {
      cachedUploadedKind = "svg";
      cachedUploadedSvgString = await file.text();
      return cachedUploadedSvgString;
    }

    // PNG wird NICHT mehr hier verarbeitet
    if (isPngFile(file)) {
      throw new Error("PNG muss zuerst über Farblayer-UI zu SVG konvertiert werden.");
    }

    throw new Error("Unbekannter Dateityp. Bitte SVG oder PNG auswählen.");
  }

$("#uploadSvg").off("change").on("change", async function () {
  const [file] = $("#uploadSvg")[0].files;
  if (!file) return;

  // ✅ PNG wird komplett vom initPngUi() behandelt.
  // Wichtig: NICHT vorher UI resetten, sonst zerlegst du dir die Farblayer-UI.
  if (isPngFile(file)) {
    return;
  }

  // Ab hier nur SVG-Flow
  resetUploadedFileState();

  // UI zurücksetzen (nur für SVG!)
  $("#preview").attr("disabled", "disabled");
  $(".svg-control").hide();
  $("#transformText").text("");

  // Defaults
  $("#infillDensity").val(0);
  $("#turdSize").val(2);

  try {
    if (isSvgFile(file)) {
      const svgString = await file.text();
      cachedUploadedKind = "svg";
      cachedUploadedSvgString = svgString;

      svgControl.setSvgString(svgString, currentState);
      $(".svg-control").show();
      $("#preview").removeAttr("disabled");
      return;
    }

    alert("Bitte nur SVG oder PNG auswählen.");
  } catch (err) {
    console.error(err);
    alert("Datei konnte nicht verarbeitet werden: " + (err?.message || err));
    resetUploadedFileState();
    $("#preview").attr("disabled", "disabled");
    $(".svg-control").hide();
  }
});


  let currentPreviewId = 0;
  let rendererFn = null;
  let rendererKey = null; // "path" | "vrv" | null

  // SVG-Serie: Einstellungen vom ersten SVG (Transform + Renderer + Regler) übernehmen
  let batchSettings = null; // wird beim ersten Upload (acceptSvg) gefüllt
  let batchAutoUploadPending = false; // wenn true: nach Render automatisch hochladen

  async function render_VectorRasterVector() {
    if (currentWorker) {
      console.log("Terminating previous worker");
      currentWorker.terminate();
    }
    currentPreviewId++;
    const thisPreviewId = currentPreviewId;

    const svgString = await getUploadedSvgString();
    if (!svgString) throw new Error('No SVG string');

    $("#progressBar").text("Rasterizing");
    const raster = await svgControl.getCurrentSvgImageData();

    const vectorizeRequest = {
      type: 'vectorize',
      raster,
      turdSize: getTurdSize(),
    };

    if (currentPreviewId == thisPreviewId) {
      currentWorker = new Worker(`./worker/worker.js?v=${Date.now()}`);

      currentWorker.onmessage = (e) => {
        if (e.data.type === 'status') {
          $("#progressBar").text(e.data.payload);
        } else if (e.data.type === 'vectorizer') {
          const vectorizedSvg = e.data.payload.svg;
          const scale = svgControl.getRenderScale();
          renderSvgInWorker(
            currentWorker,
            vectorizedSvg,
            svgControl.getTargetWidth() * scale,
            svgControl.getTargetHeight() * scale,
          );
        } else if (e.data.type === 'log') {
          console.log(`Worker: ${e.data.payload}`);
        }
      };

      currentWorker.postMessage(vectorizeRequest);
    }
  }

  async function render_PathTracing() {
    if (currentWorker) {
      console.log("Terminating previous worker");
      currentWorker.terminate();
    }
    currentPreviewId++;
    const thisPreviewId = currentPreviewId;

    const svgString = await getUploadedSvgString();
    if (!svgString) throw new Error('No SVG string');

    if (currentPreviewId == thisPreviewId) {
      currentWorker = new Worker(`./worker/worker.js?v=${Date.now()}`);
      currentWorker.onmessage = (e) => {
        if (e.data.type === 'status') {
          $("#progressBar").text(e.data.payload);
        } else if (e.data.type === 'log') {
          console.log(`Worker: ${e.data.payload}`);
        }
      };

      const renderSvg = svgControl.getRenderSvg();
      const renderSvgString = new XMLSerializer().serializeToString(renderSvg);
      renderSvgInWorker(currentWorker, renderSvgString, svgControl.getTargetWidth(), svgControl.getTargetHeight());
    }
  }

  function renderSvgInWorker(worker, svg, svgWidth, svgHeight) {
    const svgJson = svgControl.getSvgJson(svg);

    const renderRequest = {
      type: "renderSvg",
      svgJson,
      width: svgControl.getTargetWidth(),
      height: svgControl.getTargetHeight(),
      svgWidth,
      svgHeight,
      homeX: currentState.homeX,
      homeY: currentState.homeY,
      infillDensity: getInfillDensity(),
      flattenPaths: getFlattenPaths(),
    };

    const workerStartTs = performance.now();

    worker.onmessage = (e) => {
      if (e.data.type === 'status') {
        $("#progressBar").text(e.data.payload);
      } else if (e.data.type === 'renderer') {
        console.log("Worker finished!");
        setPerfStat("workerMs", performance.now() - workerStartTs);

        uploadConvertedCommands = e.data.payload.commands.join('\n');
        const resultSvgJson = e.data.payload.svgJson;
        const resultDataUrl = svgControl.convertJsonToDataURL(resultSvgJson, svgControl.getTargetWidth(), svgControl.getTargetHeight());

        const totalDistanceM = +(e.data.payload.distance / 1000).toFixed(1);
        const drawDistanceM = +(e.data.payload.drawDistance / 1000).toFixed(1);

        deactivateProgressBar();
        $("#previewSvg").attr("src", resultDataUrl);
        $("#distances").text(`Total: ${totalDistanceM}m / Draw: ${drawDistanceM}m`);
        $(".svg-preview").show();
	      $("#acceptSvg").removeAttr("disabled");
	      // Commands Preview + Simulation
	      try { initJobPreviewFromCommands(uploadConvertedCommands); } catch {}

	      // SVG-Serie: Startpunkt (Prozent) vom ersten SVG übernehmen
	      try {
	        if (batchSettings && typeof batchSettings.startPercent === "number") {
	          const pct = Math.max(0, Math.min(100, batchSettings.startPercent));
	          setTimeout(() => {
	            const scrub = document.getElementById("startScrubber");
	            if (scrub) {
	              scrub.value = String(pct);
	              $(scrub).trigger("input").trigger("change");
	            }
	          }, 120);
	        }
	      } catch {}

	      // SVG-Serie: Auto-Upload (nach Render) für alle folgenden SVGs
	      if (batchAutoUploadPending) {
	        batchAutoUploadPending = false;
	        setTimeout(() => {
	          try { $("#acceptSvg").trigger("click"); } catch {}
	        }, 250);
	      }
	}
    };

    worker.postMessage(renderRequest);
  }

  function activateProgressBar() {
    const bar = $("#progressBar");
    bar.addClass("progress-bar-striped");
    bar.addClass("progress-bar-animated");
    bar.removeClass("bg-success");
    bar.text("");
  }

  function deactivateProgressBar() {
    const bar = $("#progressBar");
    bar.removeClass("progress-bar-striped");
    bar.removeClass("progress-bar-animated");
    bar.addClass("bg-success");
    bar.text("Success");
  }

  $("#infillDensity,#turdSize,#flattenPathsCheckbox").on('input change', async function() {
    activateProgressBar();
    $("#acceptSvg").attr("disabled", "disabled");
    await rendererFn();
  });

  $("#preview").click(async function() {
    $("#svgUploadSlide").hide();
    $("#chooseRendererSlide").show();
  });

  $("#pathTracing").click(async function() {
    $("label[for='turdSize'],#turdSize").hide();
    $("label[for='flattenPathsCheckbox'],#flattenPathsCheckbox").show();

    $("#chooseRendererSlide").hide();
    $("#drawingPreviewSlide").show();
    rendererFn = render_PathTracing;
    rendererKey = "path";
    await rendererFn();
  });

  $("#vectorRasterVector").click(async function() {
    $("#flattenPathsCheckbox").prop("checked", false);
    $("label[for='turdSize'],#turdSize").show();
    $("label[for='flattenPathsCheckbox'],#flattenPathsCheckbox").hide();

    $("#chooseRendererSlide").hide();
    $("#drawingPreviewSlide").show();
    rendererFn = render_VectorRasterVector;
    rendererKey = "vrv";
    await rendererFn();
  });

  $(".backToSvgSelect").click(function() {
    uploadConvertedCommands = null;
    resetUploadedFileState();

    $(".loading").show();
    activateProgressBar();
    $("#previewSvg").removeAttr("src");
    $(".svg-preview").hide();
    $("#acceptSvg").attr("disabled", "disabled");

    $("#svgUploadSlide").show();
    $("#drawingPreviewSlide").hide();
    $("#chooseRendererSlide").hide();
  });

  $("#acceptSvg").click(function() {
    // SVG-Serie: Einstellungen vom ersten SVG für alle folgenden übernehmen
    try {
      if (window.__svgBatchActive && !batchSettings) {
        batchSettings = {
          rendererKey: rendererKey || "path",
          infillDensity: parseInt($("#infillDensity").val(), 10) || 0,
          turdSize: parseInt($("#turdSize").val(), 10) || 2,
          flattenPaths: $("#flattenPathsCheckbox").is(":checked"),
          affine: (typeof svgControl.getAffineTransform === "function") ? svgControl.getAffineTransform() : null,
          startPercent: parseInt($("#startScrubber").val(), 10) || 0,
        };
        if (window.addMessage) window.addMessage(0, "SVG-Serie", "Einstellungen vom ersten SVG übernommen");
      }
    } catch {}

    if (!uploadConvertedCommands) {
      throw new Error('Commands are empty');
    }
    $("#acceptSvg").attr("disabled", "disabled");

    const commandsBlob = new Blob([uploadConvertedCommands], { type: "text/plain" });

    $(".muralSlide").hide();
    $("#uploadProgress").show();

    const formData = new FormData();
    formData.append("commands", commandsBlob);

    $.ajax({
      url: "/uploadCommands",
      data: formData,
      processData: false,
      contentType: false,
      type: 'POST',
      success: function(data) {
        verifyUpload(data);
      },
      error: function(err) {
        alert('Upload to Mural failed! ' + err);
        window.location.reload();
      },
      xhr: function () {
        var xhr = new window.XMLHttpRequest();

        xhr.upload.addEventListener("progress", function (evt) {
          if (evt.lengthComputable) {
            var percentComplete = evt.loaded / evt.total;
            percentComplete = parseInt(percentComplete * 100);
            $("#uploadProgressBarWrap").attr("aria-valuemax", evt.total.toString());
            $("#uploadProgressBarWrap").attr("aria-valuenow", evt.loaded.toString());
            $("#uploadProgressBarWrap > .progress-bar").attr("style", `width: ${percentComplete}%`);
          }
        }, false);

        return xhr;
      },
    });
  });

  $("#beginDrawing").click(function() {
    // Preview-Simulation stoppen, damit der Browser nicht sinnlos CPU verbrennt
    try { stopSim(); } catch (e) {}

    $(".muralSlide").hide();
    $("#drawingBegan").show();

    // Telemetrie sofort starten (UI bleibt responsiv)
    startTelemetry();

    // Roboter SOFORT starten – nicht erst nach Download/Parse (das kann groß sein)
    $.post("/run", { startLine: selectedStartLine })
      .fail(function(xhr) {
        const msg = (xhr && (xhr.responseText || xhr.statusText || xhr.status)) ? (xhr.responseText || xhr.statusText || xhr.status) : "Unbekannter Fehler";
        alert("Start fehlgeschlagen: " + msg);
      });

    // Live-Malbereich vorbereiten (nicht-blockierend)
    if (jobModel) {
      setupLiveCanvasIfPossible();
    } else {
      ensureJobModelLoadedFromDevice(6000).then(function() {
        setupLiveCanvasIfPossible();
      });
    }
  });

  $("#reset").click(function() {
    stopTelemetry();
    doneWithPhase();
    location.reload();
  });

  $("#leftMotorTool").on('input', function() {
    const leftMotorDir = parseInt($("#leftMotorTool").val());
    if (leftMotorDir <= -1) {
      client.leftRetractDown();
    } else if (leftMotorDir >= 1) {
      client.leftExtendDown();
    } else {
      client.leftRetractUp();
    }
  });

  $("#rightMotorTool").on('input', function() {
    const rightMotorDir = parseInt($("#rightMotorTool").val());
    if (rightMotorDir <= -1) {
      client.rightRetractDown();
    } else if (rightMotorDir >= 1) {
      client.rightExtendDown();
    } else {
      client.rightRetractUp();
    }
  });

  $("#estepsTool").click(function() {
    $.post("/estepsCalibration", {});
  });

  const toolsModal = $("#toolsModal")[0];
  toolsModal.addEventListener('hidden.bs.modal', function () {
    client.rightRetractUp();
    client.leftRetractUp();
  });

  // ===== System Modal: beim Öffnen laden =====
  const systemModal = $("#systemModal")[0];
  systemModal.addEventListener('shown.bs.modal', function () {
    loadSystemInfo();
  });

  // Reboot Button
  document.getElementById("rebootBtn").addEventListener("click", async () => {
    const ok = confirm("Plotter wirklich neu starten?");
    if (!ok) return;
    await fetch("/reboot");
  });


  // ===== Job Preview / Live Controls =====
  $("#simPlayPause").click(async function() {
    if (!jobModel) return;
    if (simIsPlaying) {
      stopSim();
      $(this).text("Vorschau abspielen");
    } else {
      $(this).text("Pause Vorschau");
      await startSim();
    }
  });

  $("#simSpeed").on("input change", function() {
    simSpeedMult = parseInt($(this).val(), 10) || 15;
    $("#simSpeedText").text(`${simSpeedMult}×`);
  });

  $("#startScrubber").on("input change", async function() {
    if (!jobModel) return;
    const percent = parseInt($(this).val(), 10) || 0;
    const sel = findStartByPercent(jobModel, percent);
    selectedStartLine = sel.startLine;
    selectedStartDist = sel.startDist;
    $("#startScrubberText").text(`${percent}%  (startLine ${selectedStartLine})`);

    // Wenn Preview nicht läuft, Position + Marker updaten
    if (!simIsPlaying) {
      await renderPlannedPreview();
      updatePreviewOverlay();
    }
  });

  $("#downloadCommandsLocal").click(function() {
    if (!uploadConvertedCommands) {
      alert("Keine Commands vorhanden.");
      return;
    }
    const ts = new Date();
    const pad = (n) => String(n).padStart(2, "0");
    const name = `commands_${ts.getFullYear()}${pad(ts.getMonth()+1)}${pad(ts.getDate())}_${pad(ts.getHours())}${pad(ts.getMinutes())}${pad(ts.getSeconds())}.txt`;
    downloadTextFile(name, uploadConvertedCommands);
  });

  $("#downloadCommandsFromDevice").click(function() {
    // Dieser Button ist jetzt: lokale TXT importieren (vorher exportierte Commands wiederverwenden)
    const inp = document.getElementById("importCommandsInput");
    if (!inp) {
      alert("Import-Input fehlt (Bug)");
      return;
    }
    inp.value = ""; // allow re-import same file
    inp.click();
  });


  $("#downloadCommandsDeviceLink").click(async function(ev) {
    ev.preventDefault();
    try {
      const res = await fetch("/downloadCommands", { cache: "no-store" });
      if (!res.ok) throw new Error("HTTP " + res.status);
      const txt = await res.text();
      const ts = new Date();
      const pad = (n) => String(n).padStart(2, "0");
      const name = `commands_device_${ts.getFullYear()}${pad(ts.getMonth()+1)}${pad(ts.getDate())}_${pad(ts.getHours())}${pad(ts.getMinutes())}${pad(ts.getSeconds())}.txt`;
      downloadTextFile(name, txt);
    } catch (e) {
      alert("Commands vom Gerät Download fehlgeschlagen: " + (e?.message || e));
    }
  });

  // Import: lokale Commands-TXT laden
  const importInp = document.getElementById("importCommandsInput");
  if (importInp) {
    importInp.addEventListener("change", async (ev) => {
      try {
        const file = ev.target.files && ev.target.files[0];
        if (!file) return;

        const txtRaw = await file.text();
        const txt = String(txtRaw || "").replace(/\r\n/g, "\n").replace(/\r/g, "\n");
        if (!txt || txt.trim().length < 3) {
          alert("Die Datei ist leer.");
          return;
        }

        uploadConvertedCommands = txt;

        // Vorschau aktivieren
        try {
          initJobPreviewFromCommands(uploadConvertedCommands);
        } catch (e) {
          console.warn("JobPreview init failed", e);
        }

        // Damit man direkt hochladen kann (ohne SVG/PNG Pfad)
        $("#acceptSvg").removeAttr("disabled");

      } catch (e) {
        console.error(e);
        alert("Import fehlgeschlagen: " + (e?.message || e));
      }
    });
  }

  
  // Pen staged sliders (0..80), apply on next transition only
  $("#drawPenDownSlider").on("input change", async function() {
    const v = Math.max(0, Math.min(80, parseInt(this.value || "0", 10) || 0));
    const t = document.getElementById("drawPenDownValue");
    if (t) t.textContent = String(v);
    try {
      await fetch("/pen/down/set", {
        method: "POST",
        headers: { "Content-Type": "application/x-www-form-urlencoded" },
        body: "value=" + encodeURIComponent(String(v))
      });
      liveHud.pendingDown = v;
      liveHud.hasPendingDown = true;
    } catch (e) {
      console.error(e);
    }
  });

  $("#drawPenUpSlider").on("input change", async function() {
    const v = Math.max(0, Math.min(80, parseInt(this.value || "0", 10) || 0));
    const t = document.getElementById("drawPenUpValue");
    if (t) t.textContent = String(v);
    try {
      await fetch("/pen/up/set", {
        method: "POST",
        headers: { "Content-Type": "application/x-www-form-urlencoded" },
        body: "value=" + encodeURIComponent(String(v))
      });
      liveHud.pendingUp = v;
      liveHud.hasPendingUp = true;
    } catch (e) {
      console.error(e);
    }
  });

// Pause / Resume / Stop
  $("#pauseBtn").click(async function() {
    try {
      await fetch("/pauseJob", { method: "POST" });
      const modalEl = document.getElementById("pauseModal");
      const modal = new bootstrap.Modal(modalEl);
      document.getElementById("pauseStateText").textContent = "Pausiert";
      modal.show();
    } catch (e) {
      alert("Pause fehlgeschlagen: " + (e?.message || e));
    }
  });

  $("#resumeBtn").click(async function() {
    try {
      await fetch("/resumeJob", { method: "POST" });
      const modalEl = document.getElementById("pauseModal");
      const modal = bootstrap.Modal.getInstance(modalEl);
      if (modal) modal.hide();
    } catch (e) {
      alert("Resume fehlgeschlagen: " + (e?.message || e));
    }
  });

  async function stopJob() {
    const ok = confirm("Job wirklich beenden? (Stift hoch + Home)");
    if (!ok) return;
    try {
      await fetch("/stopJob", { method: "POST" });
      // UI bleibt auf Drawing, aber der Bot fährt sauber heim.
    } catch (e) {
      alert("Stop fehlgeschlagen: " + (e?.message || e));
    }
  }

  $("#stopBtn").click(stopJob);
  $("#stopBtnModal").click(async function() {
    const modalEl = document.getElementById("pauseModal");
    const modal = bootstrap.Modal.getInstance(modalEl);
    if (modal) modal.hide();
    await stopJob();
  });

  svgControl.initSvgControl();

  $("#loadingSlide").show();

  $.get("/getState", function(data) {
    adaptToState(data);
  }).fail(function() {
    alert("Failed to retrieve state");
  });

  // ===== WebLog Init =====
  bindLogUI();
  initLiveHudToggle();
  // Dateimanager: V2 bevorzugen (separates file_manager_v2.js). Legacy nur Fallback.
  if (typeof window.initFileManagerV2 === "function") {
    try { window.initFileManagerV2(); } catch (e) { console.warn("initFileManagerV2 failed", e); }
  } else {
    initFileManager();
  }

  // Legacy-Filemanager-Funktionen bleiben im Code (nur Initialisierung wird umgeschaltet).

  // -------- SVG-Serie (UX-Fix V2) --------
  // Popup (Weiter/Abbruch) -> ohne UI-Änderungen: confirm()
  window.openConfirmModal = function(msg, onYes, onNo) {
    try {
      if (window.confirm(String(msg || "Weiter?"))) {
        if (typeof onYes === "function") onYes();
      } else {
        if (typeof onNo === "function") onNo();
      }
    } catch {
      try { if (typeof onNo === "function") onNo(); } catch {}
    }
  };

  // SD-Folder listing für svgBatch.js
  if (!window.listCurrentSdFolder) window.listCurrentSdFolder = async function() {
    const path = fmState.path || "/";
    const vol = "sd";
    const res = await fetch(`/fs/list?vol=${encodeURIComponent(vol)}&path=${encodeURIComponent(path)}`, { cache: "no-store" });
    if (!res.ok) throw new Error("FS list failed HTTP " + res.status);
    const data = await res.json();
    const entries = Array.isArray(data.entries) ? data.entries : [];
    // normalize to {name, path, dir, size}
    return entries.map(e => {
      const p = String(e.name || "");
      const base = fm_baseName(p);
      return { name: base, path: p, dir: !!e.dir, size: Number(e.size || 0) };
    });
  };

  // SD-SVG laden (für svgBatch.js) + Batch-Auto-Processing
  if (!window.loadSvgFromSd) window.loadSvgFromSd = async function(path) {
    const vol = "sd";
    const p = String(path || "");
    if (!p) throw new Error("Kein Pfad");

    // UI auf SVG-Auswahl bringen (so wirkt es logisch)
    try { $(".muralSlide").hide(); $("#svgUploadSlide").show(); } catch {}

    // State reset (nur Upload-Flow)
    try {
      uploadConvertedCommands = null;
      resetUploadedFileState();
      $("#preview").removeAttr("disabled");
      $("#acceptSvg").attr("disabled", "disabled");
      $(".svg-preview").hide();
    } catch {}

    const res = await fetch(`/fs/read?vol=${encodeURIComponent(vol)}&path=${encodeURIComponent(p)}`, { cache: "no-store" });
    if (!res.ok) throw new Error("SVG lesen fehlgeschlagen HTTP " + res.status);
    const svgText = await res.text();

    cachedUploadedKind = "svg";
    cachedUploadedSvgString = svgText;

    // SVG in Vorschau setzen
    try {
      svgControl.setSvgString(svgText, currentState);
      $(".svg-control").show();
      $("#preview").removeAttr("disabled");
    } catch (e) {
      console.warn("setSvgString failed", e);
    }

    // Batch: ab dem 2. SVG automatisch gleiche Settings anwenden + rendern + uploaden
    try {
      const isBatch = !!window.__svgBatchActive;
      const idx = Number(window.__svgBatchIndex || 0);
      if (isBatch && batchSettings && idx > 0) {
        // Transform übernehmen
        if (batchSettings.affine && typeof svgControl.setAffineTransform === "function") {
          svgControl.setAffineTransform(batchSettings.affine);
        }

        // Regler übernehmen
        $("#infillDensity").val(batchSettings.infillDensity ?? 0);
        $("#turdSize").val(batchSettings.turdSize ?? 2);
        $("#flattenPathsCheckbox").prop("checked", !!batchSettings.flattenPaths);

        // Renderer wählen (ohne UI-Klick)
        rendererKey = (batchSettings.rendererKey === "vrv") ? "vrv" : "path";
        rendererFn = (rendererKey === "vrv") ? render_VectorRasterVector : render_PathTracing;

        // direkt in Preview-Slide (damit Fortschritt sichtbar ist)
        $("#svgUploadSlide").hide();
        $("#chooseRendererSlide").hide();
        $("#drawingPreviewSlide").show();

        uploadConvertedCommands = null;
        batchAutoUploadPending = true;
        activateProgressBar();
        $("#acceptSvg").attr("disabled", "disabled");

        await rendererFn();
      }
    } catch (e) {
      console.error(e);
      if (window.addMessage) window.addMessage(2, "SVG-Serie", "Auto-Render fehlgeschlagen: " + (e?.message || e));
    }
  };

  // Batch-Start: Ordner wählen (Dateimanager) -> dann Serie starten
  function openBatchFolderPicker() {
    try {
      // Neue Serie -> Settings neu sammeln
      batchSettings = null;
      batchAutoUploadPending = false;
    } catch {}

    // File Manager öffnen, auf SD stellen (V2/Legacy kompatibel)
    const _fm = (window.fmState || fmState);
    try {
      _fm.vol = "sd";
      if (!_fm.path) _fm.path = "/";
    } catch {}

    const _enterPick = (window.fm_enterFolderPickMode || fm_enterFolderPickMode);
    const _refresh   = (window.fm_refresh || fm_refresh);
    const _normPath  = (window.fm_normPath || fm_normPath);

    // Button im Dateimanager zeigt: "Ordner für SVG-Serie nutzen"
    _enterPick(async (path) => {
      try {
        _fm.vol = "sd";
        _fm.path = _normPath(String(path || "/"));
        await _refresh();

        // Serie starten (Queue + Index kommt aus svgBatch.js)
        if (typeof window.startSvgBatchFromCurrentFolder === "function") {
          window.startSvgBatchFromCurrentFolder();
        } else if (typeof startSvgBatchFromCurrentFolder === "function") {
          startSvgBatchFromCurrentFolder();
        } else {
          try { if (window.addMessage) window.addMessage(2, "SVG-Serie", "svgBatch.js nicht geladen"); } catch {}
          try { window.dispatchEvent(new CustomEvent("svgBatch:error", { detail: { message: "svgBatch.js nicht geladen" } })); } catch {};
        }
      } catch (e) {
        console.error(e);
        try { if (window.addMessage) window.addMessage(2, "SVG-Serie", "Start fehlgeschlagen: " + (e?.message || e)); } catch {}
        try { window.dispatchEvent(new CustomEvent("svgBatch:error", { detail: { message: "Start fehlgeschlagen: " + (e?.message || e) } })); } catch {};
      }
    });

    const modalEl = document.getElementById("fileManagerModal");
    if (modalEl) {
      const inst = bootstrap.Modal.getOrCreateInstance(modalEl);
      inst.show();
    }
  }

  // Neuer Button: direkt in "Bild auswählen" (UX-Fix)
  const btnBatchSelect = document.getElementById("startSvgBatchSelectFolder");
  if (btnBatchSelect) btnBatchSelect.onclick = (e) => { e.preventDefault(); openBatchFolderPicker(); };

  // Alter Button bleibt (BeginDrawing): startet ebenfalls per Ordner-Auswahl
  const btnBatchOld = document.getElementById("startSvgBatch");
  if (btnBatchOld) btnBatchOld.onclick = (e) => { e.preventDefault(); openBatchFolderPicker(); };

  startLogPolling();
  pollLogs(false);
  bindHelpModalDiagnostics();
}

// ------- Rest deines Codes (WebLog, verifyUpload, adaptToState, telemetry, system, help, servo tools) -------
// AB HIER: unverändert von dir, genau wie du es gepostet hast.


// ===== WebLog Polling (Firmware) =====
let remoteSeenSeq = 0;
const remoteSeqSet = new Set();
const remoteLogs = []; // newest first
let logPollTimer = null;

function parseFirmwareMsg(msg) {
  const s = String(msg ?? "");
  const parts = s.split("|");
  if (parts.length >= 2) {
    const title = parts[0].trim();
    const subtitle = parts.slice(1).join("|").trim();
    return { title, subtitle };
  }
  return { title: s, subtitle: "" };
}

function fmtDateDE(d) {
  try {
    const date = d.toLocaleDateString("de-DE");
    const time = d.toLocaleTimeString("de-DE", { hour12: false });
    return { date, time };
  } catch {
    return { date: "—", time: "—" };
  }
}

function levelClass(lvl) {
  if (lvl === 2) return "error";
  if (lvl === 1) return "warn";
  return "notice";
}

function renderLogs() {
  const list = document.getElementById("logList");
  if (!list) return;

  // Kombi: firmware logs + local logs (UI)
  const all = [];

  for (const r of remoteLogs) {
    const dt = r.iso ? new Date(r.iso) : null;
    const ts = dt && !isNaN(dt.getTime()) ? dt.getTime() : Date.now();
    all.push({
      _src: "fw",
      level: r.level,
      seq: r.seq,
      ts,
      iso: r.iso,
      msg: r.msg
    });
  }

  for (const l of localLogs) {
    all.push({
      _src: "ui",
      level: l.level,
      seq: 0,
      ts: l.ts,
      iso: null,
      msg: (l.subtitle ? `${l.title} | ${l.subtitle}` : l.title)
    });
  }

  // Neueste zuerst
  all.sort((a,b) => b.ts - a.ts);

  list.innerHTML = "";

  for (const it of all) {
    if (!enabledLevels.has(Number(it.level))) continue;

    const d = new Date(it.ts);
    const { date, time } = fmtDateDE(d);

    const seqTxt = it.seq ? `#${it.seq} · ${date}` : `${date}`;
    const parsed = parseFirmwareMsg(it.msg);

    const row = document.createElement("div");
    row.className = "logRow";

    row.innerHTML = `
      <div class="logDot ${levelClass(Number(it.level))}"></div>
      <div class="logText">
        <div class="logTitleLine">${escapeHtml(seqTxt)}</div>
        <div class="logSubLine">${escapeHtml(parsed.title)}${parsed.subtitle ? " | " + escapeHtml(parsed.subtitle) : ""}</div>
      </div>
      <div class="logTime">${escapeHtml(time)}</div>
    `;

    list.appendChild(row);
  }
}

async function pollLogs(forceRender = false) {
  try {
    const url = remoteSeenSeq > 0 ? `/logs?after=${remoteSeenSeq}` : "/logs";
    const res = await fetch(url, { cache: "no-store" });
    if (!res.ok) return;
    const data = await res.json();
    const items = Array.isArray(data.logs) ? data.logs : [];

    // Firmware sendet neueste zuerst -> für Unseen zählen: jede neue seq
    let newCount = 0;

    for (let i = items.length - 1; i >= 0; i--) {
      const e = items[i];
      const seq = Number(e.seq || 0);
      if (!seq || remoteSeqSet.has(seq)) continue;
      remoteSeqSet.add(seq);
      remoteSeenSeq = Math.max(remoteSeenSeq, seq);

      remoteLogs.unshift({
        seq,
        level: Number(e.level || 0),
        iso: (e.iso || ""),
        msg: String(e.msg || "")
      });
      newCount++;
    }

    // Limit (RAM im Browser)
    if (remoteLogs.length > 500) remoteLogs.length = 500;

    if (newCount > 0 && !logVisible) {
      unseenCount += newCount;
      updateBadge();
    }

    if (logVisible || forceRender) renderLogs();
  } catch {
    // ignore
  }
}

function startLogPolling() {
  if (logPollTimer) return;
  logPollTimer = setInterval(() => pollLogs(false), 1200);
}

function stopLogPolling() {
  if (!logPollTimer) return;
  clearInterval(logPollTimer);
  logPollTimer = null;
}

function bindLogUI() {
  const panel = document.getElementById("logPanel");
  if (!panel) return;

  const sideBtn   = document.getElementById("logToggleSide");
  const oldToggle = document.getElementById("logToggle");
  const doneBtn   = document.getElementById("logDone");

  syncEnabledLevelsFromUI();

  const setVisible = (v) => {
    logVisible = v;
    panel.classList.toggle("hidden", !v);
    panel.setAttribute("aria-hidden", v ? "false" : "true");

    if (v) {
      unseenCount = 0;
      updateBadge();
      pollLogs(true);
    }
  };

  if (sideBtn)   sideBtn.onclick   = () => setVisible(!logVisible);
  if (oldToggle) oldToggle.onclick = () => setVisible(!logVisible);
  if (doneBtn)   doneBtn.onclick   = () => setVisible(false);

  panel.querySelectorAll('input[type="checkbox"][data-level]').forEach(cb => {
    cb.addEventListener("change", () => {
      syncEnabledLevelsFromUI();
      pollLogs(true);
    });
  });

  setVisible(false);
  updateBadge();
}

window.clearLogs = async function clearLogs() {
  localLogs = [];
  maxSeenTs = 0;
  unseenCount = 0;

  // Firmware-Logs lokal cache leeren
  remoteSeenSeq = 0;
  remoteSeqSet.clear();
  remoteLogs.length = 0;

  updateBadge();

  try { await fetch("/logs/clear", { method: "POST" }); } catch {}

  const list = document.getElementById("logList");
  if (list) list.innerHTML = "";
};

function updateBadge() {
  const badge = document.getElementById("msgBadge");
  if (!badge) return;
  const show = (unseenCount > 0 && !logVisible);
  badge.classList.toggle("hidden", !show);
  badge.textContent = show ? String(unseenCount) : "";
}


function escapeHtml(s) {
  return String(s)
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;")
    .replaceAll('"', "&quot;")
    .replaceAll("'", "&#039;");
}

window.addMessage = function addMessage(level, title, subtitle = "") {
  const ts = Date.now();
  localLogs.push({ level, ts, title, subtitle, _local: true });

  if (!logVisible) {
    unseenCount++;
    updateBadge();
  }
  pollLogs(true);
};

function syncEnabledLevelsFromUI() {
  enabledLevels = new Set();

  document.querySelectorAll('#logPanel input[type="checkbox"][data-level]').forEach(cb => {
    const lvl = Number(cb.dataset.level);
    if (cb.checked) enabledLevels.add(lvl);
  });

  if (enabledLevels.size === 0) {
    const errCb = document.querySelector('#logPanel input[type="checkbox"][data-level="2"]');
    if (errCb) errCb.checked = true;
    enabledLevels.add(2);
  }
}

function verifyUpload(state) {
  $.ajax({
    url: "/downloadCommands",
    processData: false,
    contentType: false,
    type: 'GET',
    success: function(data) {
      const receivedData = data.split('\n');
      const sentData = uploadConvertedCommands.split('\n');
      if (receivedData.length !== sentData.length) {
        alert("Data verification failed");
        window.location.reload();
        return;
      }
      for (let i = 0; i < receivedData.length; i++) {
        if (receivedData[i] !== sentData[i]) {
          alert("Data verification failed");
          window.location.reload();
          return;
        }
      }
      setTimeout(function() {
        adaptToState(state);
      }, 1000);
    },
    error: function(err) {
      alert('Failed to download commands from Mural! ' + err);
      window.location.reload();
    },
    xhr: function () {
      var xhr = new window.XMLHttpRequest();
      xhr.addEventListener("progress", function (evt) {
        if (evt.lengthComputable) {
          var percentComplete = evt.loaded / evt.total;
          percentComplete = parseInt(percentComplete * 100);
          $("#verificationProgress").attr("aria-valuemax", evt.total.toString());
          $("#verificationProgress").attr("aria-valuenow", evt.loaded.toString());
          $("#verificationProgress > .progress-bar").attr("style", `width: ${percentComplete}%`);
        }
      }, false);

      return xhr;
    },
  });
}

function adaptToState(state) {
  $(".muralSlide").hide();
  currentState = state;
  switch(state.phase) {
    case "RetractBelts":
      $("#retractBeltsSlide").show();
      break;
    case "SetTopDistance":
      $("#distanceBetweenAnchorsSlide").show();
      break;
    case "ExtendToHome":
      $("#extendToHomeSlide").show();
      if (state.moving || state.startedHoming) {
        $("#extendToHome").prop("disabled", true);
        $("#extendingSpinner").css('visibility', 'visible');
        checkIfExtendedToHome();
      }
      break;
    case "PenCalibration":
      $.post("/setServo", {angle: 90});
      $("#penCalibrationSlide").show();
      break;
    case "SvgSelect":
      $("#svgUploadSlide").show();
      break;
    case "BeginDrawing":
      $("#beginDrawingSlide").show();
      break;
    default:
      alert("Unrecognized phase");
  }
}

function getInfillDensity() {
  const density = parseInt($("#infillDensity").val());
  if ([0, 1, 2, 3, 4].includes(density)) return density;
  throw new Error('Invalid density');
}

function getTurdSize() {
  return parseInt($("#turdSize").val());
}

function getFlattenPaths() {
  return $("#flattenPathsCheckbox").is(":checked");
}

let perfPollTimer;
let telemetryTimer;

function startPerfPoll() {
  if (perfPollTimer) return;
  perfPollTimer = setInterval(async () => {
    try {
      const res = await fetch("/status", { cache: "no-store" });
      if (!res.ok) return;
      const data = await res.json();
      updateLiveHudFromStatus(data);
      renderPenStageControls();

      // Event: Telemetrie Tick (für HUD/Stats)
      try { window.dispatchEvent(new CustomEvent("mural:telemetry", { detail: data })); } catch {}
      const p = data.perf || {};
      setPerfStat("fwLoopMs",    p.loop_ms);
      setPerfStat("fwYieldMs",   p.yield_ms);
      setPerfStat("fwMoveMs",    p.move_ms);
      setPerfStat("fwRunnerMs",  p.runner_ms);
      setPerfStat("fwPhaseMs",   p.phase_ms);
      setPerfStat("fwMaxLoopMs", p.max_loop_ms);
    } catch {}
  }, Math.max(250, UI_TUNING.STATUS_POLL_MS));
}

function stopPerfPoll() {
  if (!perfPollTimer) return;
  clearInterval(perfPollTimer);
  perfPollTimer = null;
}

function startTelemetry() {
  if (telemetryTimer) return;
  telemetryTimer = setInterval(async () => {
    try {
      const res = await fetch("/status", { cache: "no-store" });
      if (!res.ok) throw new Error("HTTP " + res.status);
      const data = await res.json();
      updateLiveHudFromStatus(data);
      renderPenStageControls();

      document.getElementById("coordsDisplay").textContent =
        `X: ${data.x.toFixed(1)}  Y: ${data.y.toFixed(1)}`;
      document.getElementById("drawProgressBar").style.width = `${data.progress}%`;
      document.getElementById("drawProgressText").textContent = `${data.progress}%`;

      // Job-Ende erkennen (running=true -> false bei hohem Progress) -> automatisch zu SVG-Upload wechseln
      try {
        const last = !!window.__uiLastRunning;
        const now  = !!data.running;

        // Keep last state always updated
        window.__uiLastRunning = now;

        // Only react once per finish edge
        if (last && !now && (Number(data.progress) >= 99) && !data.paused) {
          if (!window.__uiFinishLatch) {
            window.__uiFinishLatch = true;

            // Wenn Batch aktiv: optionaler Callback bleibt kompatibel
            try {
              if (window.__svgBatchActive && typeof window.onBatchDrawingFinished === "function") {
                window.onBatchDrawingFinished();
              }
            } catch {}

            // UI: direkt zur Upload-Seite springen (Firmware-Phase bleibt ggf. BeginDrawing)
            try {
              $(".muralSlide").hide();
              $("#beginDrawingSlide").hide();
              $("#chooseRendererSlide").hide();
              $("#svgUploadSlide").show();

              // Upload-UI zurücksetzen, damit Preview wieder korrekt erscheint
              const input = document.getElementById("uploadSvg");
              if (input) input.value = "";

              $("#sourceSvg").hide();
              $(".svg-control").hide();
              $("#preview").prop("disabled", true);

              // oben anfangen, sonst bleibt man optisch "hängen"
              try { window.scrollTo(0, 0); } catch {}
            } catch (e) {
              console.warn("Auto-switch to SVG upload failed", e);
            }
          }
        }

        // Latch wieder freigeben, sobald wieder running=true (nächster Job)
        if (now) window.__uiFinishLatch = false;
      } catch {}
// Firmware-Perf (Main loop): gemessene Werte anzeigen (nicht deine UI-Settings)
      try {
        const p = data.perf || {};
        setPerfStat("fwLoopMs",    p.loop_ms);
        setPerfStat("fwYieldMs",   p.yield_ms);
        setPerfStat("fwMoveMs",    p.move_ms);
        setPerfStat("fwRunnerMs",  p.runner_ms);
        setPerfStat("fwPhaseMs",   p.phase_ms);
        setPerfStat("fwMaxLoopMs", p.max_loop_ms);
      } catch {}

      try {
        if (jobModel) {
          advanceLiveToProgress(data.progress);
          updateLiveOverlay(data.x, data.y, data.progress, !!data.paused);
        }
      } catch (e) {
        console.warn('live overlay error', e);
      }

    } catch (err) {
      console.warn("Telemetry fetch failed:", err);
      const c = document.getElementById("coordsDisplay");
      if (c) c.textContent = "Telemetry ERROR (Konsole)";
    }
  }, UI_TUNING.STATUS_POLL_MS);
}

function stopTelemetry() {
  // Event: Job/Telemetry stop
  try { window.dispatchEvent(new CustomEvent("mural:jobStopped", { detail: { ts: Date.now() } })); } catch {}
  clearInterval(telemetryTimer);
  telemetryTimer = null;
}

const BG_COLORS = [
  "#00FFFF",
  "#00E5FF",
  "#00B0FF",
  "#0091FF",
  "#2979FF",
  "#3D5AFE",
  "#536DFE",
  "#7C4DFF",
  "#B388FF",
  "#E040FB",
  "#F500FF",
  "#FF00FF",
  "#FF00CC",
  "#FF1493",
  "#FF2D55",
  "#FF1744",
  "#FF3D00",
  "#FF6D00",
  "#FF9100",
  "#FFAB00",
  "#FFD600",
  "#FFEA00",
  "#FFFF00",
  "#EEFF41",
  "#C6FF00",
  "#B2FF59",
  "#76FF03",
  "#64DD17",
  "#00E676",
  "#00FF7F",
  "#00FF00",
  "#39FF14",
  "#00FF66",
  "#00FF99",
  "#00FFCC",
  "#00FFEE",
  "#00FFD5",
  "#00FFB3",
  "#00FF90",
  "#00FF6E",
  "#00FF4B",
  "#00FF29",
  "#00FF06",
  "#7DFF00",
  "#A8FF00",
  "#D4FF00",
  "#FFF700",
  "#FFEE00",
  "#FFE600",
  "#FFDD00",
  "#FFD400",
  "#FFCC00",
  "#FFC400",
  "#FFBB00",
  "#FFB300",
  "#FFAA00",
  "#FFA200",
  "#FF9900",
  "#FF9100",
  "#FF8800",
  "#FF8000",
  "#FF7700",
  "#FF6F00",
  "#FF6600",
  "#FF5E00",
  "#FF5500",
  "#FF4D00",
  "#FF4400",
  "#FF3C00",
  "#FF3300",
  "#FF2B00",
  "#FF2200",
  "#FF1A00",
  "#FF1100",
  "#FF0900",
  "#FF0000",
  "#FF0055",
  "#FF0077",
  "#FF0099",
  "#FF00BB",
  "#FF00DD",
  "#FF00EE",
  "#FF00FF",
  "#E600FF",
  "#CC00FF",
  "#B300FF",
  "#9900FF",
  "#8000FF",
  "#6600FF",
  "#4D00FF",
  "#3300FF",
  "#1A00FF",
  "#0000FF",
  "#001AFF",
  "#0033FF",
  "#004DFF",
  "#0066FF",
  "#0080FF",
  "#0099FF",
  "#00B3FF",
  "#00CCFF",
  "#00E6FF",
  "#00FFEA",
  "#00FFD5",
  "#00FFBF",
  "#00FFAA",
  "#00FF95",
  "#00FF7F",
  "#00FF6A",
  "#00FF55",
  "#00FF40",
  "#00FF2B",
  "#00FF15",
  "#15FF00",
  "#2BFF00",
  "#40FF00",
  "#55FF00",
  "#6AFF00",
  "#7FFF00",
  "#95FF00",
  "#AAFF00",
  "#BFFF00",
  "#D5FF00",
  "#EAFF00"
];


function randomBackgroundFade() {
  const color = BG_COLORS[Math.floor(Math.random() * BG_COLORS.length)];
  document.documentElement.style.setProperty("--bg-color", color);
}
randomBackgroundFade();
setInterval(randomBackgroundFade, 30000);

async function loadSystemInfo() {
  const el = (id) => document.getElementById(id);

  ["sysFirmware","sysReset","sysFs","sysSd","sysRam","sysNet","sysCpu","sysState","sysWarn"]
    .forEach(id => el(id).innerText = "Lade…");

  try {
    const [sysRes, statusRes] = await Promise.all([
      fetch("/sysinfo"),
      fetch("/status")
    ]);

    if (!sysRes.ok) throw new Error("sysinfo");
    const sys = await sysRes.json();

    let status = {};
    if (statusRes.ok) {
      try { status = await statusRes.json(); } catch { status = {}; }
    }

    const fmtMB = (b) => (b/1024/1024).toFixed(2) + " MB";
    const pct = (x) => (x*100).toFixed(0) + "%";

    const resetMap = {
      1: "Power-On",
      2: "External Reset",
      3: "Software Reset",
      4: "Panic/Crash",
      5: "Int WDT",
      6: "Task WDT",
      7: "WDT",
      8: "Deep Sleep",
      9: "Brownout",
      10: "SDIO Reset"
    };
    const resetText = resetMap[sys.reset_reason] || ("Unbekannt (" + sys.reset_reason + ")");

    const freePct = sys.fs_total > 0 ? (sys.fs_free / sys.fs_total) : 0;

    el("sysFirmware").innerHTML = `<b>${sys.firmware}</b><br>Build: ${sys.build}`;
    el("sysReset").innerHTML = `${resetText}<br>Uptime: ${sys.uptime_s}s`;

    el("sysFs").innerHTML =
      `Frei: <b>${fmtMB(sys.fs_free)}</b> (${pct(freePct)})<br>` +
      `Belegt: ${fmtMB(sys.fs_used)}<br>` +
      `Gesamt: ${fmtMB(sys.fs_total)}<br>` +
      `Größte Datei: ${sys.fs_largest || "—"}`;

    const sdFreePct = sys.sd_total > 0 ? (sys.sd_free / sys.sd_total) : 0;

    el("sysSd").innerHTML = sys.sd_mounted
      ? (`Status: <b>gemountet</b> (CS ${sys.sd_cs})<br>` +
         `Frei: <b>${fmtMB(sys.sd_free)}</b> (${pct(sdFreePct)})<br>` +
         `Belegt: ${fmtMB(sys.sd_used)}<br>` +
         `Gesamt: ${fmtMB(sys.sd_total)}`)
      : (`Status: <b>nicht gemountet</b> (CS ${sys.sd_cs})`);


    el("sysRam").innerHTML =
      `Heap frei: <b>${sys.heap} B</b><br>` +
      `Min-Heap: ${sys.min_heap} B`;

    el("sysNet").innerHTML =
      `IP: <b>${sys.ip}</b><br>` +
      `Host: ${sys.host}<br>` +
      `RSSI: ${sys.rssi} dBm`;

    el("sysCpu").innerHTML =
      `CPU: <b>${sys.cpu_mhz} MHz</b><br>` +
      `Board: ${sys.board}`;

    const mode = status.phase || status.state || status.mode || sys.mode || "unknown";
    const job = (status.progress ?? status.percent ?? status.job ?? sys.job ?? 0);
    el("sysState").innerHTML =
      `Modus: <b>${mode}</b><br>` +
      `Job: ${job}%`;

    let level = "ok";
    let label = "OK";

    if (freePct < 0.10 || sys.heap < 40000 || sys.rssi < -80) {
      level = "bad"; label = "Kritisch";
    } else if (freePct < 0.20 || sys.heap < 80000 || sys.rssi < -70) {
      level = "warn"; label = "Achtung";
    }

    el("sysWarn").innerHTML =
      `<div class="sys-badge sys-${level}"><span class="sys-dot"></span>${label}</div>` +
      `<div style="margin-top:10px; opacity:0.85;">` +
      `FS frei: ${pct(freePct)}<br>` +
      `Heap: ${sys.heap} B<br>` +
      `RSSI: ${sys.rssi} dBm` +
      `</div>`;

  } catch (e) {
    console.error(e);
    el("sysWarn").innerHTML =
      `<div class="sys-badge sys-bad"><span class="sys-dot"></span>Fehler</div>` +
      `<div style="margin-top:10px; opacity:0.85;">Systemdaten konnten nicht geladen werden.</div>`;
  }
}

function renderHelpDiagTable(diag) {
  const tbody = document.getElementById("helpDiagTable");
  if (!tbody) return;

  const order = [
    "printSpeedSteps","moveSpeedSteps","INFINITE_STEPS","acceleration",
    "stepsPerRotation","diameter","circumference","midPulleyToWall",
    "homedStepOffsetMM","homedStepsOffset","mass_bot","g_constant",
    "d_t","d_p","d_m","belt_elongation_coefficient","HOME_Y_OFFSET_MM",
    "safeYFraction","safeXFraction","LEFT_STEP_PIN","LEFT_DIR_PIN","LEFT_ENABLE_PIN",
    "RIGHT_STEP_PIN","RIGHT_DIR_PIN","RIGHT_ENABLE_PIN","RIGHT_MS0_PIN","RIGHT_MS1_PIN","RIGHT_MS2_PIN"
  ];

  const DIAG_META = {
    printSpeedSteps: { unit: "steps/s", desc: "Geschwindigkeit beim Zeichnen (Stift unten)" },
    moveSpeedSteps:  { unit: "steps/s", desc: "Geschwindigkeit für Fahrten (Stift oben)" },
    INFINITE_STEPS:  { unit: "steps",   desc: "Jog-Schritte (Pseudo-unendlich) für manuelle Bewegung" },
    acceleration:    { unit: "steps/s²", desc: "Beschleunigung der Stepper" },
    stepsPerRotation:{ unit: "steps/rot", desc: "Steps pro Umdrehung inkl. Microstepping" },
    diameter:        { unit: "mm", desc: "Spulendurchmesser" },
    circumference:   { unit: "mm", desc: "Spulenumfang (π·d)" },
    midPulleyToWall: { unit: "mm", desc: "Geometrie: Bezugspunkt → Wand" },
    homedStepOffsetMM:{ unit:"mm", desc: "Offset nach Homing zur Startposition" },
    homedStepsOffset:{ unit:"steps", desc: "Alt/inkonsistent. Wenn Firmware das nicht hat: entfernen." },
    mass_bot:        { unit: "kg", desc: "Masse des Bots (für Kraft/Torque-Modell)" },
    g_constant:      { unit: "m/s²", desc: "Erdbeschleunigung (normal 9.81)" },
    d_t:             { unit: "mm", desc: "Geometrieparameter d_t" },
    d_p:             { unit: "mm", desc: "Geometrieparameter d_p" },
    d_m:             { unit: "mm", desc: "Geometrieparameter d_m" },
    belt_elongation_coefficient: { unit: "1/N", desc: "Riemendehnung-Koeffizient" },
    HOME_Y_OFFSET_MM:{ unit: "mm", desc: "Y-Offset beim Homing in sicheren Bereich" },
    safeYFraction:   { unit: "*10 = %", desc: "Sicherheitsanteil Höhe (Safe-Zone)" },
    safeXFraction:   { unit: "*10 = %", desc: "Sicherheitsanteil Breite (Safe-Zone)" },
    LEFT_STEP_PIN:   { unit: "GPIO", desc: "STEP Pin linker Treiber" },
    LEFT_DIR_PIN:    { unit: "GPIO", desc: "DIR Pin linker Treiber" },
    LEFT_ENABLE_PIN: { unit: "GPIO", desc: "ENABLE Pin linker Treiber" },
    RIGHT_STEP_PIN:  { unit: "GPIO", desc: "STEP Pin rechter Treiber" },
    RIGHT_DIR_PIN:   { unit: "GPIO", desc: "DIR Pin rechter Treiber" },
    RIGHT_ENABLE_PIN:{ unit: "GPIO", desc: "ENABLE Pin rechter Treiber" },
    RIGHT_MS0_PIN:   { unit: "GPIO", desc: "Microstep Pin MS0 rechts" },
    RIGHT_MS1_PIN:   { unit: "GPIO", desc: "Microstep Pin MS1 rechts" },
    RIGHT_MS2_PIN:   { unit: "GPIO", desc: "Microstep Pin MS2 rechts" },
  };

  function metaUnit(key) { return DIAG_META[key]?.unit ?? "–"; }
  function metaDesc(key) { return DIAG_META[key]?.desc ?? ""; }

  tbody.innerHTML = order.map((k) => {
    const v = (diag && Object.prototype.hasOwnProperty.call(diag, k)) ? diag[k] : "–";
    return `
      <tr>
        <td><code>${escapeHtml(k)}</code></td>
        <td><b>${escapeHtml(String(v))}</b></td>
        <td class="text-muted">${escapeHtml(String(metaUnit(k)))}</td>
        <td class="text-muted small">${escapeHtml(String(metaDesc(k)))}</td>
      </tr>
    `;
  }).join("");
}

async function refreshHelpDiagnostics() {
  try {
    const res = await fetch("/diag", { cache: "no-store" });
    if (!res.ok) throw new Error("diag");
    const diag = await res.json();
    renderHelpDiagTable(diag);
  } catch {
    renderHelpDiagTable(null);
  }
}

function bindHelpModalDiagnostics() {
  const helpModalEl = document.getElementById("helpModal");
  if (!helpModalEl) return;

  helpModalEl.addEventListener("shown.bs.modal", () => {
    refreshHelpDiagnostics();
  });
}

function setServoToolActive(which) {
  const parkBtn = document.getElementById("parkServoTool");
  const homeBtn = document.getElementById("homeServoTool");
  if (!parkBtn || !homeBtn) return;

  const parkActive = (which === "park");

  parkBtn.classList.toggle("btn-primary", parkActive);
  parkBtn.classList.toggle("btn-outline-primary", !parkActive);

  homeBtn.classList.toggle("btn-primary", !parkActive);
  homeBtn.classList.toggle("btn-outline-primary", parkActive);

  parkBtn.setAttribute("aria-pressed", parkActive ? "true" : "false");
  homeBtn.setAttribute("aria-pressed", !parkActive ? "true" : "false");
}

function bindServoToolButtons() {
  const parkBtn = document.getElementById("parkServoTool");
  const homeBtn = document.getElementById("homeServoTool");
  if (!parkBtn || !homeBtn) return;

  setServoToolActive("park");
  const SERVO_PARK_ANGLE = 80;
  const SERVO_HOME_ANGLE = 0;

  function sendServoAngle(angle) {
    $.post("/setServo", { angle: angle })
      .fail(function () {
        alert("Servo setzen fehlgeschlagen");
      });
  }

  parkBtn.onclick = (e) => {
    e.preventDefault();
    setServoToolActive("park");
    sendServoAngle(SERVO_PARK_ANGLE);
  };

  homeBtn.onclick = (e) => {
    e.preventDefault();
    setServoToolActive("home");
    sendServoAngle(SERVO_HOME_ANGLE);
  };
}


// ===== File Manager (LittleFS/SD) =====
const fmState = {
  vol: "lfs", // "lfs" | "sd"
  path: "/",
  clip: null // { mode: "copy"|"cut", vol, path }
};

// Dateimanager: Ordner-Auswahlmodus (für SVG-Serie)
let fmFolderPickCb = null;

function fm_enterFolderPickMode(cb) {
  fmFolderPickCb = typeof cb === "function" ? cb : null;
  const btn = document.getElementById("fmUseFolderForBatch");
  if (btn) {
    btn.style.display = "";
    btn.onclick = async () => {
      try {
        if (fmFolderPickCb) fmFolderPickCb(fmState.path || "/");
      } finally {
        fm_exitFolderPickMode();
        const modalEl = document.getElementById("fileManagerModal");
        if (modalEl) {
          const inst = bootstrap.Modal.getInstance(modalEl);
          if (inst) inst.hide();
        }
      }
    };
  }
}

function fm_exitFolderPickMode() {
  fmFolderPickCb = null;
  const btn = document.getElementById("fmUseFolderForBatch");
  if (btn) {
    btn.style.display = "none";
    btn.onclick = null;
  }
}

function fm_normPath(p) {
  p = String(p || "/");
  if (!p.startsWith("/")) p = "/" + p;
  while (p.includes("//")) p = p.replaceAll("//", "/");
  return p;
}

function fm_encName(name) {
  return encodeURIComponent(String(name || ""));
}

function fm_decName(nameEnc) {
  try { return decodeURIComponent(String(nameEnc || "")); } catch { return String(nameEnc || ""); }
}

function fm_setClipText() {
  const el = document.getElementById("fmClip");
  const paste = document.getElementById("fmPaste");
  if (!el || !paste) return;
  if (!fmState.clip) {
    el.textContent = "—";
    paste.disabled = true;
    return;
  }
  el.textContent = `${fmState.clip.mode.toUpperCase()}: ${fmState.clip.path}`;
  paste.disabled = false;
}

function fm_join(dir, name) {
  if (!dir.endsWith("/")) dir += "/";
  let p = dir + name;
  // normalize
  p = p.replaceAll("//", "/");
  return p;
}

function fm_dirUp(path) {
  if (path === "/") return "/";
  const parts = path.split("/").filter(Boolean);
  parts.pop();
  return "/" + parts.join("/") + (parts.length ? "/" : "");
}

async function fm_fetchList() {
  const path = fmState.path || "/";
  const url = `/fs/list?vol=${encodeURIComponent(fmState.vol)}&path=${encodeURIComponent(path)}`;

  // 1) first try
  let res;
  try {
    res = await fetch(url, { cache: "no-store" });
  } catch (e) {
    throw new Error("FS list Netzwerkfehler");
  }

  // 2) on SD problems: try one remount + retry
  if (!res.ok && fmState.vol === "sd") {
    try {
      await fetch("/sd/remount", { method: "POST" });
    } catch {}
    await new Promise(r => setTimeout(r, 350));
    try {
      res = await fetch(url, { cache: "no-store" });
    } catch (e) {
      throw new Error("FS list Netzwerkfehler");
    }
  }

  if (!res.ok) {
    let msg = `FS list HTTP ${res.status}`;
    try {
      const ct = res.headers.get("content-type") || "";
      if (ct.includes("application/json")) {
        const j = await res.json();
        msg = j?.error ? String(j.error) : msg;
      } else {
        const t = await res.text();
        if (t && t.length < 160) msg = t;
      }
    } catch {}
    throw new Error(msg);
  }

  return await res.json();
}

function fm_setVolButtons() {
  const btnLfs = document.getElementById("fmVolLfs");
  const btnSd  = document.getElementById("fmVolSd");
  if (btnLfs) {
    btnLfs.classList.toggle("btn-outline-light", fmState.vol !== "lfs");
    btnLfs.classList.toggle("btn-primary", fmState.vol === "lfs");
  }
  if (btnSd) {
    btnSd.classList.toggle("btn-outline-light", fmState.vol !== "sd");
    btnSd.classList.toggle("btn-primary", fmState.vol === "sd");
  }
}

function fm_setPathText() {
  const p = document.getElementById("fmPath");
  if (p) p.textContent = fmState.path || "/";
}

function fm_baseName(p) {
  let s = String(p || "");
  if (s.endsWith("/")) s = s.slice(0, -1);
  const parts = s.split("/").filter(Boolean);
  return parts.length ? parts[parts.length - 1] : (s || "/");
}

function fm_fmtSize(n) {
  const v = Number(n) || 0;
  if (!v) return "";
  if (v < 1024) return `${v} B`;
  if (v < 1024*1024) return `${(v/1024).toFixed(1)} KB`;
  return `${(v/1024/1024).toFixed(2)} MB`;
}

function fm_showMsg(level, title, subtitle) {
  if (window.addMessage) window.addMessage(level, title, subtitle || "");
}


function fm_renderList(entries) {
  const list = document.getElementById("fmList");
  if (!list) return;

  list.innerHTML = "";

  // Sort: dirs first, then name
  const items = [...entries].sort((a, b) => {
    const ad = !!a.dir;
    const bd = !!b.dir;
    if (ad !== bd) return ad ? -1 : 1;
    return String(a.name).localeCompare(String(b.name), undefined, { numeric: true });
  });

  async function fm_deleteEntry(vol, path, isDir) {
    const base = fm_baseName(path);
    const msg = isDir
      ? `Ordner wirklich löschen?\n\n${base}\n\nAchtung: Inhalt wird mitgelöscht.`
      : `Datei wirklich löschen?\n\n${base}`;
    if (!confirm(msg)) return;

    const form = new URLSearchParams();
    // Firmware akzeptiert jetzt beides (target/vol), wir senden beides:
    form.set("vol", vol === "lfs" ? "littlefs" : vol);
    form.set("target", vol === "lfs" ? "littlefs" : vol);
    form.set("path", path);
    if (isDir) form.set("recursive", "1");

    const res = await fetch("/fs/delete", { method: "POST", body: form });
    let j = null; try { j = await res.json(); } catch {}
    if (!res.ok) throw new Error(j?.error || ("HTTP " + res.status));

    if (window.addMessage) window.addMessage(0, "Dateimanager", `${base} gelöscht`);
    await fm_refresh();
  }

  for (const e of items) {
    const row = document.createElement("div");
    row.className = "btn btn-outline-light w-100 text-start d-flex align-items-center gap-2 mb-1";
    row.style.cursor = "pointer";

    const icon = document.createElement("i");
    icon.className = e.dir ? "bi bi-folder2" : "bi bi-file-earmark";

    const nameSpan = document.createElement("span");
    nameSpan.className = "flex-grow-1";
    nameSpan.textContent = fm_baseName(e.name);

    const sizeSpan = document.createElement("span");
    sizeSpan.className = "small text-muted";
    sizeSpan.textContent = e.dir ? "" : fm_fmtSize(e.size);

    // NEU: Delete Button
    const delBtn = document.createElement("button");
    delBtn.type = "button";
    delBtn.className = "btn btn-sm btn-outline-danger";
    delBtn.innerHTML = `<i class="bi bi-trash"></i>`;
    delBtn.title = e.dir ? "Ordner löschen" : "Datei löschen";

    delBtn.addEventListener("click", async (ev) => {
      ev.preventDefault();
      ev.stopPropagation();
      try {
        await fm_deleteEntry(fmState.vol, String(e.name), !!e.dir);
      } catch (err) {
        console.error(err);
        if (window.addMessage) window.addMessage(2, "Dateimanager", String(err?.message || err));
        alert("Löschen fehlgeschlagen: " + String(err?.message || err));
      }
    });

    row.appendChild(icon);
    row.appendChild(nameSpan);
    row.appendChild(sizeSpan);
    row.appendChild(delBtn);

    row.addEventListener("click", async () => {
      try {
        if (e.dir) {
          let p = String(e.name || "/");
          if (!p.endsWith("/")) p += "/";
          fmState.path = fm_normPath(p);
          await fm_refresh();
          return;
        }

        // Datei: Vorschau
        fmOpenFile = { vol: fmState.vol, path: String(e.name), name: fm_baseName(e.name) };
        const wrap = document.getElementById("fmPreviewWrap");
        const txt = document.getElementById("fmTextEditor");
        const img = document.getElementById("fmImgPrev");
        const bar = document.getElementById("fmEditorBar");
        if (wrap) wrap.style.display = "";
        if (bar) bar.style.display = "";

        const lower = fmOpenFile.name.toLowerCase();
        const isImg = lower.endsWith(".png") || lower.endsWith(".jpg") || lower.endsWith(".jpeg") || lower.endsWith(".webp") || lower.endsWith(".svg");

        if (isImg) {
          if (img) {
            img.style.display = "";
            img.src = `/fs/download?vol=${encodeURIComponent(fmState.vol)}&path=${encodeURIComponent(fmOpenFile.path)}`;
          }
          if (txt) txt.style.display = "none";
        } else {
          if (img) img.style.display = "none";
          if (txt) {
            txt.style.display = "";
            const res = await fetch(`/fs/read?vol=${encodeURIComponent(fmState.vol)}&path=${encodeURIComponent(fmOpenFile.path)}`, { cache: "no-store" });
            const j = res.ok ? await res.json() : null;
            txt.value = (j && j.content) ? j.content : "(kann nicht gelesen werden)";
          }
        }

        const dl = document.getElementById("fmDownloadBtn");
        if (dl) {
          dl.onclick = () => {
            window.open(`/fs/download?vol=${encodeURIComponent(fmState.vol)}&path=${encodeURIComponent(fmOpenFile.path)}`, "_blank");
          };
        }

        const sv = document.getElementById("fmSaveBtn");
        if (sv) sv.onclick = () => alert("Speichern ist hier aktuell nicht aktiv (Firmware: /fs/write fehlt)");

      } catch (err) {
        console.error(err);
        fm_showMsg(2, "Dateimanager", String(err?.message || err));
      }
    });

    list.appendChild(row);
  }
}
  




async function fm_refresh() {
  try {
    fm_setVolButtons();
    fm_setPathText();

    const list = document.getElementById("fmList");
    if (list) list.innerHTML = "<div class=\"small text-muted\">Lade…</div>";

    const data = await fm_fetchList();
    const entries = Array.isArray(data.entries) ? data.entries : [];
    fm_renderList(entries);
    fm_setPathText();
    fm_setVolButtons();
  } catch (e) {
    console.error(e);
    const list = document.getElementById("fmList");
    if (list) list.innerHTML = `<div class=\"small text-danger\">Fehler: ${escapeHtml(String(e?.message || e))}</div>`;
    fm_showMsg(2, "Dateimanager", String(e?.message || e));
  }
}







function initFileManager() {
  const btnLfs = document.getElementById('fmVolLfs');
  const btnSd  = document.getElementById('fmVolSd');
  const btnUp  = document.getElementById('fmUp');
  const btnR   = document.getElementById('fmRefresh');
  const btnMk  = document.getElementById('fmMkdir');
  const btnUpL = document.getElementById('fmUploadBtn');
  const inpUp  = document.getElementById('fmUploadInput');
  const btnPaste = document.getElementById('fmPaste');

  if (btnLfs) btnLfs.addEventListener('click', async () => { fmState.vol='lfs'; await fm_refresh(); });
  if (btnSd)  btnSd.addEventListener('click', async () => { fmState.vol='sd'; await fm_refresh(); });

  if (btnUp) btnUp.addEventListener('click', async () => { fmState.path = fm_normPath(fm_dirUp(fmState.path)); await fm_refresh(); });
  if (btnR)  btnR.addEventListener('click', fm_refresh);

  if (btnMk) btnMk.addEventListener('click', async () => {
    const name = prompt('Ordnername', 'neu');
    if (!name) return;
    const path = fm_normPath(fm_join(fmState.path, name));
    const form = new URLSearchParams();
    form.set('vol', fmState.vol);
    form.set('path', path);
    await fetch('/fs/mkdir', { method: 'POST', body: form });
    await fm_refresh();
  });

  if (btnUpL && inpUp) {
    btnUpL.addEventListener('click', () => inpUp.click());
    inpUp.addEventListener('change', async () => {
      const f = inpUp.files && inpUp.files[0];
      if (!f) return;
      const path = fm_normPath(fm_join(fmState.path, f.name));

      const formData = new FormData();
      formData.append('vol', fmState.vol);
      formData.append('path', path);
      formData.append('file', f, f.name);

      await fetch('/fs/upload', { method: 'POST', body: formData });
      inpUp.value = '';
      await fm_refresh();
    });
  }

  if (btnPaste) btnPaste.addEventListener('click', async () => {
    if (!fmState.clip) return;
    // nur gleicher vol (sonst komplex)
    if (fmState.clip.vol !== fmState.vol) {
      alert('Kopieren zwischen LittleFS und SD ist hier nicht aktiviert.');
      return;
    }
    const baseName = fmState.clip.path.split('/').filter(Boolean).slice(-1)[0] || 'file';
    const to = fm_normPath(fm_join(fmState.path, baseName));

    const form = new URLSearchParams();
    form.set('vol', fmState.vol);
    form.set('from', fmState.clip.path);
    form.set('to', to);

    if (fmState.clip.mode === 'copy') {
      await fetch('/fs/copy', { method: 'POST', body: form });
    } else {
      await fetch('/fs/move', { method: 'POST', body: form });
      fmState.clip = null;
      fm_setClipText();
    }

    await fm_refresh();
  });

  fm_setClipText();

  const modalEl = document.getElementById('fileManagerModal');
  if (modalEl) {
    modalEl.addEventListener('shown.bs.modal', () => {
      fm_refresh();
    });

    // Wenn Modal geschlossen wird, Auswahlmodus sauber beenden
    modalEl.addEventListener('hidden.bs.modal', () => {
      fm_exitFolderPickMode();
    });
  }

  const closeBtn = document.getElementById("fmPreviewClose");
  if (closeBtn) closeBtn.addEventListener("click", () => {
    fmOpenFile = null;
    const wrap = document.getElementById("fmPreviewWrap");
    if (wrap) wrap.style.display = "none";
  });


}


// -----------------------------
// Pulse Width Settings (STEP pulse width in µs)
// -----------------------------

async function loadPulseWidths() {
  try {
    const res = await fetch("/pulseWidths");
    if (!res.ok) throw new Error("pulseWidths HTTP " + res.status);

    const data = await res.json();

    const left = document.getElementById("pulseWidthLeft");
    const right = document.getElementById("pulseWidthRight");

    if (left) left.value = data.leftUs ?? "";
    if (right) right.value = data.rightUs ?? "";

    addMessage("info", "Pulsbreite geladen: L=" + data.leftUs + "µs R=" + data.rightUs + "µs");
  } catch (e) {
    console.error(e);
    addMessage("error", "Fehler beim Laden der Pulsbreite: " + e.message);
  }
}

async function savePulseWidths() {
  try {
    const leftEl = document.getElementById("pulseWidthLeft");
    const rightEl = document.getElementById("pulseWidthRight");

    if (!leftEl || !rightEl) {
      addMessage("error", "UI-Elemente für Pulsbreite fehlen.");
      return;
    }

    const leftUs = parseInt(leftEl.value, 10);
    const rightUs = parseInt(rightEl.value, 10);

    if (!Number.isFinite(leftUs) || !Number.isFinite(rightUs)) {
      addMessage("warn", "Bitte gültige Pulsbreiten eingeben (Zahlen).");
      return;
    }

    if (leftUs < 1 || leftUs > 50 || rightUs < 1 || rightUs > 50) {
      addMessage("warn", "Pulsbreite muss zwischen 1 und 50 µs liegen.");
      return;
    }

    const payload = { leftUs, rightUs };

    const res = await fetch("/setPulseWidths", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(payload),
    });

    if (!res.ok) throw new Error("setPulseWidths HTTP " + res.status);

    const out = await res.json();
    addMessage("info", "Pulsbreite gespeichert: L=" + out.leftUs + "µs R=" + out.rightUs + "µs");
  } catch (e) {
    console.error(e);
    addMessage("error", "Fehler beim Speichern der Pulsbreite: " + e.message);
  }
}

// Hook Buttons (call in init after DOM is ready)
function initPulseWidthSettingsUI() {
  const btnLoad = document.getElementById("btnLoadPulseWidths");
  const btnSave = document.getElementById("btnSavePulseWidths");

  if (btnLoad) btnLoad.addEventListener("click", loadPulseWidths);
  if (btnSave) btnSave.addEventListener("click", savePulseWidths);

  // Auto-load once at startup
  loadPulseWidths();
}

// ... dein bisheriger Code

(function initGrafButton() {
  const btn = document.getElementById('grafOpenSide');
  if (!btn) return;
  btn.addEventListener('click', () => {
    window.location.href = '/graf/index.html';
  });
})();


  // ===== Planner UI (Gear menu) =====
  function clamp(v, a, b){ return Math.max(a, Math.min(b, v)); }
  function lerp(a,b,t){ return a + (b-a)*t; }
  function lerpInt(a,b,t){ return Math.round(lerp(a,b,t)); }

  function plannerProfileFromMaster(master){
    const t = clamp(master/100.0, 0.0, 1.0);

    // 0 = fast, 1 = quality
    const fast = {
      junctionDeviation: 0.15,
      lookaheadSegments: 16,
      minSegmentTimeMs: 0,
      cornerSlowdown: 0.20,
      minCornerFactor: 0.55,
      minSegmentLenMM: 0.50,
      collinearDeg: 6.0,
      backlashXmm: 0.0,
      backlashYmm: 0.0,
      sCurveFactor: 0.10
    };

    const qual = {
      junctionDeviation: 0.01,
      lookaheadSegments: 96,
      minSegmentTimeMs: 6,
      cornerSlowdown: 0.65,
      minCornerFactor: 0.20,
      minSegmentLenMM: 0.10,
      collinearDeg: 1.5,
      backlashXmm: 0.0,
      backlashYmm: 0.0,
      sCurveFactor: 0.75
    };

    return {
      junctionDeviation: parseFloat(lerp(fast.junctionDeviation, qual.junctionDeviation, t).toFixed(3)),
      lookaheadSegments: lerpInt(fast.lookaheadSegments, qual.lookaheadSegments, t),
      minSegmentTimeMs: lerpInt(fast.minSegmentTimeMs, qual.minSegmentTimeMs, t),
      cornerSlowdown: parseFloat(lerp(fast.cornerSlowdown, qual.cornerSlowdown, t).toFixed(2)),
      minCornerFactor: parseFloat(lerp(fast.minCornerFactor, qual.minCornerFactor, t).toFixed(2)),
      minSegmentLenMM: parseFloat(lerp(fast.minSegmentLenMM, qual.minSegmentLenMM, t).toFixed(2)),
      collinearDeg: parseFloat(lerp(fast.collinearDeg, qual.collinearDeg, t).toFixed(1)),
      backlashXmm: 0.0,
      backlashYmm: 0.0,
      sCurveFactor: parseFloat(lerp(fast.sCurveFactor, qual.sCurveFactor, t).toFixed(2)),
    };
  }

  function plannerSetInputs(p){
    $("#pl_jd").val(p.junctionDeviation);
    $("#pl_look").val(p.lookaheadSegments);
    $("#pl_minms").val(p.minSegmentTimeMs);
    $("#pl_cslow").val(p.cornerSlowdown);
    $("#pl_mincf").val(p.minCornerFactor);
    $("#pl_minlen").val(p.minSegmentLenMM);
    $("#pl_coldeg").val(p.collinearDeg);
    $("#pl_blx").val(p.backlashXmm);
    $("#pl_bly").val(p.backlashYmm);
    $("#pl_scurve").val(p.sCurveFactor);
  }

  async function plannerLoadFromDevice(){
    $("#plannerStatusText").text("Lade...");
    try{
      const r = await fetch("/status");
      const j = await r.json();
      const p = (j && j.planner) ? j.planner : null;
      if (!p) { $("#plannerStatusText").text("Keine Planner-Daten"); return; }
      plannerSetInputs({
        junctionDeviation: p.junctionDeviation,
        lookaheadSegments: p.lookaheadSegments,
        minSegmentTimeMs: p.minSegmentTimeMs,
        cornerSlowdown: p.cornerSlowdown,
        minCornerFactor: p.minCornerFactor,
        minSegmentLenMM: p.minSegmentLenMM,
        collinearDeg: p.collinearDeg,
        backlashXmm: p.backlashXmm,
        backlashYmm: p.backlashYmm,
        sCurveFactor: p.sCurveFactor
      });
      $("#plannerStatusText").text("OK");
    }catch(e){
      $("#plannerStatusText").text("Fehler");
    }
  }

  async function plannerSaveToDevice(){
    const p = {
      junctionDeviation: parseFloat($("#pl_jd").val()),
      lookaheadSegments: parseInt($("#pl_look").val(), 10),
      minSegmentTimeMs: parseInt($("#pl_minms").val(), 10),
      cornerSlowdown: parseFloat($("#pl_cslow").val()),
      minCornerFactor: parseFloat($("#pl_mincf").val()),
      minSegmentLenMM: parseFloat($("#pl_minlen").val()),
      collinearDeg: parseFloat($("#pl_coldeg").val()),
      backlashXmm: parseFloat($("#pl_blx").val()),
      backlashYmm: parseFloat($("#pl_bly").val()),
      sCurveFactor: parseFloat($("#pl_scurve").val())
    };

    $("#plannerStatusText").text("Speichere...");
    try{
      await $.post("/setPlannerConfig", p);
      $("#plannerStatusText").text("Gespeichert");
    }catch(e){
      $("#plannerStatusText").text("Fehler beim Speichern");
    }
  }

  // larger slider usable range: show value live
  $("#plannerMaster").on("input change", function(){
    const v = parseInt($(this).val(), 10) || 0;
    $("#plannerMasterValue").text(v);
    const prof = plannerProfileFromMaster(v);
    plannerSetInputs(prof);
  });

  $("#plannerLoadBtn").click(function(){ plannerLoadFromDevice(); });
  $("#plannerSaveBtn").click(function(){ plannerSaveToDevice(); });

  // load planner when tools modal opens
  toolsModal.addEventListener('shown.bs.modal', function () {
    if ($("#toolsSecPlanner").length) {
      plannerLoadFromDevice();
      const v = parseInt($("#plannerMaster").val(),10) || 50;
      $("#plannerMasterValue").text(v);
    }
  });
