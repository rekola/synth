#pragma once

#include <cellui/Backend.h>
#include <cellui/CellBuffer.h>
#include <cellui/PollEventLoop.h>
#include <cellui/term/BackdropEmulation.h>
#include <cellui/term/Capabilities.h>
#include <cellui/term/FrameEncoder.h>
#include <cellui/term/ImageEncoder.h>
#include <cellui/term/InputParser.h>

#include <chrono>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace cellui::term {

struct SessionOptions {
  // The terminal to use; -1 opens /dev/tty, leaving standard input and
  // output alone (and blocking, for anything else that writes to them).
  int fd = -1;
  // Keep the line discipline's signals: Ctrl-C, Ctrl-Z and Ctrl-\ then
  // never arrive as keys.
  bool line_signals = false;
  // Restore the terminal when the process dies of a signal. A termination
  // signal (INT, TERM, HUP, QUIT) the program handles itself is passed on
  // to its handler instead, which ends the session normally.
  bool install_signal_handlers = true;
  bool mouse = true;
  // How what has to meet a palette (Sixel images) is dithered.
  Dither::Method dither = Dither::Method::ORDERED;
  Environment env = Environment::fromProcess();
  std::chrono::milliseconds grace{25};
};

// A full-screen session on a POSIX terminal: the terminal put into the
// mode the UI needs, start-up negotiation, frames written, input read, and
// everything put back on close, on exit, or when the process dies of a
// signal. Single-threaded: one thread calls everything except wake().
//
// The caller's loop waits for inputFd() or wakeFd() to be readable, or for
// nextDeadline(), then calls poll(), which returns every event available
// without blocking.
class TerminalSession : public Backend {
 public:
  using TimePoint = InputParser::TimePoint;

  // Starts a session; nullptr (with *error set) when there is no usable
  // terminal. Waits for the start-up replies, at most Discovery::timeout().
  static std::unique_ptr<TerminalSession> open(SessionOptions options, std::string * error = nullptr);
  ~TerminalSession() override;
  TerminalSession(const TerminalSession &) = delete;
  TerminalSession & operator=(const TerminalSession &) = delete;

  // Puts the terminal back as it was. Idempotent.
  void close();

  // What start-up learned; display() is its part the application sees.
  const Capabilities & capabilities() const { return caps_; }
  const DisplayInfo & display() const override { return caps_.display; }
  int rows() const override { return rows_; }
  int cols() const override { return cols_; }

  EventLoop & eventLoop() override { return loop_; }
  int inputFd() const override { return fd_; }
  int wakeFd() const override { return wake_read_; }
  std::optional<TimePoint> nextDeadline() const override { return parser_.nextDeadline(); }

  // Every event available now, as the application sees them. A resize is
  // reported with the new size, already read from the terminal.
  std::vector<BackendEvent> poll() override;
  // The same events as the terminal sent them, queries' replies aside.
  std::vector<TermEvent> pollTerm();

  // Draws a frame (see FrameEncoder) and its images (ImageEncoder).
  // A real terminal has no use for the frame info.
  void present(const CellBuffer & frame, const CursorRequest & cursor = {}, const std::vector<ImagePlacement> & images = {},
               const FrameInfo * info = nullptr) override;
  // The next present() redraws the whole screen (after the screen may have
  // been disturbed, Emacs's C-l).
  void redraw() override { encoder_.invalidate(); }
  // Only when the session was opened with the mouse (SessionOptions::mouse).
  void setNativeSelection(bool on) override;
  // Writes raw bytes, all of them.
  void write(const std::string & bytes);

  // Makes wakeFd() readable; any thread may call it, and so may a signal
  // handler.
  void wake();

  // The bytes that undo the session's terminal modes, as close() sends
  // them.
  const std::string & restoreSequence() const { return restore_; }

 private:
  TerminalSession() = default;
  bool start(SessionOptions & options, std::string * error);
  void negotiate(const Environment & env);
  void enterModes();
  void readSize();

  PollEventLoop loop_;
  int fd_ = -1;
  bool own_fd_ = false;
  int wake_read_ = -1, wake_write_ = -1;
  bool open_ = false;
  bool handlers_ = false;
  bool line_signals_ = false;
  bool mouse_ = true;
  bool mouse_reporting_ = true;
  Dither::Method dither_ = Dither::Method::ORDERED;
  // The image encoder's profile for the capabilities, with the session's
  // dithering.
  ImageEncoder::Profile imageProfile() const;
  int rows_ = 24, cols_ = 80;
  bool size_known_ = false;
  Capabilities caps_;
  InputParser parser_;
  FrameEncoder encoder_;
  ImageEncoder images_;
  // What a terminal makes of a backdrop (it cannot blur one).
  BackdropEmulation backdrops_;
  std::string setup_;
  std::string restore_;
  std::deque<TermEvent> pending_;
  bool ended_ = false;
};

}  // namespace cellui::term
