#include "ShutdownSignal.h"

#include <csignal>
#include <initializer_list>

namespace {

volatile std::sig_atomic_t shutdown_flag = 0;

void onSignal(int) {
  shutdown_flag = 1;
}

} // namespace

void
installShutdownSignalHandlers() {
#ifdef _WIN32
  for (int sig : {SIGINT, SIGTERM}) std::signal(sig, onSignal);
#else
  struct sigaction sa {};
  sa.sa_handler = onSignal;
  sigemptyset(&sa.sa_mask);
  for (int sig : { SIGINT, SIGTERM, SIGHUP }) sigaction(sig, &sa, nullptr);
#endif
}

bool
shutdownRequested() {
  return shutdown_flag != 0;
}

void
requestShutdown() {
  shutdown_flag = 1;
}

void
clearShutdownRequest() {
  shutdown_flag = 0;
}
