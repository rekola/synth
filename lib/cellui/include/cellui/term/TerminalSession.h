#pragma once

#include <cellui/CellBuffer.h>
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
class TerminalSession {
 public:
  using TimePoint = InputParser::TimePoint;

  // Starts a session; nullptr (with *error set) when there is no usable
  // terminal. Waits for the start-up replies, at most Discovery::timeout().
  static std::unique_ptr<TerminalSession> open(SessionOptions options, std::string * error = nullptr);
  ~TerminalSession();
  TerminalSession(const TerminalSession &) = delete;
  TerminalSession & operator=(const TerminalSession &) = delete;

  // Puts the terminal back as it was. Idempotent.
  void close();

  const Capabilities & capabilities() const { return caps_; }
  int rows() const { return rows_; }
  int cols() const { return cols_; }

  int inputFd() const { return fd_; }
  int wakeFd() const { return wake_read_; }
  std::optional<TimePoint> nextDeadline() const { return parser_.nextDeadline(); }

  // Every event available now. A resize is reported with the new size,
  // already read from the terminal.
  std::vector<TermEvent> poll();

  // Draws a frame (see FrameEncoder) and its images (ImageEncoder).
  void present(const CellBuffer & frame, const CursorRequest & cursor = {}, const std::vector<ImagePlacement> & images = {});
  // The next present() redraws the whole screen (after the screen may have
  // been disturbed, Emacs's C-l).
  void redraw() { encoder_.invalidate(); }
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

  int fd_ = -1;
  bool own_fd_ = false;
  int wake_read_ = -1, wake_write_ = -1;
  bool open_ = false;
  bool handlers_ = false;
  bool line_signals_ = false;
  bool mouse_ = true;
  int rows_ = 24, cols_ = 80;
  bool size_known_ = false;
  Capabilities caps_;
  InputParser parser_;
  FrameEncoder encoder_;
  ImageEncoder images_;
  std::string setup_;
  std::string restore_;
  std::deque<TermEvent> pending_;
  bool ended_ = false;
};

}  // namespace cellui::term
