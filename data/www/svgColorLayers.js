// svgColorLayers.js
// Minimal-invasive helper for multi-color SVG layer drawing.
// - Extract fill/stroke colors
// - Map any tone to marker family (red/green/blue/...) to keep UX simple
// - Build per-family layer SVGs

(function(global){
  "use strict";

  const FAMILY_ORDER_LIGHT_TO_DARK = [
    "yellow",
    "orange",
    "red",
    "purple",
    "cyan",
    "blue",
    "lightgreen",
    "darkgreen",
    "brown",
    "black"
  ];

  const FAMILY_META = {
    yellow:     { name: "Gelb",      swatch: "#FFD200" },
    orange:     { name: "Orange",    swatch: "#FF8A00" },
    red:        { name: "Rot",       swatch: "#E53935" },
    purple:     { name: "Violett",   swatch: "#7E57C2" },
    blue:       { name: "Blau",      swatch: "#1E88E5" },
    cyan:       { name: "Türkis",    swatch: "#00B8D4" },
    lightgreen: { name: "Hellgrün",  swatch: "#66BB6A" },
    darkgreen:  { name: "Dunkelgrün",swatch: "#1B5E20" },
    brown:      { name: "Braun",     swatch: "#6D4C41" },
    black:      { name: "Schwarz",   swatch: "#000000" },
  };

  function clamp(v,min,max){ return Math.max(min, Math.min(max, v)); }

  function parseColorToRgb(input){
    if (!input) return null;
    let s = String(input).trim().toLowerCase();
    if (!s || s === "none" || s === "transparent") return null;

    // style can contain 'fill: ...;'
    if (s.includes("fill:") || s.includes("stroke:")) {
      // not a color
      return null;
    }

    // hex
    if (s[0] === '#') {
      const hex = s.slice(1);
      if (hex.length === 3) {
        const r = parseInt(hex[0]+hex[0],16);
        const g = parseInt(hex[1]+hex[1],16);
        const b = parseInt(hex[2]+hex[2],16);
        if ([r,g,b].every(Number.isFinite)) return {r,g,b};
      }
      if (hex.length === 6) {
        const r = parseInt(hex.slice(0,2),16);
        const g = parseInt(hex.slice(2,4),16);
        const b = parseInt(hex.slice(4,6),16);
        if ([r,g,b].every(Number.isFinite)) return {r,g,b};
      }
      return null;
    }

    // rgb/rgba
    const m = s.match(/^rgba?\(([^)]+)\)$/);
    if (m) {
      const parts = m[1].split(',').map(x => x.trim());
      if (parts.length >= 3) {
        const r = clamp(parseFloat(parts[0]),0,255);
        const g = clamp(parseFloat(parts[1]),0,255);
        const b = clamp(parseFloat(parts[2]),0,255);
        if ([r,g,b].every(Number.isFinite)) return {r,g,b};
      }
      return null;
    }

    // basic named colors (enough for typical svgs)
    const NAMED = {
      black: "#000000",
      white: "#ffffff",
      red: "#ff0000",
      green: "#00ff00",
      blue: "#0000ff",
      yellow: "#ffff00",
      orange: "#ffa500",
      purple: "#800080",
      violet: "#ee82ee",
      cyan: "#00ffff",
      magenta: "#ff00ff",
      brown: "#a52a2a",
      gray: "#808080",
      grey: "#808080",
    };
    if (NAMED[s]) return parseColorToRgb(NAMED[s]);

    return null;
  }

  function rgbToHsv(rgb){
    const r = rgb.r/255, g = rgb.g/255, b = rgb.b/255;
    const max = Math.max(r,g,b), min = Math.min(r,g,b);
    const d = max - min;
    let h = 0;
    if (d !== 0) {
      if (max === r) h = ((g-b)/d) % 6;
      else if (max === g) h = ((b-r)/d) + 2;
      else h = ((r-g)/d) + 4;
      h *= 60;
      if (h < 0) h += 360;
    }
    const s = max === 0 ? 0 : d/max;
    const v = max;
    return {h, s, v};
  }

  function relLuma(rgb){
    // simple sRGB luma
    return 0.2126*rgb.r + 0.7152*rgb.g + 0.0722*rgb.b;
  }

  function mapRgbToFamily(rgb){
    const hsv = rgbToHsv(rgb);

    // black/near-black
    if (hsv.v <= 0.12 || relLuma(rgb) < 40) return "black";

    // brown: low-ish saturation, warm hue, mid-dark
    if (hsv.h >= 10 && hsv.h <= 50 && hsv.s >= 0.25 && hsv.v <= 0.55) return "brown";

    // grey-ish (treat as black for pen plot)
    if (hsv.s < 0.12) {
      return relLuma(rgb) < 128 ? "black" : "yellow";
    }

    // hue buckets
    const h = hsv.h;
    if (h >= 45 && h < 75) return "yellow";
    if (h >= 20 && h < 45) return "orange";
    if (h >= 345 || h < 20) return "red";
    if (h >= 285 && h < 345) return "purple";
    if (h >= 200 && h < 285) return "blue";
    if (h >= 160 && h < 200) return "cyan";
    if (h >= 75 && h < 160) {
      // green split by value
      return hsv.v >= 0.55 ? "lightgreen" : "darkgreen";
    }

    return "black";
  }

  function getStyleColor(styleText, prop){
    if (!styleText) return null;
    const s = String(styleText);
    const re = new RegExp(`${prop}\\s*:\\s*([^;]+)`, 'i');
    const m = s.match(re);
    return m ? m[1].trim() : null;
  }

  function extractFamiliesFromSvg(svgString){
    const doc = new DOMParser().parseFromString(svgString, 'image/svg+xml');
    const els = Array.from(doc.querySelectorAll('*'));
    const families = new Map(); // key -> {key, name, swatch, luma}

    for (const el of els) {
      const fillRaw = el.getAttribute('fill') || getStyleColor(el.getAttribute('style'), 'fill');
      const strokeRaw = el.getAttribute('stroke') || getStyleColor(el.getAttribute('style'), 'stroke');

      const colors = [fillRaw, strokeRaw].filter(Boolean);
      for (const c of colors) {
        const rgb = parseColorToRgb(c);
        if (!rgb) continue;
        const fam = mapRgbToFamily(rgb);
        const meta = FAMILY_META[fam];
        if (!meta) continue;
        const luma = relLuma(parseColorToRgb(meta.swatch) || rgb);
        families.set(fam, { key: fam, name: meta.name, swatch: meta.swatch, luma });
      }
    }

    // order by our fixed order (hell->dunkel), but only keep present
    const ordered = [];
    for (const k of FAMILY_ORDER_LIGHT_TO_DARK) {
      if (families.has(k)) ordered.push(families.get(k));
    }
    return ordered;
  }

  function buildLayerSvg(svgString, targetFamily){
    const doc = new DOMParser().parseFromString(svgString, 'image/svg+xml');
    const svgEl = doc.documentElement;

    // Walk elements and remove those that do not belong to the family.
    const all = Array.from(svgEl.querySelectorAll('*')).reverse();

    function elementMatches(el){
      const fillRaw = el.getAttribute('fill') || getStyleColor(el.getAttribute('style'), 'fill');
      const strokeRaw = el.getAttribute('stroke') || getStyleColor(el.getAttribute('style'), 'stroke');

      const fillRgb = parseColorToRgb(fillRaw);
      const strokeRgb = parseColorToRgb(strokeRaw);

      const famFill = fillRgb ? mapRgbToFamily(fillRgb) : null;
      const famStroke = strokeRgb ? mapRgbToFamily(strokeRgb) : null;

      return (famFill === targetFamily) || (famStroke === targetFamily);
    }

    for (const el of all) {
      // keep root, defs, metadata
      if (el === svgEl) continue;
      const tag = (el.tagName || '').toLowerCase();
      if (tag === 'defs' || tag === 'metadata' || tag === 'title' || tag === 'desc') continue;

      // If element has children, keep it for now, it may become empty later.
      const hasChildren = el.children && el.children.length > 0;
      if (hasChildren) continue;

      if (!elementMatches(el)) {
        el.remove();
      }
    }

    // Remove empty groups
    const groups = Array.from(svgEl.querySelectorAll('g')).reverse();
    for (const g of groups) {
      if (g.children.length === 0) g.remove();
    }

    return new XMLSerializer().serializeToString(svgEl);
  }

  function buildColorLayerQueue(svgString){
    const fams = extractFamiliesFromSvg(svgString);
    const layers = [];
    for (const f of fams) {
      const layerSvg = buildLayerSvg(svgString, f.key);
      layers.push({ key: f.key, name: f.name, swatch: f.swatch, svg: layerSvg });
    }
    return layers;
  }

  function paletteInfo(){
    return JSON.parse(JSON.stringify(FAMILY_META));
  }

  global.buildColorLayerQueue = buildColorLayerQueue;
  global.paletteInfo = paletteInfo;

})(window);
