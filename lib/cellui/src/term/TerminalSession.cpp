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
  for (int s : kTerminationSignals) sigaddset(&set, s);
  return set;
}

void setNonblocking(int fd) {
  int flags = fcntl(fd, F_GETFL);
  if (flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

}  // namespace

std::unique_ptr<TerminalSession> TerminalSession::open(SessionOptions options, std::string * error) {
  std::unique_ptr<TerminalSession> session(new TerminalSession());
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
  line_signals_ = options.line_signals;
  mouse_ = options.mouse;
  parser_.setGrace(options.grace);

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
    if (ws.ws_ypixel > 0 && ws.ws_xpixel > 0 && caps_.cell_px_height == 0) {
      caps_.cell_px_height = ws.ws_ypixel / ws.ws_row;
      caps_.cell_px_width = ws.ws_xpixel / ws.ws_col;
    }
  }
  parser_.setScreenSize(rows_, cols_);
}

void TerminalSession::negotiate(const Environment & env) {
  Discovery discovery(env);
  // The alternate screen first, so whatever a terminal fails to consume
  // lands there.
  parser_.expectCursorReport();
  write("\x1b[?1049h" + discovery.queries());
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
  discovery.setWindowCellPixels(caps_.cell_px_height, caps_.cell_px_width);
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
  encoder_ = FrameEncoder(EncoderProfile::from(caps_));
  images_ = ImageEncoder(ImageEncoder::Profile::from(caps_));
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
  if (mouse_) setup_ += "\x1b[?1002;1006h";
  if (caps_.pixels == PixelProtocol::KITTY) setup_ += "\x1b_Ga=d,q=2\x1b\\";
  write(setup_);
  encoder_.invalidate();

  restore_ += "\x1b[?2026l";
  restore_ += "\x1b[0m";
  if (mouse_) restore_ += "\x1b[?1002;1006l";
  restore_ += "\x1b[?2004l\x1b[?7h\x1b[?12l\x1b[?25h";
  restore_ += caps_.kitty_keyboard ? "\x1b[<u" : "\x1b[<u\x1b[>4m\x1b[>2m";
  if (caps_.pixels == PixelProtocol::KITTY) restore_ += "\x1b_Ga=d,q=2\x1b\\";
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
    write(restore_);
    tcsetattr(fd_, TCSAFLUSH, &g.saved);
  }
  g.fd = -1;
  g.wake_write = -1;
  ::close(wake_read_);
  ::close(wake_write_);
  wake_read_ = wake_write_ = -1;
  if (own_fd_) ::close(fd_);
  fd_ = -1;
}

void TerminalSession::write(const std::string & bytes) {
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

void TerminalSession::present(const CellBuffer & frame, const CursorRequest & cursor, const std::vector<ImagePlacement> & images) {
  std::string pixels = images_.encode(frame, images, encoder_.willClear(frame));
  write(encoder_.encode(frame, cursor, pixels));
}

void TerminalSession::wake() {
  if (wake_write_ < 0) return;
  char c = 'w';
  ssize_t n = ::write(wake_write_, &c, 1);
  (void)n;
}

std::vector<TermEvent> TerminalSession::poll() {
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
      if (e.reply == ReplyKind::WINDOW_REPORT && e.params.size() == 3 && e.params[0] == 6 && e.params[1] > 0) {
        caps_.cell_px_height = e.params[1];
        caps_.cell_px_width = e.params[2];
        caps_.display_scale = estimateDisplayScale(caps_.cell_px_height);
        ImageEncoder::Profile profile = ImageEncoder::Profile::from(caps_);
        write(images_.clear());
        images_ = ImageEncoder(profile);
        parser_.setPixelMouse(false, e.params[1], e.params[2]);
      }
      continue;
    }
    out.push_back(std::move(e));
  }
  return out;
}

}  // namespace cellui::term
