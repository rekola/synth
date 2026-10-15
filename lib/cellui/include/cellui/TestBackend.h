#pragma once

#include <cellui/term/Terminal.h>

#include <deque>
#include <string>
#include <vector>

namespace cellui::test {

// The backend of a test: it has no screen, tty or window. The frames an
// application presents are kept for the test to read (frame(), text(),
// info()), and the input comes from the test, so what the UI shows can be
// asserted on without a pty or an emulator. Input is queued with the push
// functions and read with poll(), as a session's would be; inputFd() is
// readable while events wait.
//
// It stands in for the terminal now (it implements term::Terminal); a
// window backend's features - pixels, scale, a pointer - join it as that
// backend's interface is defined, so the same tests can run on both.
class TestBackend : public term::Terminal {
 public:
  explicit TestBackend(int rows = 24, int cols = 80, term::Capabilities caps = {});
  ~TestBackend() override;
  TestBackend(const TestBackend &) = delete;
  TestBackend & operator=(const TestBackend &) = delete;

  // Terminal.
  const term::Capabilities & capabilities() const override { return caps_; }
  int rows() const override { return rows_; }
  int cols() const override { return cols_; }
  int inputFd() const override { return pipe_[0]; }
  int wakeFd() const override { return wake_[0]; }
  std::optional<TimePoint> nextDeadline() const override { return std::nullopt; }
  std::vector<term::TermEvent> poll() override;
  void present(const CellBuffer & frame, const term::CursorRequest & cursor = {},
               const std::vector<ImagePlacement> & images = {}, const FrameInfo * info = nullptr) override;
  bool wantsFrameInfo() const override { return true; }
  void redraw() override { redraws_++; }
  void setMouseReporting(bool on) override { mouse_reporting_ = on; }
  bool mouseReporting() const { return mouse_reporting_; }

  // Input.
  void push(const term::TermEvent & event);
  void pushKey(int key, unsigned mods = 0) { push(term::TermEvent::makeKey(key, mods)); }
  // Each character of `text` as a key (UTF-8 aware).
  void pushText(const std::string & text);
  void pushMouse(int y, int x, int key, unsigned mods = 0, term::TermEvent::Kind kind = term::TermEvent::Kind::PRESS);
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
  const term::CursorRequest & cursor() const { return cursor_; }
  const std::vector<ImagePlacement> & images() const { return images_; }
  const FrameInfo & info() const { return info_; }

 private:
  term::Capabilities caps_;
  int rows_, cols_;
  int pipe_[2] = { -1, -1 };
  int wake_[2] = { -1, -1 };
  std::deque<term::TermEvent> queue_;
  CellBuffer frame_;
  term::CursorRequest cursor_;
  std::vector<ImagePlacement> images_;
  FrameInfo info_;
  size_t frames_ = 0, redraws_ = 0;
  bool mouse_reporting_ = true;
};

}  // namespace cellui::term
