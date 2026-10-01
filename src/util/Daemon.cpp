#include "Daemon.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>

namespace {

int ready_fd = -1; // daemon's end of the startup pipe

void
failStartup(const char * what) {
  fprintf(stderr, "daemon: %s: %s\n", what, strerror(errno));
  exit(1);
}

} // namespace

void
daemonize(const std::string & log_file, const std::string & pid_file) {
  int fds[2];
  if (pipe(fds) != 0) failStartup("pipe");

  // Anything buffered now would otherwise be written by both processes.
  fflush(nullptr);
  pid_t pid = fork();
  if (pid < 0) failStartup("fork");

  if (pid > 0) {
    close(fds[1]);
    char byte;
    ssize_t n;
    while ((n = read(fds[0], &byte, 1)) < 0 && errno == EINTR) { }
    if (n != 1) {
      fprintf(stderr, "daemon: failed to start%s%s\n", log_file.empty() ? "" : ", see ", log_file.c_str());
      _exit(1);
    }
    _exit(0);
  }

  close(fds[0]);
  ready_fd = fds[1];
  if (setsid() < 0) failStartup("setsid");

  int log_fd = open(log_file.empty() ? "/dev/null" : log_file.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
  if (log_fd < 0) failStartup("can't open log file");
  int null_fd = open("/dev/null", O_RDONLY);
  if (null_fd < 0) failStartup("can't open /dev/null");
  dup2(null_fd, STDIN_FILENO);
  dup2(log_fd, STDOUT_FILENO);
  dup2(log_fd, STDERR_FILENO);
  if (null_fd > STDERR_FILENO) close(null_fd);
  if (log_fd > STDERR_FILENO) close(log_fd);

  if (!pid_file.empty()) {
    FILE * f = fopen(pid_file.c_str(), "w");
    if (!f) failStartup("can't write pid file");
    fprintf(f, "%d\n", static_cast<int>(getpid()));
    fclose(f);
  }
}

void
daemonStarted() {
  if (ready_fd < 0) return;
  char byte = 1;
  ssize_t ignored = write(ready_fd, &byte, 1);
  (void)ignored;
  close(ready_fd);
  ready_fd = -1;
}
