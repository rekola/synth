#pragma once

#include "LevelMeter.h"

#include <cellui/Lamp.h>

// A track's clip lamp in the clip grid: lit for a while after the last clip it was told of.
class ClipLamp {
 public:
  static constexpr int kCols = cellui::kLampCols;

  void update(bool clipping, float dt_seconds) { lit_ = hold_.update(clipping, dt_seconds); }
  bool lit() const { return lit_; }
  void draw(cellui::Plane & plane, int row, int col, const cellui::Color & bg, int rows) const {
    cellui::drawLamp(plane, row, col, lit_, bg, 0, rows);
  }

 private:
  level_meter::ClipHold hold_;
  bool lit_ = false;
};
