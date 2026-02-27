// file_manager_v2.js
// Dateimanager V2 (LittleFS/SD) + robuste SD-Fehlerbehandlung + Ordner-Auswahlmodus für SVG-Serie
// Ziel: SD-Karte "geht nicht" -> sauberer Retry + /sd/remount, ohne dass die UI mit Alerts nervt.

(function () {
  "use strict";

  // Globaler Zustand (damit svgBatch.js und andere Skripte sauber lesen können)
  const fmState = window.fmState || { vol: "lfs", path: "/", clip: null };
  window.fmState = fmState;

  // Simple overlay for SVG previews (blue frame + mm + ORG)
  let fmOverlaySvg = null;
  let fmOverlayMeta = null;

  function fmEnsureOverlay() {
    if (fmOverlaySvg) return fmOverlaySvg;
    const svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
    svg.setAttribute('xmlns', 'http://www.w3.org/2000/svg');
    svg.style.position = 'absolute';
    svg.style.left = '0px';
    svg.style.top = '0px';
    svg.style.width = '0px';
    svg.style.height = '0px';
    svg.style.pointerEvents = 'none';
    svg.style.zIndex = 999;
    document.body.appendChild(svg);
    fmOverlaySvg = svg;
    return svg;
  }

  function fmRenderOverlay(img) {
    if (!fmOverlayMeta || !img || img.style.display === 'none') {
      if (fmOverlaySvg) fmOverlaySvg.style.width = '0px';
      return;
    }
    const r = img.getBoundingClientRect();
    if (!(r.width > 5 && r.height > 5)) return;

    const svg = fmEnsureOverlay();
    svg.style.left = (window.scrollX + r.left) + 'px';
    svg.style.top = (window.scrollY + r.top) + 'px';
    svg.style.width = r.width + 'px';
    svg.style.height = r.height + 'px';
    svg.setAttribute('viewBox', `0 0 ${r.width} ${r.height}`);

    while (svg.firstChild) svg.removeChild(svg.firstChild);

    const mk = (name) => document.createElementNS('http://www.w3.org/2000/svg', name);
    const rect = mk('rect');
    rect.setAttribute('x', '1');
    rect.setAttribute('y', '1');
    rect.setAttribute('width', (r.width-2).toString());
    rect.setAttribute('height', (r.height-2).toString());
    rect.setAttribute('fill', 'none');
    rect.setAttribute('stroke', '#2f81ff');
    rect.setAttribute('stroke-width', '2');
    svg.appendChild(rect);

    let ox = 0, oy = 0;
    if (fmOverlayMeta.hasViewBox && fmOverlayMeta.vbW > 0 && fmOverlayMeta.vbH > 0) {
      ox = (-fmOverlayMeta.vbX / fmOverlayMeta.vbW) * r.width;
      oy = (-fmOverlayMeta.vbY / fmOverlayMeta.vbH) * r.height;
    }
    ox = Math.max(0, Math.min(r.width, ox));
    oy = Math.max(0, Math.min(r.height, oy));

    const l1 = mk('line');
    l1.setAttribute('x1', (ox-10).toString());
    l1.setAttribute('y1', oy.toString());
    l1.setAttribute('x2', (ox+10).toString());
    l1.setAttribute('y2', oy.toString());
    l1.setAttribute('stroke', '#2f81ff');
    l1.setAttribute('stroke-width', '2');
    svg.appendChild(l1);

    const l2 = mk('line');
    l2.setAttribute('x1', ox.toString());
    l2.setAttribute('y1', (oy-10).toString());
    l2.setAttribute('x2', ox.toString());
    l2.setAttribute('y2', (oy+10).toString());
    l2.setAttribute('stroke', '#2f81ff');
    l2.setAttribute('stroke-width', '2');
    svg.appendChild(l2);

    const text = mk('text');
    text.setAttribute('x', '8');
    text.setAttribute('y', '20');
    text.setAttribute('fill', '#2f81ff');
    text.setAttribute('font-size', '14');
    text.setAttribute('font-family', 'system-ui, -apple-system, Segoe UI, Roboto, Arial');
    text.textContent = `maxX ${(fmOverlayMeta.widthMm||0).toFixed(1)}mm  maxY ${(fmOverlayMeta.heightMm||0).toFixed(1)}mm   ORG (0,0)  X+  Y+`;
    svg.appendChild(text);
  }

  let folderPickCb = null;

  function normPath(p) {
    p = String(p || "/");
    if (!p.startsWith("/")) p = "/" + p;
    while (p.includes("//")) p = p.replaceAll("//", "/");
    return p;
  }

  function baseName(p) {
    let s = String(p || "");
    if (s.endsWith("/")) s = s.slice(0, -1);
    const parts = s.split("/").filter(Boolean);
    return parts.length ? parts[parts.length - 1] : (s || "/");
  }

  function dirUp(path) {
    path = String(path || "/");
    if (path === "/") return "/";
    const parts = path.split("/").filter(Boolean);
    parts.pop();
    return "/" + parts.join("/") + (parts.length ? "/" : "");
  }

  function joinPath(dir, name) {
    dir = String(dir || "/");
    name = String(name || "");
    if (!dir.endsWith("/")) dir += "/";
    return normPath(dir + name);
  }

  function fmtSize(n) {
    const v = Number(n) || 0;
    if (!v) return "";
    if (v < 1024) return `${v} B`;
    if (v < 1024 * 1024) return `${(v / 1024).toFixed(1)} KB`;
    return `${(v / 1024 / 1024).toFixed(2)} MB`;
  }

  function uiMsg(level, title, subtitle) {
    try {
      if (window.addMessage) window.addMessage(level, title, subtitle || "");
    } catch {}
  }

  async function sdRemountOnce() {
    try {
      const r = await fetch("/sd/remount", { method: "POST" });
      return r.ok;
    } catch {
      return false;
    }
  }

  async function fetchList(vol, path) {
    vol = String(vol || "lfs");
    path = normPath(path || "/");

    // Backend-Kompatibilität:
    // - ältere Web-UI nutzt ?vol=lfs/sd
    // - Firmware nutzt oft ?target=littlefs/sd
    const target = (vol === "lfs") ? "littlefs" : vol;

    // 1) erster Versuch
    let res = await fetch(`/fs/list?vol=${encodeURIComponent(vol)}&target=${encodeURIComponent(target)}&path=${encodeURIComponent(path)}`, { cache: "no-store" });

    // 2) wenn SD zickt -> einmal remount + retry
    if (!res.ok && vol === "sd") {
      await sdRemountOnce();
      await new Promise(r => setTimeout(r, 250));
      res = await fetch(`/fs/list?vol=${encodeURIComponent(vol)}&target=${encodeURIComponent(target)}&path=${encodeURIComponent(path)}`, { cache: "no-store" });
    }

    if (!res.ok) {
      let msg = `FS list HTTP ${res.status}`;
      try {
        const ct = res.headers.get("content-type") || "";
        if (ct.includes("application/json")) {
          const j = await res.json();
          if (j && j.error) msg = String(j.error);
        } else {
          const t = await res.text();
          if (t && t.length < 200) msg = t;
        }
      } catch {}
      throw new Error(msg);
    }

    const data = await res.json();

    // Kompatibel mit beiden Antwortformaten:
    // - { entries:[{name,dir,size}] }
    // - { items:[{name,isDir,size}] }
    const entries = Array.isArray(data.entries) ? data.entries : (Array.isArray(data.items) ? data.items : []);
    return entries.map(e => ({
      name: String(e.name || ""),
      dir: !!(e.dir ?? e.isDir),
      size: Number(e.size || 0)
    }));
  }

  async function fetchRead(vol, path) {
    vol = String(vol || "lfs");
    path = String(path || "/");
    const target = (vol === "lfs") ? "littlefs" : vol;
    const res = await fetch(`/fs/read?vol=${encodeURIComponent(vol)}&target=${encodeURIComponent(target)}&path=${encodeURIComponent(path)}`, { cache: "no-store" });
    if (!res.ok) throw new Error(`FS read HTTP ${res.status}`);

    // Firmware liefert je nach Version Text oder JSON {content:...}
    const ct = res.headers.get("content-type") || "";
    if (ct.includes("application/json")) {
      const j = await res.json();
      return (j && typeof j.content === "string") ? j.content : JSON.stringify(j, null, 2);
    }
    return await res.text();
  }

  async function fetchDelete(vol, path, recursive) {
    vol = String(vol || "lfs");
    path = String(path || "/");
    const target = (vol === "lfs") ? "littlefs" : vol;

    const form = new URLSearchParams();
    // Sende beides, damit alte/neue Firmware kompatibel bleibt
    form.set("vol", vol);
    form.set("target", target);
    form.set("path", path);
    if (recursive) form.set("recursive", "1");

    const res = await fetch("/fs/delete", { method: "POST", body: form });
    let j = null;
    try {
      const ct = res.headers.get("content-type") || "";
      if (ct.includes("application/json")) j = await res.json();
      else j = { error: await res.text() };
    } catch {
      j = null;
    }
    if (!res.ok) throw new Error(j?.error || ("HTTP " + res.status));
    return j;
  }

  function setVolButtons() {
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

  function setPathText() {
    const p = document.getElementById("fmPath");
    if (p) p.textContent = fmState.path || "/";
  }

  function setClipText() {
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

  function exitFolderPickMode() {
    folderPickCb = null;
    const btn = document.getElementById("fmUseFolderForBatch");
    if (btn) {
      btn.style.display = "none";
      btn.onclick = null;
    }
  }

  function enterFolderPickMode(cb) {
    folderPickCb = typeof cb === "function" ? cb : null;
    const btn = document.getElementById("fmUseFolderForBatch");
    if (btn) {
      btn.style.display = "";
      btn.onclick = () => {
        try { if (folderPickCb) folderPickCb(fmState.path || "/"); }
        finally { exitFolderPickMode(); }
      };
    }
  }

  // Expose for main.js / svgBatch flow
  window.fm_enterFolderPickMode = enterFolderPickMode;
  window.fm_exitFolderPickMode = exitFolderPickMode;

  function renderList(entries) {
    const list = document.getElementById("fmList");
    if (!list) return;

    list.innerHTML = "";

    const items = [...entries].sort((a, b) => {
      if (!!a.dir !== !!b.dir) return a.dir ? -1 : 1;
      return baseName(a.name).localeCompare(baseName(b.name), undefined, { numeric: true });
    });

    for (const e of items) {
      // WICHTIG: kein <button> als Row, weil wir Action-Buttons (Löschen) darin haben.
      // Verschachtelte Buttons sind HTML-Undefined-Behavior und führen dazu, dass Buttons "nicht da" sind.
      const row = document.createElement("div");
      row.className = "btn btn-outline-light w-100 text-start d-flex align-items-center gap-2 mb-1";
      row.setAttribute("role", "button");
      row.tabIndex = 0;

      const icon = document.createElement("i");
      icon.className = e.dir ? "bi bi-folder2" : "bi bi-file-earmark";

      const nameSpan = document.createElement("span");
      nameSpan.className = "flex-grow-1";
      nameSpan.textContent = baseName(e.name);

      const sizeSpan = document.createElement("span");
      sizeSpan.className = "small text-muted";
      sizeSpan.textContent = e.dir ? "" : fmtSize(e.size);

      // Action: Delete (Datei/Ordner)
      const delBtn = document.createElement("button");
      delBtn.type = "button";
      delBtn.className = "btn btn-sm btn-outline-danger";
      delBtn.title = e.dir ? "Ordner löschen" : "Datei löschen";
      delBtn.innerHTML = `<i class="bi bi-trash"></i>`;

      delBtn.addEventListener("click", async (ev) => {
        ev.preventDefault();
        ev.stopPropagation();

        const name = baseName(e.name);
        const isDir = !!e.dir;
        const ok = confirm(isDir
          ? `Ordner wirklich löschen?\n\n${name}\n\nAchtung: Inhalt wird mitgelöscht.`
          : `Datei wirklich löschen?\n\n${name}`
        );
        if (!ok) return;

        try {
          await fetchDelete(fmState.vol, String(e.name || ""), isDir);
          uiMsg(0, "Dateimanager", `${name} gelöscht`);
          await refresh();
        } catch (err) {
          console.error(err);
          uiMsg(2, "Dateimanager", String(err?.message || err));
          alert("Löschen fehlgeschlagen: " + String(err?.message || err));
        }
      });

      row.appendChild(icon);
      row.appendChild(nameSpan);
      row.appendChild(sizeSpan);
      row.appendChild(delBtn);

      const openEntry = async () => {
        try {
          if (e.dir) {
            let p = String(e.name || "/");
            if (!p.endsWith("/")) p += "/";
            fmState.path = normPath(p);
            await refresh();
            return;
          }

          // Datei Vorschau
          const filePath = String(e.name || "");
          const fileName = baseName(filePath);
          const wrap = document.getElementById("fmPreviewWrap");
          const txt = document.getElementById("fmTextEditor");
          const img = document.getElementById("fmImgPrev");
          const bar = document.getElementById("fmEditorBar");
          if (wrap) wrap.style.display = "";
          if (bar) bar.style.display = "";

          const lower = fileName.toLowerCase();
          const isImg = lower.endsWith(".png") || lower.endsWith(".jpg") || lower.endsWith(".jpeg") || lower.endsWith(".webp") || lower.endsWith(".svg");

          if (isImg) {
            if (img) {
              img.style.display = "";
              const t = (fmState.vol === "lfs") ? "littlefs" : fmState.vol;
              img.src = `/fs/download?vol=${encodeURIComponent(fmState.vol)}&target=${encodeURIComponent(t)}&path=${encodeURIComponent(filePath)}`;

              // SVG overlay meta (header-chunk parsing in firmware)
              if (lower.endsWith(".svg")) {
                try {
                  const src = (fmState.vol === "lfs") ? "fs" : "sd";
                  const r = await fetch(`/svgMeta?src=${encodeURIComponent(src)}&path=${encodeURIComponent(filePath)}`, { cache: "no-store" });
                  const j = await r.json();
                  fmOverlayMeta = (j && j.ok) ? j : null;
                } catch {
                  fmOverlayMeta = null;
                }

                img.addEventListener('load', () => fmRenderOverlay(img), { once: true });
                setTimeout(() => fmRenderOverlay(img), 50);
              } else {
                fmOverlayMeta = null;
                if (fmOverlaySvg) fmOverlaySvg.style.width = '0px';
              }
            }
            if (txt) txt.style.display = "none";
          } else {
            if (img) img.style.display = "none";
            if (txt) {
              txt.style.display = "";
              txt.value = await fetchRead(fmState.vol, filePath);
            }
          }

          const dl = document.getElementById("fmDownloadBtn");
          if (dl) {
            const t = (fmState.vol === "lfs") ? "littlefs" : fmState.vol;
            dl.onclick = () => window.open(`/fs/download?vol=${encodeURIComponent(fmState.vol)}&target=${encodeURIComponent(t)}&path=${encodeURIComponent(filePath)}`, "_blank");
          }

          const sv = document.getElementById("fmSaveBtn");
          if (sv) sv.onclick = () => alert("Speichern ist hier aktuell nicht aktiv (Firmware: /fs/write fehlt)");

        } catch (err) {
          console.error(err);
          uiMsg(2, "Dateimanager", String(err?.message || err));
        }
      };

      row.addEventListener("click", openEntry);
      row.addEventListener("keydown", (ev) => {
        if (ev.key === "Enter" || ev.key === " ") {
          ev.preventDefault();
          openEntry();
        }
      });

      list.appendChild(row);
    }
  }

  async function refresh() {
    try {
      setVolButtons();
      setPathText();

      const list = document.getElementById("fmList");
      if (list) list.innerHTML = `<div class="small text-muted">Lade…</div>`;

      const entries = await fetchList(fmState.vol, fmState.path);
      renderList(entries);

      setVolButtons();
      setPathText();

    } catch (e) {
      console.error(e);
      const list = document.getElementById("fmList");
      if (list) list.innerHTML = `<div class="small text-danger">Fehler: ${String(e?.message || e)}</div>`;
      uiMsg(2, "Dateimanager", String(e?.message || e));
    }
  }

  function bindUi() {
    const btnLfs = document.getElementById("fmVolLfs");
    const btnSd  = document.getElementById("fmVolSd");
    const btnUp  = document.getElementById("fmUp");
    const btnR   = document.getElementById("fmRefresh");
    const btnMk  = document.getElementById("fmMkdir");
    const btnUpL = document.getElementById("fmUploadBtn");
    const inpUp  = document.getElementById("fmUploadInput");
    const btnPaste = document.getElementById("fmPaste");

    if (btnLfs) btnLfs.addEventListener("click", async () => { fmState.vol = "lfs"; await refresh(); });
    if (btnSd)  btnSd.addEventListener("click", async () => { fmState.vol = "sd"; await refresh(); });

    if (btnUp) btnUp.addEventListener("click", async () => { fmState.path = normPath(dirUp(fmState.path)); await refresh(); });
    if (btnR)  btnR.addEventListener("click", refresh);

    if (btnMk) btnMk.addEventListener("click", async () => {
      const name = prompt("Ordnername", "neu");
      if (!name) return;
      const path = normPath(joinPath(fmState.path, name));
      const form = new URLSearchParams();
      const t = (fmState.vol === "lfs") ? "littlefs" : fmState.vol;
      form.set("vol", fmState.vol);
      form.set("target", t);
      form.set("path", path);
      await fetch("/fs/mkdir", { method: "POST", body: form });
      await refresh();
    });

    if (btnUpL && inpUp) {
      btnUpL.addEventListener("click", () => inpUp.click());
      inpUp.addEventListener("change", async () => {
        const f = inpUp.files && inpUp.files[0];
        if (!f) return;
        const path = normPath(joinPath(fmState.path, f.name));
        const t = (fmState.vol === "lfs") ? "littlefs" : fmState.vol;

        const formData = new FormData();
        // Kompatibilität: alte UI (vol/path) und Firmware (target/dir)
        formData.append("vol", fmState.vol);
        formData.append("target", t);
        formData.append("dir", fmState.path);
        formData.append("path", path);
        formData.append("file", f, f.name);

        await fetch("/fs/upload", { method: "POST", body: formData });
        inpUp.value = "";
        await refresh();
      });
    }

    if (btnPaste) btnPaste.addEventListener("click", async () => {
      if (!fmState.clip) return;

      if (fmState.clip.vol !== fmState.vol) {
        alert("Kopieren zwischen LittleFS und SD ist hier nicht aktiviert.");
        return;
      }
      const base = fmState.clip.path.split("/").filter(Boolean).slice(-1)[0] || "file";
      const to = normPath(joinPath(fmState.path, base));

      const form = new URLSearchParams();
      const t = (fmState.vol === "lfs") ? "littlefs" : fmState.vol;
      form.set("vol", fmState.vol);
      form.set("target", t);
      form.set("from", fmState.clip.path);
      form.set("to", to);

      if (fmState.clip.mode === "copy") {
        await fetch("/fs/copy", { method: "POST", body: form });
      } else {
        await fetch("/fs/move", { method: "POST", body: form });
        fmState.clip = null;
        setClipText();
      }

      await refresh();
    });

    setClipText();

    const modalEl = document.getElementById("fileManagerModal");
    if (modalEl) {
      modalEl.addEventListener("shown.bs.modal", () => refresh());
      modalEl.addEventListener("hidden.bs.modal", () => exitFolderPickMode());
    }

    const closeBtn = document.getElementById("fmPreviewClose");
    if (closeBtn) closeBtn.addEventListener("click", () => {
      const wrap = document.getElementById("fmPreviewWrap");
      if (wrap) wrap.style.display = "none";
    });
  }

  // Expose für main.js (Batch-Folder-Picker nutzt diese Funktionen, wenn V2 aktiv ist)
  window.fm_refresh = refresh;
  window.fm_normPath = normPath;

  // Public init (wird von main.js bevorzugt verwendet, wenn vorhanden)
  window.initFileManagerV2 = function initFileManagerV2() {
    try { bindUi(); } catch (e) { console.error(e); }
  };

  // Für svgBatch.js: Liste aktueller SD-Ordner (robust)
  window.listCurrentSdFolder = async function listCurrentSdFolder() {
    const path = fmState.path || "/";
    const vol = "sd";
    const entries = await fetchList(vol, path);
    return entries.map(e => {
      const p = String(e.name || "");
      return { name: baseName(p), path: p, dir: !!e.dir, size: Number(e.size || 0) };
    });
  };

  window.addEventListener('resize', () => {
    try { fmRenderOverlay(document.getElementById('fmImgPrev')); } catch {}
  });
  window.addEventListener('scroll', () => {
    try { fmRenderOverlay(document.getElementById('fmImgPrev')); } catch {}
  }, true);

})();
