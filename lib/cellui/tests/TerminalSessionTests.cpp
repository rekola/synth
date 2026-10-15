#include "TestFramework.h"

#include <cellui/term/TerminalSession.h>

#include <atomic>
#include <fcntl.h>
#include <fstream>
#include <mutex>
#include <poll.h>
#include <sstream>
#include <sys/ioctl.h>
#include <termios.h>
#include <thread>
#include <sys/wait.h>
#include <unistd.h>

using namespace cellui;
using namespace cellui::term;

namespace {

std::string readFixture(const std::string & name) {
  std::ifstream in(std::string(CELLUI_TEST_DIR) + "/fixtures/startup/" + name + ".bin", std::ios::binary);
  std::stringstream s;
  s << in.rdbuf();
  return s.str();
}

// A pseudo-terminal whose far end plays a recorded terminal: it answers the
// start-up queries with that terminal's replies and records the rest.
class FakeTerminal {
 public:
  explicit FakeTerminal(std::string replies) : replies_(std::move(replies)) {
    master_ = posix_openpt(O_RDWR | O_NOCTTY);
    grantpt(master_);
    unlockpt(master_);
    slave_ = ::open(ptsname(master_), O_RDWR | O_NOCTTY);
    struct winsize ws = {30, 100, 1000, 600};
    ioctl(master_, TIOCSWINSZ, &ws);
    reader_ = std::thread([this] { run(); });
  }
  ~FakeTerminal() {
    stop_ = true;
    reader_.join();
    ::close(slave_);
    ::close(master_);
  }
  int slave() const { return slave_; }
  void type(const std::string & bytes) {
    ssize_t n = ::write(master_, bytes.data(), bytes.size());
    (void)n;
  }
  std::string output() {
    std::lock_guard<std::mutex> lock(mutex_);
    return output_;
  }
  // Waits until the output contains `s`, up to a second.
  bool waitFor(const std::string & s) {
    for (int i = 0; i < 100; i++) {
      if (output().find(s) != std::string::npos) return true;
      usleep(10000);
    }
    return false;
  }

 private:
  void run() {
    bool answered = false;
    char buf[4096];
    while (!stop_) {
      struct pollfd p = {master_, POLLIN, 0};
      if (::poll(&p, 1, 10) <= 0) continue;
      ssize_t n = ::read(master_, buf, sizeof buf);
      if (n <= 0) continue;
      bool sentinel;
      {
        std::lock_guard<std::mutex> lock(mutex_);
        output_.append(buf, static_cast<size_t>(n));
        sentinel = output_.find("\x1b[c") != std::string::npos;
      }
      if (sentinel && !answered) {
        answered = true;
        type(replies_);
      }
    }
  }

  std::string replies_;
  int master_ = -1, slave_ = -1;
  std::thread reader_;
  std::atomic<bool> stop_{false};
  std::mutex mutex_;
  std::string output_;
};

SessionOptions testOptions(int fd) {
  SessionOptions o;
  o.fd = fd;
  o.install_signal_handlers = false;
  o.env = Environment();
  o.env.term = "xterm-256color";
  return o;
}

std::vector<TermEvent> pollFor(TerminalSession & s, size_t count) {
  std::vector<TermEvent> got;
  for (int i = 0; i < 100 && got.size() < count; i++) {
    struct pollfd p = {s.inputFd(), POLLIN, 0};
    ::poll(&p, 1, 10);
    for (auto & e : s.pollTerm()) got.push_back(e);
  }
  return got;
}

}  // namespace

