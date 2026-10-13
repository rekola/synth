#pragma once

#include <cellui/term/Terminal.h>

#include <deque>
#include <string>
#include <vector>

namespace cellui::term {

// A terminal with no tty: the frames an application presents are kept and
// can be read back as JSON, and the input comes from the test. It is the
// terminal backend of a test, so what the UI shows can be asserted on
// without a pty or an emulator. Input is queued with the push functions and
// read with poll(), as a session's would be; inputFd() is readable while
// events wait.
class TestTerminal : public Terminal {
 public:
  explicit TestTerminal(int rows = 24, int cols = 80, Capabilities caps = {});
  ~TestTerminal() override;
  TestTerminal(const TestTerminal &) = delete;
  TestTerminal & operator=(const TestTerminal &) = delete;

  // Terminal.
  const Capabilities & capabilities() const override { return caps_; }
  int rows() const override { return rows_; }
  int cols() const override { return cols_; }
  int inputFd() const override { return pipe_[0]; }
  int wakeFd() const override { return wake_[0]; }
  std::optional<TimePoint> nextDeadline() const override { return std::nullopt; }
  std::vector<TermEvent> poll() override;
  void present(const CellBuffer & frame, const CursorRequest & cursor = {},
               const std::vector<ImagePlacement> & images = {}, const FrameInfo * info = nullptr) override;
  bool wantsFrameInfo() const override { return true; }
  void redraw() override { redraws_++; }

  // Input.
  void push(const TermEvent & event);
  void pushKey(int key, unsigned mods = 0) { push(TermEvent::makeKey(key, mods)); }
  // Each character of `text` as a key (UTF-8 aware).
  void pushText(const std::string & text);
  void pushMouse(int y, int x, int key, unsigned mods = 0, TermEvent::Kind kind = TermEvent::Kind::PRESS);
  void pushPaste(const std::string & text);
  // The terminal's size changes; the next poll() reports it.
  void resize(int rows, int cols);
  // The terminal goes away (the end of input).
  void pushEnd();
  // Makes wakeFd() readable, as TerminalSession::wake().
  void wake();

  // Output.
  size_t frameCount() const { return frames_; }
  size_t redrawCount() const { return redraws_; }
  const CellBuffer & frame() const { return frame_; }
  // The last frame's cells as text, one line per row.
  std::string text() const { return frame_.toPlainText(); }
  const CursorRequest & cursor() const { return cursor_; }
  const std::vector<ImagePlacement> & images() const { return images_; }
  const FrameInfo & info() const { return info_; }
  // The last frame as JSON: size, cursor, focus, the plane tree, the rows
  // (text, and runs of cells sharing a style) and the image placements.
  // Deterministic, so a golden file can hold it.
  std::string toJson() const;

 private:
  Capabilities caps_;
  int rows_, cols_;
  int pipe_[2] = { -1, -1 };
  int wake_[2] = { -1, -1 };
  std::deque<TermEvent> queue_;
  CellBuffer frame_;
  CursorRequest cursor_;
  std::vector<ImagePlacement> images_;
  FrameInfo info_;
  size_t frames_ = 0, redraws_ = 0;
};

}  // namespace cellui::term
