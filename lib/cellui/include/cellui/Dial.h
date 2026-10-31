#pragma once

#include <cellui/Color.h>
#include <cellui/Input.h>
#include <cellui/InputEvents.h>
#include <cellui/Plane.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace cellui {

// A dial: a ring open at the bottom for a one-letter caption, and a pointer
// from the centre that sweeps the ring from its bottom-left end (fraction 0)
// over the top to its bottom-right end (fraction 1).
struct DialReading {
  std::string letter;
  float fraction = 0.0f;  // 0..1
  // What the dial's slot shows where no picture can be: the value, in the
  // middle of a rounded box with the letter in its bottom border.
  std::string text;
  // While a value is being typed: what has been typed, shown over the dial
  // (dimmed in the picture) with a caret after it.
  bool editing = false;
  std::string edit_text;
};

// The ring and pointer in `foreground`, on `background` (which the pointer's
// outline is drawn in too, to keep it apart from the ring).
struct DialLook {
  Color background;
  Color foreground;
};

namespace dial_image {

// A width x height picture of the dial, as large as fits, centred. Returns
// width*height pixels, row 0 at the top, bytes R, G, B, A (all opaque), or
// nothing where canvas has no pixel backend. Drawn at `scale`, the display
// scale: the ring is 4 pixels thick at scale 1.
std::vector<uint32_t> render(const DialReading & reading, const DialLook & look, unsigned width, unsigned height, float scale = 1.0f);

}  // namespace dial_image

// Draws a dial in the `rows` x `cols` cells from (row, col), only in the plane
// rows from clip_top up to clip_bottom. A plane that shows pictures
// (Plane::cellPixelSize()) gets the picture, with no number; any other gets
// the rounded box with `reading.text` in the same cells. `id` names the dial for
// the plane's images (see Plane::drawImageAt()).
void drawDial(Plane & plane, int row, int col, int rows, int cols, const DialReading & reading, const DialLook & look, int clip_top,
              int clip_bottom, uint64_t id = 0);

// The text version of drawDial().
void drawCellDial(Plane & plane, int row, int col, int rows, int cols, const DialReading & reading, const DialLook & look, int clip_top,
                  int clip_bottom);

// What a Dial turns: the value at the two ends of its arc, what lies below the
// lowest step, and how far each gesture moves it.
struct DialSpec {
  std::string letter;
  float min = -36.0f, max = 0.0f;  // the arc, from its bottom-left end to its bottom-right one
  float off = -100.0f;             // what a turn below `min` sets
  float typed_min = -100.0f, typed_max = 12.0f;  // what may be typed
  float drag_per_row = 3.0f;  // a row dragged
  float wheel_step = 1.0f;    // a wheel step
  float fine = 0.1f;          // the factor Shift puts on both
};

// A dial that handles itself: the picture or the text version (drawDial()),
// dragging up and down to turn it, the wheel, a click to type a value over it
// (Enter commits, Escape or losing focus closes it), and the selected and
// pressed looks. The host places it (setPosition()), gives it the
// value, draws it and passes it the mouse and keys; it owns none of the value
// and none of the gestures.
class Dial {
 public:
  static constexpr int kRows = 3, kCols = 5;

  Dial(Plane & plane, DialSpec spec) : plane_(plane), spec_(std::move(spec)) {}

  // The slot's top-left cell in the plane, and the plane rows it may draw in.
  void setPosition(int row, int col) { row_ = row, col_ = col; }
  void setClip(int top, int bottom) { clip_top_ = top, clip_bottom_ = bottom; }
  void setId(uint64_t id) { id_ = id; }
  // The ring and background when idle.
  void setLook(const Color & foreground, const Color & background) { foreground_ = foreground, background_ = background; }
  void setValue(float value) { value_ = value; }
  float value() const { return value_; }
  // The cursor's mark on the dial, 0..1 (how far it has eased in).
  void setSelected(float amount) { selected_ = amount; }
  // The pressed look, as while the button is held on the dial.
  void setPressed(bool pressed) { pressed_ = pressed; }
  bool isPressed() const { return pressed_; }

  // The value was turned or typed.
  std::function<void(float)> on_change;
  // A turn by dragging starts and ends, around its on_change calls (one step of undo).
  std::function<void()> on_turn_begin, on_turn_end;
  // The dial was clicked (the editor opens).
  std::function<void()> on_click;

  void draw();
  bool contains(int screen_y, int screen_x) const;

  // The mouse, in cells of the screen (`y` with its fraction where the display
  // has one): a press, a drag (the press again), the release and the wheel
  // (`up` for a notch up). True if the dial took it. A press is taken only
  // over the dial, the rest of a drag wherever the pointer is.
  bool press(float y, int row, int col, bool shift);
  bool release();
  bool wheel(int row, int col, bool up, bool shift);
  // While the editor is open, every key.
  bool key(int key, const Modifiers & mods);
  bool isDragging() const { return down_; }
  bool isEditing() const { return editing_; }
  // Starts typing a value: the current one is shown and the first key replaces it.
  void startEdit();
  void cancelEdit();
  // Changes with everything that needs the dial drawn again.
  uint64_t version() const { return version_; }

  // The position of a value on the arc, 0..1.
  float fraction() const;

 private:
  void turn(float base, float delta);
  std::string valueText() const;

  void commitEdit();

  Plane & plane_;
  DialSpec spec_;
  bool editing_ = false, replaceable_ = false;
  std::string edit_text_;
  uint64_t version_ = 0;
  int row_ = 0, col_ = 0, clip_top_ = 0, clip_bottom_ = 1 << 20;
  uint64_t id_ = 0;
  Color foreground_ = Color(255, 255, 255), background_ = Color(0, 0, 0);
  float value_ = 0.0f, selected_ = 0.0f;
  bool pressed_ = false, down_ = false, moved_ = false;
  float start_y_ = 0.0f, start_value_ = 0.0f;
};

}  // namespace cellui
