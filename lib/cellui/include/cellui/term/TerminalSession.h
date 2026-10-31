#pragma once

#include <cellui/Backend.h>
#include <cellui/CellBuffer.h>
#include <cellui/PollEventLoop.h>
#include <cellui/term/BackdropEmulation.h>
#include <cellui/term/Capabilities.h>
#include <cellui/term/FrameEncoder.h>
#include <cellui/term/FramePacer.h>
#include <cellui/term/ImageEncoder.h>
#include <cellui/term/InputParser.h>
#include <cellui/term/OutputQueue.h>

#include <chrono>
#include <cstdio>
#include <deque>
#include <map>
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
  // How frames are paced to the link (see FramePacer).
  PacerConfig pacer;
  // How the link is classified (LinkStatus.h).
  LinkClassifierConfig link;
  LinkThresholds link_thresholds;
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
  // The parser's deadline, and the moment the pacer lets the next frame start.
  std::optional<TimePoint> nextDeadline() const override;

  // Frames are paced to the link: the handler is called when the pacer lets a
  // frame start, with the monotonic clock in microseconds. Without a handler
  // present() sends at once and nothing is paced.
  void requestFrame() override { pacer_.requestFrame(); }
  void setFrameHandler(FrameHandler handler) override;

  // What the pacer has learned of the link, and the bandwidth it sends by (bytes
  // a second; 0 until known).
  double estimatedBytesPerSec() const { return pacer_.estimatedBytesPerSec(); }
  PacerStats pacerStats() const;

  // The link's class and description, from what the pacer has learned (see updateLink()).
  LinkStatus linkStatus() const override { return link_.status(); }
  void setLinkHandler(LinkHandler handler) override { link_.setHandler(std::move(handler)); }
  void setLinkPin(std::optional<LinkClass> pin) override { link_.pin(pin); }

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
  // Queues raw bytes and writes what the terminal takes now; the rest goes
  // as it can (never blocks).
  void write(const std::string & bytes);

  // The pointer's shape the last frame asked for over the cell it is in.
  MouseCursor mouseCursor() const { return shown_cursor_; }

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
  Connection frames_hook_;
  int fd_ = -1;
  bool own_fd_ = false;
  int wake_read_ = -1, wake_write_ = -1;
  bool open_ = false;
  bool handlers_ = false;
  bool line_signals_ = false;
  bool mouse_ = true;
  bool mouse_reporting_ = true;
  // Mouse positions are asked for in pixels (mode 1016): the terminal can,
  // the cell size is known and the mouse is reported to the application.
  bool pixel_mouse_on_ = false;
  bool wantPixelMouse() const {
    return mouse_ && mouse_reporting_ && caps_.pixel_mouse && caps_.display.cell_px_height > 0 && caps_.display.cell_px_width > 0;
  }
  // Brings mode 1016 and the parser in line with wantPixelMouse().
  void updatePixelMouse();
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
  // The areas of the last frame that asked for a pointer shape, the cell the
  // pointer is in, and the shape the terminal was told to show.
  std::vector<CursorArea> cursor_areas_;
  int pointer_row_ = -1, pointer_col_ = -1;
  MouseCursor shown_cursor_ = MouseCursor::DEFAULT;
  void updateMouseCursor();
  bool ended_ = false;

  // Output: everything goes through the queue, which never blocks. A frame is
  // followed by a fence (a cursor position query) whose reply acknowledges it.
  OutputQueue out_;
  struct Mark {
    uint64_t end;  // the queue position after the frame (or the bare query)
    FrameBytes size;
    bool probe;
  };
  std::deque<Mark> marks_;
  FramePacer pacer_{false};
  PacerConfig pacer_config_;
  FrameHandler frame_handler_;
  bool pacing_ = false;
  bool in_frame_ = false;
  bool frame_presented_ = false;
  size_t late_replies_ = 0;
  size_t kitty_errors_ = 0;
  std::string last_kitty_error_;
  // The latest frame presented outside the pacer's turn, sent when it allows.
  struct Held {
    CellBuffer frame{0, 0};
    CursorRequest cursor;
    std::vector<ImagePlacement> images;
    std::vector<BackdropLayer> layers;
  };
  std::optional<Held> held_;
  // SYNTH_LINK_STATS=<file>: a line a second of what the link carried (see logLinkStats()).
  FILE * link_log_ = nullptr;
  std::chrono::steady_clock::time_point link_log_last_{};
  FrameBytes link_log_mark_;
  size_t link_log_frames_ = 0;
  std::map<std::pair<int, int>, ImageEncoder::PictureStat> link_log_pictures_;
  void logLinkStats(std::chrono::steady_clock::time_point now);
  // Once a second: the class, from the bandwidth estimate, but only on a second the link held frames back
  // (otherwise the estimate says what the application sent, not what the link carries) and not in
  // the first seconds, while the estimate is still a lower bound.
  void updateLink(std::chrono::steady_clock::time_point now);
  LinkMonitor link_;
  LinkThresholds link_thresholds_;
  std::chrono::steady_clock::time_point link_opened_{}, link_last_pump_{}, link_last_update_{};
  double link_blocked_seconds_ = 0;
  bool link_was_blocked_ = false;  // at the last turn: the link held the next frame back
  size_t link_stalls_seen_ = 0;
  std::chrono::steady_clock::time_point link_stalled_at_{};
  void writeBlocking(const std::string & bytes);
  void flushOutput();
  void drainOutput(std::chrono::milliseconds limit);
  void pumpFrame();
  void sendFrame(const CellBuffer & frame, const CursorRequest & cursor, const std::vector<ImagePlacement> & images,
                 const std::vector<BackdropLayer> & layers);
  std::string fenceBytes();
};

}  // namespace cellui::term
