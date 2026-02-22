// svgSplitToTemp.js
// English comments with eastern-european technical style, as requested.
//
// Purpose:
// - Take one SVG string
// - Split by color groups (tolerant mapping to marker palette)
// - Produce per-color SVG strings
// - Provide helper for SD:/temp export via existing /fs/* endpoints (vol=sd)
//
// This file is ES module.

function clamp01(v) { return Math.max(0, Math.min(1, v)); }

function parseCssColorToRgb(c) {
  if (!c) return null;
  c = String(c).trim();
  if (!c || c === "none") return null;

  // hex
  if (c[0] === "#") {
    let h = c.slice(1);
    if (h.length === 3) h = h.split("").map(ch => ch + ch).join("");
    if (h.length !== 6) return null;
    const r = parseInt(h.slice(0,2), 16);
    const g = parseInt(h.slice(2,4), 16);
    const b = parseInt(h.slice(4,6), 16);
    return { r, g, b };
  }

  // rgb/rgba
  const m = c.match(/^rgba?\(([^)]+)\)$/i);
  if (m) {
    const parts = m[1].split(",").map(s => s.trim());
    if (parts.length < 3) return null;
    const r = Math.round(parseFloat(parts[0]));
    const g = Math.round(parseFloat(parts[1]));
    const b = Math.round(parseFloat(parts[2]));
    return { r, g, b };
  }

  // named colors minimal
  const named = {
    black: {r:0,g:0,b:0},
    white: {r:255,g:255,b:255},
    red: {r:255,g:0,b:0},
    green: {r:0,g:128,b:0},
    blue: {r:0,g:0,b:255},
    yellow: {r:255,g:255,b:0},
    orange: {r:255,g:165,b:0},
    cyan: {r:0,g:255,b:255},
    magenta: {r:255,g:0,b:255},
    purple: {r:128,g:0,b:128},
    brown: {r:165,g:42,b:42},
    gray: {r:128,g:128,b:128},
    grey: {r:128,g:128,b:128}
  };
  const lc = c.toLowerCase();
  if (named[lc]) return named[lc];

  return null;
}

function rgbToHsl(r, g, b) {
  r /= 255; g /= 255; b /= 255;
  const max = Math.max(r,g,b), min = Math.min(r,g,b);
  let h = 0, s = 0;
  const l = (max + min) / 2;
  const d = max - min;
  if (d !== 0) {
    s = d / (1 - Math.abs(2*l - 1));
    switch (max) {
      case r: h = 60 * (((g - b) / d) % 6); break;
      case g: h = 60 * (((b - r) / d) + 2); break;
      case b: h = 60 * (((r - g) / d) + 4); break;
    }
  }
  if (h < 0) h += 360;
  return { h, s, l };
}

function relLuminance(r,g,b) {
  // sRGB relative luminance
  const srgb = [r,g,b].map(v => {
    v /= 255;
    return v <= 0.04045 ? v/12.92 : Math.pow((v+0.055)/1.055, 2.4);
  });
  return 0.2126*srgb[0] + 0.7152*srgb[1] + 0.0722*srgb[2];
}

export const MARKER_LABELS = [
  "Gelb",
  "Orange",
  "Rot",
  "Türkis",
  "Blau",
  "Violett",
  "Hellgrün",
  "Dunkelgrün",
  "Braun",
  "Schwarz"
];

function classifyMarker(rgb) {
  if (!rgb) return null;
  const {r,g,b} = rgb;
  const lum = relLuminance(r,g,b);
  const hsl = rgbToHsl(r,g,b);

  // grayscale -> black
  if (hsl.s < 0.12) {
    return { label: "Schwarz", lum };
  }

  // brown: warm hue and darker
  if (hsl.h >= 12 && hsl.h <= 45 && hsl.l < 0.45 && hsl.s > 0.20) {
    return { label: "Braun", lum };
  }

  // hue buckets
  const h = hsl.h;
  if (h >= 45 && h <= 70) return { label: "Gelb", lum };
  if (h > 20 && h < 45) return { label: "Orange", lum };

  if (h <= 20 || h >= 340) return { label: "Rot", lum };

  if (h >= 70 && h < 170) {
    // green split by brightness
    return { label: (hsl.l >= 0.52 ? "Hellgrün" : "Dunkelgrün"), lum };
  }

  if (h >= 170 && h < 210) return { label: "Türkis", lum };
  if (h >= 210 && h < 260) return { label: "Blau", lum };
  if (h >= 260 && h < 330) return { label: "Violett", lum };

  return { label: "Rot", lum };
}

function getEffectiveColorForElement(el) {
  // prefer fill, then stroke
  const fill = el.getAttribute("fill");
  const stroke = el.getAttribute("stroke");

  // style attribute
  const style = el.getAttribute("style");
  let styleFill = null, styleStroke = null;
  if (style) {
    const parts = style.split(";").map(s => s.trim()).filter(Boolean);
    for (const p of parts) {
      const [k,v] = p.split(":").map(s => s.trim());
      if (!k || !v) continue;
      if (k === "fill") styleFill = v;
      if (k === "stroke") styleStroke = v;
    }
  }

  const c = (fill && fill !== "none") ? fill
          : (styleFill && styleFill !== "none") ? styleFill
          : (stroke && stroke !== "none") ? stroke
          : (styleStroke && styleStroke !== "none") ? styleStroke
          : null;

  return parseCssColorToRgb(c);
}

