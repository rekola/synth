#pragma once

#include <cellui/CellBuffer.h>
#include <cellui/term/Capabilities.h>

#include <optional>
#include <string>

namespace cellui::term {

// The xterm 256-colour palette entry nearest to an RGB colour, from the
// 6x6x6 cube and the grey ramp only (0-15 follow the user's theme).
int nearest256(const Color & c);
// One of the 8 basic colours: greys by brightness, others by which
// components are at least half on.
int nearest8(const Color & c);

// What the encoder needs to know about the terminal.
struct EncoderProfile {
  ColorDepth colors = ColorDepth::TRUECOLOR;
  bool column_move = true;
  SyncForm sync = SyncForm::NONE;
  // A background equal to this is nudged off it (see Capabilities).
  std::optional<Color> distinct_from;

  static EncoderProfile from(const Capabilities & caps);
};

// Where the terminal's cursor should be shown after a frame, if anywhere.
struct CursorRequest {
  bool visible = false;
  int y = 0, x = 0;
};

// Turns composed frames into the bytes that update a terminal from the
// last frame it was sent: only cells that changed are written, row by row,
// with absolute cursor moves (a column move on the same row) and only the
// colour and style changes the terminal does not already have. An
// unchanged frame is no bytes at all. A non-empty frame is wrapped in
// synchronized output where the terminal has it, so it appears at once.
class FrameEncoder {
 public:
  explicit FrameEncoder(EncoderProfile profile = {});

  // The next frame clears the screen and draws everything: after a resize
  // or a resume, when what the terminal shows is no longer known. A frame
  // of a different size than the last does the same.
  void invalidate() { full_ = true; }
  // The cursor position and pen are no longer known (something else wrote
  // to the terminal).
  void forgetCursor() {
    cursor_y_ = cursor_x_ = -1;
    pen_known_ = false;
  }

  std::string encode(const CellBuffer & frame, const CursorRequest & cursor = {});

 private:
  // A colour as sent: the terminal's default, a palette index or RGB.
  struct Ink {
    enum Kind { DEFAULT, INDEX, RGB } kind = DEFAULT;
    int value = 0;  // the index, or 0xRRGGBB
    bool operator==(const Ink & o) const { return kind == o.kind && value == o.value; }
    bool operator!=(const Ink & o) const { return !(*this == o); }
  };

  Ink ink(const std::optional<Color> & c, bool background) const;
  void moveTo(std::string & out, int y, int x);
  void pen(std::string & out, const CellBuffer::Cell & cell);

  EncoderProfile profile_;
  std::optional<CellBuffer> last_;
  bool full_ = false;

  int cursor_y_ = -1, cursor_x_ = -1;
  bool cursor_visible_ = false;
  int shown_y_ = -1, shown_x_ = -1;

  bool pen_known_ = false;
  Ink fg_, bg_;
  bool bold_ = false, italic_ = false, underline_ = false;
};

}  // namespace cellui::term
