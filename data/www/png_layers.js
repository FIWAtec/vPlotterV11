// data/www/png_layers.js
// High Quality PNG -> SVG for Plotter:
// - Color layer detection
// - Vector outline (potrace via existing worker)
// - Hatch fill (diagonal, smooth-ish)

function clamp(v, a, b) { return Math.max(a, Math.min(b, v)); }

export function rgbToHex(r, g, b) {
  const to = (x) => x.toString(16).padStart(2, "0");
  return `#${to(r)}${to(g)}${to(b)}`;
}

function hexToRgb(hex) {
  const m = /^#?([0-9a-f]{6})$/i.exec(hex || "");
  if (!m) return null;
  const n = parseInt(m[1], 16);
  return { r: (n >> 16) & 255, g: (n >> 8) & 255, b: n & 255 };
}

function distSqRGB(r1,g1,b1,r2,g2,b2){
  const dr=r1-r2, dg=g1-g2, db=b1-b2;
  return dr*dr + dg*dg + db*db;
}

function isNearWhite(r,g,b){ return r>252 && g>252 && b>252; }
function isNearBlack(r,g,b){ return r<45 && g<45 && b<45; }
function isGrayish(r,g,b){
  return Math.abs(r-g)<18 && Math.abs(g-b)<18 && Math.abs(r-b)<18;
}

async function decodeToBitmap(file) {
  if ("createImageBitmap" in window) return await createImageBitmap(file);

  const url = URL.createObjectURL(file);
  try {
    const img = new Image();
    img.decoding = "async";
    img.src = url;
    await new Promise((res, rej) => {
      img.onload = () => res();
      img.onerror = () => rej(new Error("PNG decode failed"));
    });
    const c = document.createElement("canvas");
    c.width = img.naturalWidth;
    c.height = img.naturalHeight;
    const ctx = c.getContext("2d", { willReadFrequently: true });
    ctx.drawImage(img, 0, 0);
    return c;
  } finally {
    URL.revokeObjectURL(url);
  }
}

function drawBitmapToCanvas(bitmap, maxSide = 1400) {
  const w0 = bitmap.width || bitmap.naturalWidth || bitmap.canvas?.width;
  const h0 = bitmap.height || bitmap.naturalHeight || bitmap.canvas?.height;

  const scale = Math.min(1, maxSide / Math.max(w0, h0));
  const w = Math.max(1, Math.round(w0 * scale));
  const h = Math.max(1, Math.round(h0 * scale));

  const canvas = document.createElement("canvas");
  canvas.width = w;
  canvas.height = h;
  const ctx = canvas.getContext("2d", { willReadFrequently: true });

  ctx.imageSmoothingEnabled = true;
  ctx.imageSmoothingQuality = "high";

  if (bitmap instanceof HTMLCanvasElement) ctx.drawImage(bitmap, 0, 0, w, h);
  else ctx.drawImage(bitmap, 0, 0, w, h);

  return { canvas, ctx, scale };
}

/**
 * Robust color clustering for layer list (anti-alias friendly)
 */
export async function analyzePngLayers(file, { tolerance = 80, minPixels = 20 } = {}) {
  const bmp = await decodeToBitmap(file);
  const { canvas, ctx } = drawBitmapToCanvas(bmp, 900);

  const w = canvas.width, h = canvas.height;
  const img = ctx.getImageData(0, 0, w, h);
  const d = img.data;

  const tol = clamp(Number(tolerance) || 80, 10, 160);
  const tolSq = tol * tol;

  // sample stride for performance
  const stride = Math.max(1, Math.round(Math.max(w, h) / 700));
  const scaleFactor = stride * stride;

  const clusters = [];
  let blackCount = 0;

  function addToCluster(r, g, b) {
    let bestIdx = -1, bestD = 1e18;
    for (let i = 0; i < clusters.length; i++) {
      const c = clusters[i];
      const ds = distSqRGB(c.r,c.g,c.b,r,g,b);
      if (ds < bestD) { bestD = ds; bestIdx = i; }
    }
    if (bestIdx >= 0 && bestD <= tolSq) {
      const c = clusters[bestIdx];
      const n = c.count + 1;
      c.r = Math.round((c.r * c.count + r) / n);
      c.g = Math.round((c.g * c.count + g) / n);
      c.b = Math.round((c.b * c.count + b) / n);
      c.count = n;
    } else {
      clusters.push({ r, g, b, count: 1 });
      if (clusters.length > 80) {
        clusters.sort((a,b)=>b.count-a.count);
        clusters.length = 80;
      }
    }
  }

  for (let y = 0; y < h; y += stride) {
    for (let x = 0; x < w; x += stride) {
      const i = (y * w + x) * 4;
      const a = d[i + 3];
      if (a < 32) continue;

      const r = d[i], g = d[i + 1], b = d[i + 2];
      if (isNearWhite(r,g,b)) continue;

      // outline bucket
      const maxc = Math.max(r,g,b);
      const minc = Math.min(r,g,b);
      const sat = maxc - minc;
      if (sat < 12 && isNearBlack(r,g,b)) { blackCount++; continue; }

      addToCluster(r,g,b);
    }
  }

  const minPx = Math.max(1, Number(minPixels) || 20);

  let layers = clusters
    .map(c => ({
      hex: rgbToHex(c.r,c.g,c.b),
      rgb: { r:c.r, g:c.g, b:c.b },
      count: c.count * scaleFactor,
      kind: "color"
    }))
    .sort((a,b)=>b.count-a.count)
    .filter(l => l.count >= minPx)
    .slice(0, 24);

  if (blackCount * scaleFactor >= minPx) {
    layers.unshift({
      hex:"#000000",
      rgb:{r:0,g:0,b:0},
      count:blackCount*scaleFactor,
      kind:"black"
    });
  }

  return layers;
}

