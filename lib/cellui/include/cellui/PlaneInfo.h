#pragma once

#include <cellui/BackdropLayer.h>

#include <string>
#include <vector>

namespace cellui {

// What a plane is, as a test or an inspector sees it: its place inside its
// parent, its size, whether it shows, and the planes on it, bottom to top.
struct PlaneInfo {
  std::string name;  // set by the application (UIPlane::setName()), may be empty
  int y = 0, x = 0;  // relative to the parent
  int rows = 0, cols = 0;
  bool visible = true;
  float elevation = 0;  // above the parent (UIPlane::setElevation())
  bool reader = false;  // a text field is open on this plane
  bool picker = false;  // a picker is open on this plane
  std::vector<PlaneInfo> children;
};

// The part of the screen a plane above the base covers, at its height above
// it (its own and its ancestors' elevations together).
struct ElevatedArea {
  int row = 0, col = 0, rows = 0, cols = 0;
  float elevation = 0;
  std::string name;
  // How many of the frame's backdrop layers lie below it: one fewer than
  // FrameInfo::backdrops has means a later backdrop covers it.
  size_t layer = 0;
};

// What an application tells a backend about a frame besides its cells.
struct FrameInfo {
  PlaneInfo planes;
  // Which widget takes the keys, in the application's own words.
  std::string focus;
  // The layers below each backdrop, bottom first. The display applies the
  // backdrops (blurs and dims the layer below); the frame's cells are what
  // is above the last one. Always given, empty without a backdrop.
  std::vector<BackdropLayer> backdrops;
  // Every shown plane above the base, in the order they are drawn (lowest
  // first). Always given.
  std::vector<ElevatedArea> elevations;
};

}  // namespace cellui