function serializeSvg(doc) {
  const s = new XMLSerializer();
  return s.serializeToString(doc.documentElement);
}

function cloneSvgShell(srcSvgEl) {
  const doc = document.implementation.createDocument("http://www.w3.org/2000/svg", "svg", null);
  const svg = doc.documentElement;

  // copy common attrs
  const attrs = ["viewBox","width","height","xmlns","xmlns:xlink","preserveAspectRatio"];
  for (const a of attrs) {
    const v = srcSvgEl.getAttribute(a);
    if (v) svg.setAttribute(a, v);
  }
  // ensure xmlns
  if (!svg.getAttribute("xmlns")) svg.setAttribute("xmlns","http://www.w3.org/2000/svg");
  return doc;
}

export function splitSvgIntoMarkerLayers(svgString) {
  const parser = new DOMParser();
  const doc = parser.parseFromString(svgString, "image/svg+xml");
  const srcSvg = doc.documentElement;
  if (!srcSvg || srcSvg.nodeName.toLowerCase() !== "svg") throw new Error("Bad SVG");

  // gather drawable elements
  const all = Array.from(srcSvg.querySelectorAll("*")).filter(el => {
    const n = el.nodeName.toLowerCase();
    if (n === "defs" || n === "style" || n === "metadata" || n === "title" || n === "desc") return false;
    return true;
  });

  const buckets = new Map(); // label -> {lumSum,count, nodes:[]}
  for (const el of all) {
    const rgb = getEffectiveColorForElement(el);
    const cls = classifyMarker(rgb);
    if (!cls) continue;
    const label = cls.label;
    if (!buckets.has(label)) buckets.set(label, { lumSum:0, count:0, nodes:[] });
    const b = buckets.get(label);
    b.lumSum += cls.lum;
    b.count += 1;
    b.nodes.push(el);
  }

  const layers = [];
  for (const [label, b] of buckets.entries()) {
    // build new doc
    const outDoc = cloneSvgShell(srcSvg);
    const outSvg = outDoc.documentElement;

    // copy defs/styles from original
    const defs = srcSvg.querySelector("defs");
    if (defs) outSvg.appendChild(outDoc.importNode(defs, true));
    const style = srcSvg.querySelector("style");
    if (style) outSvg.appendChild(outDoc.importNode(style, true));

    for (const node of b.nodes) {
      // include full subtree clone
      outSvg.appendChild(outDoc.importNode(node, true));
    }

    const avgLum = b.count ? (b.lumSum / b.count) : 0.0;
    layers.push({
      label,
      avgLum,
      svg: serializeSvg(outDoc)
    });
  }

  // order: hell -> dunkel (descending luminance)
  layers.sort((a,b) => (b.avgLum - a.avgLum));

  // stable ordering by our palette list, but preserve luminance preference
  // Keep luminance primary, then palette index
  layers.sort((a,b) => {
    const dl = (b.avgLum - a.avgLum);
    if (Math.abs(dl) > 0.02) return dl;
    return MARKER_LABELS.indexOf(a.label) - MARKER_LABELS.indexOf(b.label);
  });

  return layers;
}

function pad2(n){ return String(n).padStart(2,"0"); }

export async function exportLayersToSdTemp(layers, {tempDir="/temp"} = {}) {
  // Use existing FS API: /fs/delete, /fs/mkdir, /fs/upload
  // Note: This function assumes SD is mounted.
  // Clear dir: delete if exists, then mkdir.
  await fetch(`/fs/delete`, {
    method: "POST",
    headers: {"Content-Type":"application/x-www-form-urlencoded"},
    body: new URLSearchParams({ vol:"sd", path: tempDir })
  }).catch(()=>{});

  await fetch(`/fs/mkdir`, {
    method: "POST",
    headers: {"Content-Type":"application/x-www-form-urlencoded"},
    body: new URLSearchParams({ vol:"sd", path: tempDir })
  });

  const out = [];
  for (let i=0;i<layers.length;i++) {
    const layer = layers[i];
    const fname = `${tempDir}/${pad2(i+1)}_${layer.label}.svg`;
    const fd = new FormData();
    fd.append("vol", "sd");
    fd.append("path", fname);
    // filename param is ignored by firmware handler, but required for multipart
    fd.append("file", new Blob([layer.svg], {type:"image/svg+xml"}), "layer.svg");

    const res = await fetch(`/fs/upload`, { method:"POST", body: fd });
    if (!res.ok) throw new Error("Temp upload failed: " + res.status);
    out.push({ index:i, label: layer.label, path: fname, avgLum: layer.avgLum });
  }
  return out;
}
