#ifndef SVG_META_H
#define SVG_META_H

#include <Arduino.h>

struct SvgMeta {
  bool ok = false;
  String error;

  // Physical size in mm (derived from width/height attributes if possible)
  double widthMm = 0.0;
  double heightMm = 0.0;

  // Parsed viewBox (if present)
  bool hasViewBox = false;
  double vbX = 0.0;
  double vbY = 0.0;
  double vbW = 0.0;
  double vbH = 0.0;

  // Original units ("mm","cm","in","px","viewBox" ...)
  String unit;
};

SvgMeta parseSvgHeaderChunk(const String& chunk);
double svgUnitToMm(double value, const String& unit);

#endif