/**
 * Build a binary mask ImageData for a specific target color.
 * Mask is black (0) where belongs to layer, white (255) elsewhere.
 */
async function buildMaskImageData(file, targetRgb, tolerance = 80, maxSide = 1600) {
  const bmp = await decodeToBitmap(file);
  const { canvas, ctx } = drawBitmapToCanvas(bmp, maxSide);

  const w = canvas.width, h = canvas.height;
  const img = ctx.getImageData(0, 0, w, h);
  const d = img.data;

  const tol = clamp(Number(tolerance) || 80, 10, 180);
  const tolSq = tol * tol;

  const out = new ImageData(w, h);
  const od = out.data;

  // black mask: belongs; white background otherwise
  for (let i = 0; i < d.length; i += 4) {
    const a = d[i + 3];
    if (a < 16) {
      od[i] = 255; od[i+1]=255; od[i+2]=255; od[i+3]=255;
      continue;
    }

    const r = d[i], g = d[i+1], b = d[i+2];

    // ignore pure white background
    if (isNearWhite(r,g,b)) {
      od[i] = 255; od[i+1]=255; od[i+2]=255; od[i+3]=255;
      continue;
    }

    let belongs = false;

    // black layer: catch near-black gray pixels too
    if (targetRgb.r === 0 && targetRgb.g === 0 && targetRgb.b === 0) {
      if ((isGrayish(r,g,b) && isNearBlack(r,g,b)) || isNearBlack(r,g,b)) belongs = true;
    } else {
      const ds = distSqRGB(r,g,b, targetRgb.r, targetRgb.g, targetRgb.b);
      if (ds <= tolSq) belongs = true;
    }

    if (belongs) {
      od[i] = 0; od[i+1]=0; od[i+2]=0; od[i+3]=255;
    } else {
      od[i] = 255; od[i+1]=255; od[i+2]=255; od[i+3]=255;
    }
  }

  return { mask: out, width: w, height: h };
}

/**
 * Use the existing worker (./worker/worker.js) to potrace-vectorize a mask.
 * Returns an SVG string.
 */
function vectorizeMaskToSvg(maskImageData, turdSize = 2) {
  return new Promise((resolve, reject) => {
    const w = new Worker(`./worker/worker.js?v=${Date.now()}`);

    w.onerror = (err) => {
      try { w.terminate(); } catch {}
      reject(err);
    };

    w.onmessage = (e) => {
      if (e?.data?.type === "vectorizer") {
        const svg = e.data.payload.svg;
        try { w.terminate(); } catch {}
        resolve(svg);
      }
    };

    w.postMessage({
      type: "vectorize",
      raster: maskImageData,
      turdSize: Number.isFinite(turdSize) ? turdSize : 2,
    });
  });
}

/**
 * Extract path 'd' strings from an SVG string.
 */
function extractPathDs(svgString) {
  const doc = new DOMParser().parseFromString(svgString, "image/svg+xml");
  const paths = Array.from(doc.querySelectorAll("path"));
  return paths.map(p => p.getAttribute("d")).filter(Boolean);
}

/**
 * Diagonal hatch fill by line-casting in rotated space.
 * This produces many stroke segments that look like proper hatch.
 */