TEST(session_negotiates_draws_reads_and_restores) {
  FakeTerminal term(readFixture("kitty"));
  struct termios before;
  tcgetattr(term.slave(), &before);
  std::string error;
  auto session = TerminalSession::open(testOptions(term.slave()), &error);
  CHECK(session != nullptr);
  if (!session) return;
  CHECK(session->capabilities().answered);
  CHECK(session->capabilities().terminal == "kitty");
  CHECK(session->rows() == 30 && session->cols() == 100);
  CHECK(session->capabilities().display.cell_px_height == 20);
  CHECK(term.waitFor("\x1b[?2004h"));
  std::string out = term.output();
  CHECK(out.compare(0, 8, "\x1b[?1049h") == 0);

  struct termios during;
  tcgetattr(term.slave(), &during);
  CHECK(!(during.c_lflag & ICANON) && !(during.c_lflag & ECHO) && !(during.c_lflag & ISIG) && !(during.c_iflag & IXON));

  // The recording ends with a key typed after the replies.
  term.type("\x1b[97;5u");
  auto events = pollFor(*session, 2);
  CHECK(events.size() == 2);
  if (events.size() == 2) {
    CHECK(describe(events[0]) == "n");
    CHECK(describe(events[1]) == "Ctrl+A press");
  }

  CellBuffer frame(30, 100);
  frame.write(0, 0, "hello", {});
  session->present(frame);
  CHECK(term.waitFor("hello"));

  std::string restore = session->restoreSequence();
  CHECK(restore.find("\x1b[<u") < restore.find("\x1b[?1049l"));
  session->close();
  CHECK(term.waitFor("\x1b[?1049l"));
  struct termios after;
  tcgetattr(term.slave(), &after);
  CHECK(after.c_lflag == before.c_lflag && after.c_iflag == before.c_iflag);
}

TEST(session_does_not_wait_for_a_silent_terminal_past_the_deadline) {
  FakeTerminal term("");
  auto start = std::chrono::steady_clock::now();
  auto session = TerminalSession::open(testOptions(term.slave()));
  auto waited = std::chrono::steady_clock::now() - start;
  CHECK(session != nullptr);
  if (!session) return;
  CHECK(!session->capabilities().answered);
  CHECK(waited < std::chrono::milliseconds(1500));
  CHECK(session->restoreSequence().find("\x1b[>4m") != std::string::npos);
}

TEST(session_refuses_what_is_not_a_terminal) {
  int fds[2];
  CHECK(pipe(fds) == 0);
  std::string error;
  CHECK(TerminalSession::open(testOptions(fds[0]), &error) == nullptr);
  CHECK(!error.empty());
  ::close(fds[0]);
  ::close(fds[1]);
  SessionOptions dumb = testOptions(-1);
  dumb.env.term = "dumb";
  CHECK(TerminalSession::open(dumb) == nullptr);
}

TEST(session_wake_makes_the_wake_descriptor_readable) {
  FakeTerminal term(readFixture("minimal"));
  auto session = TerminalSession::open(testOptions(term.slave()));
  CHECK(session != nullptr);
  if (!session) return;
  struct pollfd p = {session->wakeFd(), POLLIN, 0};
  CHECK(::poll(&p, 1, 0) == 0);
  session->wake();
  CHECK(::poll(&p, 1, 0) == 1);
  session->pollTerm();
  CHECK(::poll(&p, 1, 0) == 0);
}

TEST(session_restores_the_terminal_when_killed) {
  FakeTerminal term(readFixture("xterm"));
  pid_t child = fork();
  if (child == 0) {
    SessionOptions o = testOptions(term.slave());
    o.install_signal_handlers = true;
    auto session = TerminalSession::open(o);
    if (!session) _exit(2);
    raise(SIGTERM);
    _exit(3);
  }
  int status = 0;
  waitpid(child, &status, 0);
  CHECK(WIFSIGNALED(status) && WTERMSIG(status) == SIGTERM);
  CHECK(term.waitFor("\x1b[?1049l"));
  std::string out = term.output();
  CHECK(out.find("\x1b[?2004l") != std::string::npos && out.find("\x1b[>4m") != std::string::npos);
  struct termios t;
  tcgetattr(term.slave(), &t);
  CHECK((t.c_lflag & ICANON) && (t.c_lflag & ECHO));
}
