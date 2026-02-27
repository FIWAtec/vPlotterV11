#include "svgmeta.h"

static String toLowerCopy(String s) {
  s.toLowerCase();
  return s;
}

static bool parseAttr(const String& svg, const char* key, String& outVal) {
  const String k1 = String(key) + "=\"";
  int i = svg.indexOf(k1);
  if (i >= 0) {
    i += k1.length();
    int e = svg.indexOf('"', i);
    if (e > i) { outVal = svg.substring(i, e); return true; }
  }

  const String k2 = String(key) + "='";
  i = svg.indexOf(k2);
  if (i >= 0) {
    i += k2.length();
    int e = svg.indexOf('\'', i);
    if (e > i) { outVal = svg.substring(i, e); return true; }
  }
  return false;
}

static bool parseNumberWithUnit(const String& s, double& outNum, String& outUnit) {
  String t = s;
  t.trim();
  if (t.length() == 0) return false;

  int cut = 0;
  while (cut < (int)t.length()) {
    const char c = t.charAt(cut);
    if ((c >= '0' && c <= '9') || c == '.' || c == '-' || c == '+') {
      cut++;
      continue;
    }
    break;
  }

  const String numStr = t.substring(0, cut);
  const String unitStr = t.substring(cut);

  outNum = numStr.toDouble();
  outUnit = unitStr;
  outUnit.trim();
  outUnit.toLowerCase();

  if (outUnit.length() == 0) outUnit = "px";
  return true;
}

double svgUnitToMm(double value, const String& unit) {
  const String u = toLowerCopy(unit);
  if (u == "mm") return value;
  if (u == "cm") return value * 10.0;
  if (u == "in") return value * 25.4;
  if (u == "pt") return value * (25.4 / 72.0);
  if (u == "pc") return value * (25.4 / 6.0);
  return value * (25.4 / 96.0);
}

SvgMeta parseSvgHeaderChunk(const String& chunk) {
  SvgMeta m;
  String svg = chunk;

  int s = svg.indexOf("<svg");
  if (s < 0) {
    m.ok = false;
    m.error = "no <svg tag";
    return m;
  }
  int e = svg.indexOf('>', s);
  if (e < 0) e = (int)svg.length();
  svg = svg.substring(s, e);

  String wStr, hStr, vbStr;
  (void)parseAttr(svg, "width", wStr);
  (void)parseAttr(svg, "height", hStr);
  (void)parseAttr(svg, "viewBox", vbStr);

  if (vbStr.length() > 0) {
    vbStr.trim();
    vbStr.replace(',', ' ');
    vbStr.replace('\t', ' ');
    while (vbStr.indexOf("  ") >= 0) vbStr.replace("  ", " ");

    int p1 = vbStr.indexOf(' ');
    int p2 = (p1 >= 0) ? vbStr.indexOf(' ', p1 + 1) : -1;
    int p3 = (p2 >= 0) ? vbStr.indexOf(' ', p2 + 1) : -1;
    if (p1 > 0 && p2 > p1 && p3 > p2) {
      m.vbX = vbStr.substring(0, p1).toDouble();
      m.vbY = vbStr.substring(p1 + 1, p2).toDouble();
      m.vbW = vbStr.substring(p2 + 1, p3).toDouble();
      m.vbH = vbStr.substring(p3 + 1).toDouble();
      m.hasViewBox = (m.vbW > 0.0 && m.vbH > 0.0);
    }
  }

  double wNum = 0.0, hNum = 0.0;
  String wUnit = "px", hUnit = "px";
  bool hasW = false, hasH = false;

  if (wStr.length() > 0) hasW = parseNumberWithUnit(wStr, wNum, wUnit);
  if (hStr.length() > 0) hasH = parseNumberWithUnit(hStr, hNum, hUnit);

  if (hasW && hasH) {
    m.widthMm = svgUnitToMm(wNum, wUnit);
    m.heightMm = svgUnitToMm(hNum, hUnit);
    m.unit = wUnit;
    m.ok = true;
    return m;
  }

  if (m.hasViewBox) {
    m.widthMm = svgUnitToMm(m.vbW, "px");
    m.heightMm = svgUnitToMm(m.vbH, "px");
    m.unit = "viewBox";
    m.ok = true;
    return m;
  }

  m.ok = false;
  m.error = "no width/height/viewBox";
  return m;
}