function hatchSegmentsFromMask(maskImageData, hatchStep = 4, angleDeg = 35) {
  const w = maskImageData.width;
  const h = maskImageData.height;
  const d = maskImageData.data;

  const step = clamp(Number(hatchStep) || 4, 1, 14);
  const ang = (Number(angleDeg) || 35) * Math.PI / 180;

  const cx = w / 2;
  const cy = h / 2;

  const cosA = Math.cos(ang);
  const sinA = Math.sin(ang);

  // rotate (x',y') -> (x,y)
  function rotToXY(xp, yp) {
    const x = xp * cosA - yp * sinA + cx;
    const y = xp * sinA + yp * cosA + cy;
    return { x, y };
  }

  // bounds in rotated space: diagonal
  const diag = Math.ceil(Math.hypot(w, h));
  const half = diag / 2;

  function maskOn(x, y) {
    const xi = Math.round(x);
    const yi = Math.round(y);
    if (xi < 0 || yi < 0 || xi >= w || yi >= h) return false;
    const i = (yi * w + xi) * 4;
    // black pixel => on
    return d[i] < 128; 
  }

  const segs = [];

  // y' lines across rotated box
  for (let yp = -half; yp <= half; yp += step) {
    let inRun = false;
    let runStart = null;
    let lastPt = null;

    // sample along x' with pixel-ish step 1
    for (let xp = -half; xp <= half; xp += 1) {
      const p = rotToXY(xp, yp);
      const on = maskOn(p.x, p.y);

      if (on && !inRun) {
        inRun = true;
        runStart = { x: p.x, y: p.y };
        lastPt = { x: p.x, y: p.y };
      } else if (on && inRun) {
        lastPt = { x: p.x, y: p.y };
      } else if (!on && inRun) {
        // end run
        inRun = false;
        if (runStart && lastPt) {
          const dx = lastPt.x - runStart.x;
          const dy = lastPt.y - runStart.y;
          if ((dx*dx + dy*dy) >= 2.0) segs.push({ x1: runStart.x, y1: runStart.y, x2: lastPt.x, y2: lastPt.y });
        }
        runStart = null;
        lastPt = null;
      }
    }

    // close run at end
    if (inRun && runStart && lastPt) {
      const dx = lastPt.x - runStart.x;
      const dy = lastPt.y - runStart.y;
      if ((dx*dx + dy*dy) >= 2.0) segs.push({ x1: runStart.x, y1: runStart.y, x2: lastPt.x, y2: lastPt.y });
    }
  }

  return segs;
}

/**
 * Build "crass" SVG:
 * - For each selected color: outline from potrace paths + hatch fill from mask.
 * - Stroke-only output (plotter friendly).
 */
export async function buildCrassSvgFromPng(file, selectedHexColors, options = {}) {
  const tolerance = options.tolerance ?? 90;
  const turdSize = options.turdSize ?? 6;         // higher removes speckles
  const hatchStep = options.hatchStep ?? 4;       // smaller = denser fill
  const hatchAngle = options.hatchAngle ?? 35;    // 0..90
  const outlineStroke = options.outlineStroke ?? 1.2;
  const hatchStroke = options.hatchStroke ?? 0.8;

  const targets = selectedHexColors.map(hexToRgb).filter(Boolean);
  if (targets.length === 0) {
    return `<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" width="10" height="10" viewBox="0 0 10 10"></svg>`;
  }

  // Use the first mask to get consistent W/H
  const first = await buildMaskImageData(file, targets[0], tolerance, 1600);
  const W = first.width, H = first.height;

  const layerSvgs = [];

  for (const t of targets) {
    const { mask } = await buildMaskImageData(file, t, tolerance, 1600);

    // 1) Outline via potrace
    const vecSvg = await vectorizeMaskToSvg(mask, turdSize);
    const ds = extractPathDs(vecSvg);

    // 2) Hatch fill via line-casting
    const hatch = hatchSegmentsFromMask(mask, hatchStep, hatchAngle);

    const colorHex = rgbToHex(t.r, t.g, t.b);

    const outlinePaths = ds.map(d => (
      `<path d="${d}"
        fill="none"
        stroke="#000"
        stroke-width="${outlineStroke}"
        stroke-linejoin="round"
        stroke-linecap="round" />`
    )).join("\n");

    // chunk hatch segments to keep SVG manageable
    const chunkSize = 1800;
    const hatchPaths = [];
    for (let i = 0; i < hatch.length; i += chunkSize) {
      const chunk = hatch.slice(i, i + chunkSize);
      const dstr = chunk.map(s => `M ${s.x1.toFixed(2)} ${s.y1.toFixed(2)} L ${s.x2.toFixed(2)} ${s.y2.toFixed(2)}`).join(" ");
      hatchPaths.push(
        `<path d="${dstr}"
          fill="none"
          stroke="#000"
          stroke-width="${hatchStroke}"
          stroke-linecap="round"
          stroke-linejoin="round" />`
      );
    }

    layerSvgs.push(`
  <g data-layer="${colorHex}">
    <!-- LAYER ${colorHex} -->
    ${hatchPaths.join("\n")}
    ${outlinePaths}
  </g>`);
  }

  return `<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg"
     width="${W}" height="${H}"
     viewBox="0 0 ${W} ${H}">
${layerSvgs.join("\n")}
</svg>`;
}
