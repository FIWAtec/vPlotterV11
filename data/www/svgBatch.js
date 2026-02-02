// =====================================================
// svgBatch.js
// SVG-Serienmodus (alle SVGs im SD-Ordner nacheinander)
// Reihenfolge: numerischer Prefix 01,02,03...
// Popup-Bestätigung vor jedem Bildwechsel
// =====================================================

// interner Batch-Zustand
let svgBatchQueue = [];
let svgBatchIndex = 0;
let svgBatchActive = false;

// UI-Events (ohne alert-Blocks): HUD/Log kann darauf reagieren
function _emitBatchState() {
  try {
    const cur = (svgBatchActive && svgBatchQueue[svgBatchIndex]) ? svgBatchQueue[svgBatchIndex].name : "";
    window.dispatchEvent(new CustomEvent("svgBatch:state", {
      detail: {
        active: !!svgBatchActive,
        index: svgBatchIndex,
        total: svgBatchQueue.length,
        currentName: cur
      }
    }));
  } catch {}
}

function _emitBatchError(message) {
  try {
    window.dispatchEvent(new CustomEvent("svgBatch:error", { detail: { message: String(message || "") } }));
  } catch {}
}

function _uiInfo(msg) {
  try { if (window.addMessage) window.addMessage(0, "SVG-Serie", String(msg || "")); } catch {}
}
function _uiWarn(msg) {
  try { if (window.addMessage) window.addMessage(1, "SVG-Serie", String(msg || "")); } catch {}
}
function _uiErr(msg) {
  try { if (window.addMessage) window.addMessage(2, "SVG-Serie", String(msg || "")); } catch {}
}


// Status für andere Skripte (z.B. main.js Telemetrie)
function _publishState() {
  try {
    window.__svgBatchActive = !!svgBatchActive;
    window.__svgBatchIndex = svgBatchIndex;
    window.__svgBatchLen = svgBatchQueue.length;
  } catch {}

  _emitBatchState();
}

/**
 * Startet den SVG-Serienmodus aus dem aktuell
 * im SD-Filemanager geöffneten Ordner.
 */
async function startSvgBatchFromCurrentFolder() {
  try {
    const files = await listCurrentSdFolder();

    svgBatchQueue = files
      .filter(f =>
        f &&
        f.name &&
        typeof f.name === "string" &&
        f.name.toLowerCase().endsWith(".svg")
      )
      .sort((a, b) =>
        a.name.localeCompare(b.name, undefined, { numeric: true })
      );

    if (svgBatchQueue.length === 0) {
      _uiWarn("Keine SVG-Dateien im aktuellen Ordner gefunden");
      _emitBatchError("Keine SVG-Dateien im aktuellen Ordner gefunden");
      return;
    }

    svgBatchIndex = 0;
    svgBatchActive = true;
    _publishState();

    console.log(
      "[SVG-BATCH] Starte Serie mit",
      svgBatchQueue.length,
      "SVGs"
    );

    // erstes SVG laden (Einstellungen setzt der User wie gewohnt)
    loadSvgFromSd(svgBatchQueue[0].path);

  } catch (err) {
    console.error("[SVG-BATCH] Fehler beim Start:", err);
    _uiErr("Fehler beim Starten der SVG-Serie");
    _emitBatchError("Fehler beim Starten der SVG-Serie");
  }
}

/**
 * Muss aufgerufen werden, wenn ein SVG fertig gezeichnet wurde.
 * (z. B. wenn Status auf IDLE / finished wechselt)
 */
function onBatchDrawingFinished() {
  if (!svgBatchActive) return;

  openConfirmModal(
    "Nächstes SVG zeichnen?",
    () => {
      svgBatchIndex++;

      if (svgBatchIndex >= svgBatchQueue.length) {
        svgBatchActive = false;
        _publishState();
        _uiInfo("Alle SVGs im Ordner wurden gezeichnet");
        console.log("[SVG-BATCH] Serie abgeschlossen");
        return;
      }

      console.log(
        "[SVG-BATCH] Lade nächstes SVG:",
        svgBatchQueue[svgBatchIndex].name
      );

      loadSvgFromSd(svgBatchQueue[svgBatchIndex].path);
      _publishState();
    },
    () => {
      svgBatchActive = false;
      _publishState();
      console.log("[SVG-BATCH] Serie manuell beendet");
    }
  );
}

/**
 * Optional: Serienmodus hart abbrechen
 */
function stopSvgBatch() {
  svgBatchActive = false;
  svgBatchQueue = [];
  svgBatchIndex = 0;
  _publishState();
  console.log("[SVG-BATCH] Serie abgebrochen");
}

// initial publish
_publishState();