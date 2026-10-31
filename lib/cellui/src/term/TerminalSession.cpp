#include <cellui/term/TerminalSession.h>

#include <cellui/term/Discovery.h>

#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#ifdef __linux__
#include <linux/kd.h>
#endif

namespace cellui::term {

namespace {

// What a signal handler needs, kept where it can reach it without locks:
// one session at a time per process.
struct Global {
  std::atomic<bool> active{false};
  int fd = -1;
  int wake_write = -1;
  struct termios saved;
  char restore[1024];
  size_t restore_size = 0;
  std::atomic<bool> resized{false};
  std::atomic<bool> continued{false};
};
Global g;

const int kFatalSignals[] = {SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT};
const int kTerminationSignals[] = {SIGINT, SIGTERM, SIGHUP, SIGQUIT};
struct sigaction g_previous[NSIG];

// Async-signal-safe: writes the precomputed bytes and the saved modes.
void restoreFromSignal() {
  if (!g.active.exchange(false)) return;
  size_t done = 0;
  while (done < g.restore_size) {
    ssize_t n = ::write(g.fd, g.restore + done, g.restore_size - done);
    if (n <= 0) break;
    done += static_cast<size_t>(n);
  }
  tcsetattr(g.fd, TCSAFLUSH, &g.saved);
}

void notify(char what) {
  int saved_errno = errno;
  if (g.wake_write >= 0) {
    ssize_t n = ::write(g.wake_write, &what, 1);
    (void)n;
  }
  errno = saved_errno;
}

void onResize(int) {
  g.resized = true;
  notify('r');
}

void onContinue(int) {
  g.continued = true;
  notify('c');
}

void onFatal(int sig) {
  restoreFromSignal();
  sigaction(sig, &g_previous[sig], nullptr);
  raise(sig);
}

void onTermination(int sig) {
  const struct sigaction & prev = g_previous[sig];
  bool handled = (prev.sa_flags & SA_SIGINFO) ? prev.sa_sigaction != nullptr
                                              : prev.sa_handler != SIG_DFL && prev.sa_handler != SIG_IGN;
  if (handled) {
    // The program ends the session itself.
    if (prev.sa_flags & SA_SIGINFO) prev.sa_sigaction(sig, nullptr, nullptr);
    else prev.sa_handler(sig);
    return;
  }
  onFatal(sig);
}

void install(int sig, void (*handler)(int), bool reset) {
  struct sigaction sa;
  std::memset(&sa, 0, sizeof sa);
  sa.sa_handler = handler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = reset ? static_cast<int>(SA_RESETHAND) : SA_RESTART;
  sigaction(sig, &sa, &g_previous[sig]);
}

void uninstall(int sig) { sigaction(sig, &g_previous[sig], nullptr); }

void restoreAtExit() { restoreFromSignal(); }

sigset_t blockedWhileWriting() {
  sigset_t set;
  sigemptyset(&set);
  // Parenthesised to call the function: where sigaddset is a macro (macOS) it
  // does int arithmetic on an unsigned sigset_t.
  for (int s : kTerminationSignals) (sigaddset)(&set, s);
  return set;
}

void setNonblocking(int fd) {
  int flags = fcntl(fd, F_GETFL);
  if (flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

}  // namespace

std::unique_ptr<TerminalSession> TerminalSession::open(SessionOptions options, std::string * error) {
  std::unique_ptr<TerminalSession> session(new TerminalSession());
  session->frames_hook_ = session->loop_.onFrame([s = session.get()] { s->pumpFrame(); });
  if (!session->start(options, error)) return nullptr;
  return session;
}

bool TerminalSession::start(SessionOptions & options, std::string * error) {
  auto fail = [&](const std::string & why) {
    if (error) *error = why;
    if (own_fd_ && fd_ >= 0) ::close(fd_);
    fd_ = -1;
    return false;
  };
  if (g.active) return fail("a terminal session is already open");
  const std::string & term = options.env.term;
  if (term == "dumb") return fail("TERM=dumb cannot address the cursor");
  if (options.fd >= 0) {
    fd_ = options.fd;
  } else {
    fd_ = ::open("/dev/tty", O_RDWR | O_NOCTTY | O_CLOEXEC);
    own_fd_ = true;
    if (fd_ < 0) return fail(std::string("no terminal: ") + std::strerror(errno));
  }
  if (!isatty(fd_)) return fail("not a terminal");
#ifdef KDGETMODE
  int kd_mode = 0;
  if (ioctl(fd_, KDGETMODE, &kd_mode) == 0) options.env.linux_console = true;
#endif
  if (tcgetattr(fd_, &g.saved) != 0) return fail(std::string("tcgetattr: ") + std::strerror(errno));
  int pipe_fds[2];
  if (pipe(pipe_fds) != 0) return fail(std::string("pipe: ") + std::strerror(errno));
  wake_read_ = pipe_fds[0];
  wake_write_ = pipe_fds[1];
  for (int fd : pipe_fds) {
    setNonblocking(fd);
    fcntl(fd, F_SETFD, FD_CLOEXEC);
  }
  setNonblocking(fd_);
  loop_.addWriteSource(fd_, [this] { return !out_.empty(); }, [this] { flushOutput(); });
  line_signals_ = options.line_signals;
  mouse_ = options.mouse;
  dither_ = options.dither;
  parser_.setGrace(options.grace);
  pacer_config_ = options.pacer;
  link_ = LinkMonitor(options.link);
  link_thresholds_ = options.link_thresholds;
  link_opened_ = std::chrono::steady_clock::now();
  if (const char * path = std::getenv("SYNTH_LINK_STATS")) link_log_ = std::fopen(path, "a");

  // cbreak, plus what an Emacs-style key map needs: Ctrl-S/Q (flow
  // control), Ctrl-V/O (the line discipline's own) and, unless kept,
  // Ctrl-C/Z/\ as keys rather than signals.
  struct termios t = g.saved;
  t.c_lflag &= ~static_cast<tcflag_t>(ICANON | ECHO | IEXTEN);
  if (!line_signals_) t.c_lflag &= ~static_cast<tcflag_t>(ISIG);
  t.c_iflag &= ~static_cast<tcflag_t>(ICRNL | IXON);
  t.c_cc[VMIN] = 1;
  t.c_cc[VTIME] = 0;
  if (tcsetattr(fd_, TCSANOW, &t) != 0) return fail(std::string("tcsetattr: ") + std::strerror(errno));

  g.fd = fd_;
  g.wake_write = wake_write_;
  g.resized = g.continued = false;
  g.restore_size = 0;
  g.active = true;
  open_ = true;
  handlers_ = options.install_signal_handlers;
  if (handlers_) {
    install(SIGWINCH, onResize, false);
    install(SIGCONT, onContinue, false);
    for (int s : kFatalSignals) install(s, onFatal, true);
    for (int s : kTerminationSignals) install(s, onTermination, true);
    static bool registered = false;
    if (!registered) {
      std::atexit(restoreAtExit);
      registered = true;
    }
  }

  readSize();
  negotiate(options.env);
  enterModes();
  return true;
}

void TerminalSession::readSize() {
  struct winsize ws;
  if (ioctl(fd_, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0 && ws.ws_col > 0) {
    rows_ = ws.ws_row;
    cols_ = ws.ws_col;
    size_known_ = true;
    if (ws.ws_ypixel > 0 && ws.ws_xpixel > 0 && caps_.display.cell_px_height == 0) {
      caps_.display.cell_px_height = ws.ws_ypixel / ws.ws_row;
      caps_.display.cell_px_width = ws.ws_xpixel / ws.ws_col;
    }
  }
  parser_.setScreenSize(rows_, cols_);
}

void TerminalSession::negotiate(const Environment & env) {
  Discovery discovery(env);
  // The alternate screen first, so whatever a terminal fails to consume
  // lands there.
  parser_.expectCursorReport();
  parser_.expectDecCursorReport();
  writeBlocking("\x1b[?1049h" + discovery.queries());
  auto deadline = std::chrono::steady_clock::now() + discovery.timeout();
  char buf[4096];
  while (!discovery.complete()) {
    auto now = std::chrono::steady_clock::now();
    if (now >= deadline) break;
    struct pollfd p = {fd_, POLLIN, 0};
    int ms = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count()) + 1;
    int r = ::poll(&p, 1, ms);
    if (r < 0 && errno != EINTR) break;
    if (r <= 0) continue;
    ssize_t n = ::read(fd_, buf, sizeof buf);
    if (n <= 0) {
      if (n < 0 && (errno == EAGAIN || errno == EINTR)) continue;
      break;
    }
    parser_.feed(buf, static_cast<size_t>(n), std::chrono::steady_clock::now());
    while (parser_.hasEvents()) {
      TermEvent e = parser_.pop();
      if (e.type == TermEvent::Type::REPLY) discovery.ingest(e);
      else pending_.push_back(std::move(e));
    }
  }
  discovery.setWindowCellPixels(caps_.display.cell_px_height, caps_.display.cell_px_width);
  caps_ = discovery.result();
  if (caps_.rows > 0 && caps_.cols > 0 && !size_known_) {
    rows_ = caps_.rows;
    cols_ = caps_.cols;
  }
  caps_.rows = rows_;
  caps_.cols = cols_;
  parser_.setScreenSize(rows_, cols_);
  parser_.setKittyKeyboard(caps_.kitty_keyboard);
  parser_.setRxvt(caps_.rxvt);
  parser_.forgetExpectedReports();
  pacer_ = FramePacer(caps_.dec_cursor_report || caps_.cursor_report, pacer_config_);
  encoder_ = FrameEncoder(EncoderProfile::from(caps_));
  images_ = ImageEncoder(imageProfile());
}

void TerminalSession::enterModes() {
  setup_.clear();
  restore_.clear();
  // Without the kitty protocol, xterm's modifyOtherKeys reports modified
  // keys a legacy encoding cannot express.
  if (!caps_.kitty_keyboard) setup_ += "\x1b[>4;2m\x1b[>2;1m";
  setup_ += "\x1b[?7l";    // no autowrap: the last cell never scrolls the screen
  setup_ += "\x1b[?25l";   // cursor hidden
  setup_ += "\x1b[?2004h"; // bracketed paste
  setup_ += "\x1b[?1004h"; // focus reports
  setup_ += "\x1b=";       // application keypad: Ctrl+numpad / and * arrive as keys of their own
  if (mouse_) setup_ += "\x1b[?1003;1006h";
  pixel_mouse_on_ = wantPixelMouse();
  parser_.setPixelMouse(pixel_mouse_on_, caps_.display.cell_px_height, caps_.display.cell_px_width);
  caps_.display.precise_mouse = pixel_mouse_on_;
  if (pixel_mouse_on_) setup_ += "\x1b[?1016h";
  if (caps_.pixels == PixelProtocol::KITTY) setup_ += "\x1b_Ga=d,q=1\x1b\\";
  writeBlocking(setup_);
  encoder_.invalidate();

  restore_ += "\x1b[?2026l";
  restore_ += "\x1b[0m";
  restore_ += "\x1b]22;default\x1b\\";
  if (mouse_) restore_ += "\x1b[?1016l\x1b[?1003;1006l";
  restore_ += "\x1b>\x1b[?1004l\x1b[?2004l\x1b[?7h\x1b[?12l\x1b[?25h";
  restore_ += caps_.kitty_keyboard ? "\x1b[<u" : "\x1b[<u\x1b[>4m\x1b[>2m";
  if (caps_.pixels == PixelProtocol::KITTY) restore_ += "\x1b_Ga=d,q=1\x1b\\";
  restore_ += "\x1b[?1049l";
  size_t n = std::min(restore_.size(), sizeof g.restore);
  std::memcpy(g.restore, restore_.data(), n);
  g.restore_size = n;
}

TerminalSession::~TerminalSession() { close(); }

void TerminalSession::close() {
  if (!open_) return;
  open_ = false;
  if (handlers_) {
    uninstall(SIGWINCH);
    uninstall(SIGCONT);
    for (int s : kFatalSignals) uninstall(s);
    for (int s : kTerminationSignals) uninstall(s);
  }
  if (g.active.exchange(false)) {
    drainOutput(std::chrono::milliseconds(500));
    writeBlocking(restore_);
    tcsetattr(fd_, TCSAFLUSH, &g.saved);
  }
  g.fd = -1;
  g.wake_write = -1;
  if (link_log_) std::fclose(link_log_);
  link_log_ = nullptr;
  ::close(wake_read_);
  ::close(wake_write_);
  wake_read_ = wake_write_ = -1;
  if (own_fd_) ::close(fd_);
  fd_ = -1;
}

ImageEncoder::Profile TerminalSession::imageProfile() const {
  ImageEncoder::Profile profile = ImageEncoder::Profile::from(caps_);
  profile.dither = dither_;
  return profile;
}

void TerminalSession::setNativeSelection(bool native) {
  bool on = !native;
  if (!mouse_ || on == mouse_reporting_) return;
  mouse_reporting_ = on;
  write(on ? "\x1b[?1003;1006h" : "\x1b[?1016l\x1b[?1003;1006l");
  // The write above left 1016 off either way.
  pixel_mouse_on_ = false;
  caps_.display.precise_mouse = false;
  updatePixelMouse();
}

void TerminalSession::updatePixelMouse() {
  bool want = wantPixelMouse();
  parser_.setPixelMouse(want, caps_.display.cell_px_height, caps_.display.cell_px_width);
  if (want == pixel_mouse_on_) return;
  pixel_mouse_on_ = want;
  caps_.display.precise_mouse = want;
  write(want ? "\x1b[?1016h" : "\x1b[?1016l");
}

void TerminalSession::writeBlocking(const std::string & bytes) {
  if (fd_ < 0 || bytes.empty()) return;
  // A signal's restoration must not land inside a half-written sequence.
  sigset_t block = blockedWhileWriting(), old;
  pthread_sigmask(SIG_BLOCK, &block, &old);
  size_t done = 0;
  while (done < bytes.size()) {
    ssize_t n = ::write(fd_, bytes.data() + done, bytes.size() - done);
    if (n > 0) {
      done += static_cast<size_t>(n);
    } else if (n < 0 && (errno == EAGAIN || errno == EINTR)) {
      struct pollfd p = {fd_, POLLOUT, 0};
      ::poll(&p, 1, 100);
    } else {
      break;  // the terminal is gone
    }
  }
  pthread_sigmask(SIG_SETMASK, &old, nullptr);
}

void TerminalSession::write(const std::string & bytes) {
  if (fd_ < 0 || bytes.empty()) return;
  out_.append(bytes);
  flushOutput();
}

void TerminalSession::flushOutput() {
  if (fd_ < 0) return;
  bool ok = out_.flush(fd_);
  auto now = std::chrono::steady_clock::now();
  // A frame is sent when its last byte has been taken by the descriptor.
  while (!marks_.empty() && marks_.front().end <= out_.accepted()) {
    Mark mark = marks_.front();
    marks_.pop_front();
    if (mark.probe) pacer_.onProbeSent(now);
    else pacer_.onFrameSent(mark.size, now);
  }
  if (!ok) {
    // The terminal is gone; the read side reports the end.
    out_.discard();
    marks_.clear();
  }
}

void TerminalSession::drainOutput(std::chrono::milliseconds limit) {
  auto end = std::chrono::steady_clock::now() + limit;
  while (!out_.empty() && fd_ >= 0) {
    auto now = std::chrono::steady_clock::now();
    if (now >= end) break;
    struct pollfd p = {fd_, POLLOUT, 0};
    ::poll(&p, 1, static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(end - now).count()) + 1);
    flushOutput();
  }
  // What did not go is given up: the restoration matters more.
  out_.discard();
  marks_.clear();
}

void TerminalSession::setFrameHandler(FrameHandler handler) {
  frame_handler_ = std::move(handler);
  pacing_ = static_cast<bool>(frame_handler_);
}

std::optional<TerminalSession::TimePoint> TerminalSession::nextDeadline() const {
  std::optional<TimePoint> deadline = parser_.nextDeadline();
  // While the queue holds bytes the write source wakes the loop for the next
  // frame; the clock is still wanted for a stall.
  if (pacing_) {
    if (auto t = pacer_.deadline(out_.empty())) deadline = deadline ? std::min(*deadline, *t) : *t;
  }
  return deadline;
}

PacerStats TerminalSession::pacerStats() const {
  PacerStats stats = pacer_.stats(std::chrono::steady_clock::now());
  stats.kitty_errors = kitty_errors_;
  stats.last_kitty_error = last_kitty_error_;
  return stats;
}

std::string TerminalSession::fenceBytes() {
  if (caps_.dec_cursor_report) {
    parser_.expectDecCursorReport();
    return "\x1b[?6n";
  }
  // The plain query's reply looks like F3 with modifiers when the cursor is on
  // row 1: park it lower first.
  parser_.expectCursorReport();
  if (rows_ < 2) return "\x1b[6n";
  encoder_.forgetCursor();
  return "\x1b[" + std::to_string(rows_) + ";1H\x1b[6n";
}

// One line a second, key=value, for measuring what a link carries (phase 0 of the link
// classes): the frames and bytes sent in the second (picture data, placement, the rest),
// the pacer's bandwidth and RTT, and each picture size uploaded as WxH:count/bytes.
void TerminalSession::logLinkStats(std::chrono::steady_clock::time_point now) {
  if (!link_log_) return;
  if (link_log_last_ == std::chrono::steady_clock::time_point{}) link_log_last_ = now;
  double dt = std::chrono::duration<double>(now - link_log_last_).count();
  if (dt < 1.0) return;
  PacerStats s = pacerStats();
  std::fprintf(link_log_, "t=%.1f frames=%zu upload=%zu placement=%zu other=%zu bandwidth=%.0f rtt_ms=%.1f in_flight=%zu pics=", dt,
               s.frames - link_log_frames_, s.totals.upload - link_log_mark_.upload, s.totals.placement - link_log_mark_.placement,
               s.totals.other - link_log_mark_.other, s.bandwidth, s.rtt_min_us / 1000.0, s.bytes_in_flight);
  bool first = true;
  for (const auto & [size, stat] : link_log_pictures_) {
    std::fprintf(link_log_, "%s%dx%d:%zu/%zu", first ? "" : ",", size.first, size.second, stat.uploads, stat.bytes);
    first = false;
  }
  std::fprintf(link_log_, "%s\n", first ? "-" : "");
  std::fflush(link_log_);
  link_log_last_ = now;
  link_log_frames_ = s.frames;
  link_log_mark_ = s.totals;
  link_log_pictures_.clear();
}

namespace {

std::string formatRate(double bytes_per_sec) {
  char buf[32];
  if (bytes_per_sec >= 1e6) std::snprintf(buf, sizeof buf, "%.1f MB/s", bytes_per_sec / 1e6);
  else if (bytes_per_sec >= 1e3) std::snprintf(buf, sizeof buf, "%.0f kB/s", bytes_per_sec / 1e3);
  else std::snprintf(buf, sizeof buf, "%.0f B/s", bytes_per_sec);
  return buf;
}

}  // namespace

void TerminalSession::updateLink(std::chrono::steady_clock::time_point now) {
  constexpr auto kInterval = std::chrono::seconds(1);
  constexpr auto kWarmUp = std::chrono::seconds(4);
  if (link_last_update_ == std::chrono::steady_clock::time_point{}) link_last_update_ = now;
  if (now - link_last_update_ < kInterval) return;
  double span = std::chrono::duration<double>(now - link_last_update_).count();
  double blocked = link_blocked_seconds_ / span;
  link_blocked_seconds_ = 0;
  link_last_update_ = now;
  PacerStats s = pacerStats();
  if (!s.fences) {
    // No answers to the cursor queries: the pacer sends at a fixed rate and measures nothing.
    link_.setDescription("no cursor reports, " + std::to_string(static_cast<int>(pacer_config_.fallback_fps)) + " Hz");
    link_.update(now, LinkClass::HZ_5, LinkClass::HZ_5);
    return;
  }
  if (s.stalls != link_stalls_seen_) {
    link_stalls_seen_ = s.stalls;
    link_stalled_at_ = now;
  }
  bool settled = now - link_opened_ >= kWarmUp && s.acks >= 12 && now - link_stalled_at_ >= kWarmUp;
  if (!settled) return;
  std::string rtt;
  if (s.rtt_min_us >= 1000) rtt = "RTT " + std::to_string(static_cast<int>(s.rtt_min_us / 1000.0 + 0.5)) + " ms";
  else if (s.rtt_min_us > 0) rtt = "RTT <1 ms";
  if (s.bandwidth <= 0 || blocked < 0.4) {
    // The link did not hold frames back, so the estimate is rough (bursts the buffers took, and what
    // the application chose to send): shown with a tilde, and the class stays where it is.
    std::string text = s.bandwidth > 0 ? "~" + formatRate(s.bandwidth) : "not limiting";
    if (!rtt.empty()) text += ", " + rtt;
    link_.setDescription(std::move(text));
    link_.setAvailable(true);
    return;
  }
  std::string text = formatRate(s.bandwidth);
  if (!rtt.empty()) text += ", " + rtt;
  link_.setDescription(std::move(text));
  link_.update(now, linkClassForBandwidth(link_thresholds_, s.bandwidth), linkClassForBandwidth(link_thresholds_, s.bandwidth, link_.classifierConfig().up_margin));
}

void TerminalSession::pumpFrame() {
  if (!pacing_ || fd_ < 0 || !open_) return;
  auto now = std::chrono::steady_clock::now();
  logLinkStats(now);
  // The time since the last turn counts as link-bound if the link is what holds the next frame back.
  // The state the last turn left held for the time since: the loop sleeps through a stretch that the
  // link holds a frame back, and wakes when it ends.
  if (link_was_blocked_ && link_last_pump_ != std::chrono::steady_clock::time_point{}) {
    link_blocked_seconds_ += std::min(1.0, std::chrono::duration<double>(now - link_last_pump_).count());
  }
  link_was_blocked_ = pacer_.linkBound(now) || (!out_.empty() && pacer_.dirty());
  link_last_pump_ = now;
  updateLink(now);
  pacer_.tick(now);
  late_replies_ += pacer_.takeLateReplies();
  if (!out_.empty() || !pacer_.mayStart(now)) return;
  pacer_.onFrameStart(now);
  in_frame_ = true;
  frame_presented_ = false;
  frame_handler_(std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count());
  in_frame_ = false;
  // A frame presented outside the turn is sent now if the handler had none.
  if (!frame_presented_ && held_) {
    Held held = std::move(*held_);
    held_.reset();
    sendFrame(held.frame, held.cursor, held.images, held.layers);
  }
}

void TerminalSession::present(const CellBuffer & frame, const CursorRequest & cursor, const std::vector<ImagePlacement> & images,
                              const FrameInfo * info) {
  if (info) {
    cursor_areas_ = info->mouse_cursors;
    updateMouseCursor();
  }
  static const std::vector<BackdropLayer> kNoLayers;
  const std::vector<BackdropLayer> & layers = info ? info->backdrops : kNoLayers;
  if (!pacing_ || in_frame_) {
    held_.reset();
    sendFrame(frame, cursor, images, layers);
    return;
  }
  // Presented outside a turn of the pacer's (a status message, say): the
  // latest one is kept and sent when a frame may start.
  held_ = Held{frame, cursor, images, layers};
  pacer_.requestFrame();
}

void TerminalSession::sendFrame(const CellBuffer & frame, const CursorRequest & cursor, const std::vector<ImagePlacement> & images,
                                const std::vector<BackdropLayer> & layers) {
  frame_presented_ = true;
  std::string body;
  // A backdrop is made of what the terminal can show; the frame and its
  // pictures are copied only then.
  if (!layers.empty() || backdrops_.holdsState()) {
    CellBuffer shown = frame;
    std::vector<ImagePlacement> placements = images;
    backdrops_.apply(shown, placements, layers, caps_.display);
    std::string pixels = images_.encode(shown, placements, encoder_.willClear(shown));
    body = encoder_.encode(shown, cursor, pixels);
  } else {
    std::string pixels = images_.encode(frame, images, encoder_.willClear(frame));
    body = encoder_.encode(frame, cursor, pixels);
  }
  if (body.empty()) return;  // nothing changed: no bytes, no frame
  if (link_log_) {
    for (const auto & [dims, stat] : images_.takePictureStats()) {
      link_log_pictures_[dims].uploads += stat.uploads;
      link_log_pictures_[dims].bytes += stat.bytes;
    }
  }
  FrameBytes size;
  size.upload = images_.lastSizes().upload;
  size.placement = images_.lastSizes().placement;
  size.other = body.size() - std::min(body.size(), size.upload + size.placement);
  if (!pacing_) {
    out_.append(body);
    flushOutput();
    return;
  }
  if (pacer_.fencesEnabled()) {
    if (pacer_.needsProbe()) {
      out_.append(fenceBytes());
      marks_.push_back({out_.queued(), {}, true});
    }
    out_.append(body);
    std::string fence = fenceBytes();
    size.other += fence.size();
    out_.append(fence);
  } else {
    out_.append(body);
  }
  marks_.push_back({out_.queued(), size, false});
  flushOutput();
}

void TerminalSession::wake() {
  if (wake_write_ < 0) return;
  char c = 'w';
  ssize_t n = ::write(wake_write_, &c, 1);
  (void)n;
}

void TerminalSession::updateMouseCursor() {
  MouseCursor cursor = mouseCursorAt(cursor_areas_, pointer_row_, pointer_col_);
  if (cursor == shown_cursor_) return;
  shown_cursor_ = cursor;
  write(std::string("\x1b]22;") + mouseCursorName(cursor) + "\x1b\\");
}

std::vector<BackendEvent> TerminalSession::poll() {
  std::vector<BackendEvent> out;
  for (const TermEvent & e : pollTerm()) {
    if (e.type == TermEvent::Type::MOUSE) {
      pointer_row_ = e.y;
      pointer_col_ = e.x;
      updateMouseCursor();
      // Hovering is for the pointer's shape alone.
      if (e.kind == TermEvent::Kind::MOVE) continue;
    }
    if (auto event = toBackendEvent(e)) out.push_back(std::move(*event));
  }
  return out;
}

std::vector<TermEvent> TerminalSession::pollTerm() {
  std::vector<TermEvent> out(pending_.begin(), pending_.end());
  pending_.clear();
  if (!open_) return out;
  char buf[4096];
  while (::read(wake_read_, buf, sizeof buf) > 0) { }
  if (g.continued.exchange(false)) {
    // Back from a suspend: the modes may have been reset meanwhile.
    struct termios t;
    if (tcgetattr(fd_, &t) == 0) {
      t.c_lflag &= ~static_cast<tcflag_t>(ICANON | ECHO | IEXTEN);
      if (!line_signals_) t.c_lflag &= ~static_cast<tcflag_t>(ISIG);
      t.c_iflag &= ~static_cast<tcflag_t>(ICRNL | IXON);
      tcsetattr(fd_, TCSANOW, &t);
    }
    write("\x1b[?1049h" + std::string(caps_.kitty_keyboard ? "\x1b[>u\x1b[=27u" : "") + setup_);
    g.resized = true;
  }
  if (g.resized.exchange(false)) {
    readSize();
    encoder_.invalidate();
    write("\x1b[16t");  // the cell size may have changed with the font
    TermEvent e;
    e.type = TermEvent::Type::RESIZE;
    e.rows = rows_;
    e.cols = cols_;
    out.push_back(e);
  }
  while (!ended_) {
    ssize_t n = ::read(fd_, buf, sizeof buf);
    if (n > 0) {
      parser_.feed(buf, static_cast<size_t>(n), std::chrono::steady_clock::now());
      continue;
    }
    if (n < 0 && errno == EINTR) continue;
    if (n < 0 && errno == EAGAIN) break;
    parser_.finish();  // end of input or hang-up
    ended_ = true;
  }
  parser_.expire(std::chrono::steady_clock::now());
  while (parser_.hasEvents()) {
    TermEvent e = parser_.pop();
    if (e.type == TermEvent::Type::REPLY) {
      if (e.reply == ReplyKind::DEC_CURSOR_POSITION || e.reply == ReplyKind::CURSOR_POSITION) {
        // A fence's reply: the oldest frame in flight has been taken in.
        if (!pacing_) continue;
        if (late_replies_ > 0) late_replies_--;
        else pacer_.onAck(e.at);
        continue;
      }
      if (e.reply == ReplyKind::KITTY_GRAPHICS) {
        std::string error = images_.handleKittyReply(e.text);
        if (!error.empty()) {
          kitty_errors_++;
          last_kitty_error_ = std::move(error);
        }
        continue;
      }
      if (e.reply == ReplyKind::WINDOW_REPORT && e.params.size() == 3 && e.params[0] == 6 && e.params[1] > 0) {
        caps_.display.cell_px_height = e.params[1];
        caps_.display.cell_px_width = e.params[2];
        caps_.display.display_scale = estimateDisplayScale(caps_.display.cell_px_height);
        ImageEncoder::Profile profile = imageProfile();
        write(images_.clear());
        images_ = ImageEncoder(profile);
        updatePixelMouse();
      }
      continue;
    }
    out.push_back(std::move(e));
  }
  return out;
}

}  // namespace cellui::term
