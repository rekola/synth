#include "ShutdownSignal.h"

#include <csignal>
#include <initializer_list>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

volatile std::sig_atomic_t shutdown_flag = 0;

void onSignal(int) {
  shutdown_flag = 1;
}

#ifdef _WIN32
// Ctrl-C, Ctrl-Break, the console window being closed, logoff and system
// shutdown arrive on a thread of their own, not through signal().
BOOL WINAPI
onConsoleControl(DWORD) {
  shutdown_flag = 1;
  return TRUE;
}
#endif

} // namespace

void
installShutdownSignalHandlers() {
#ifdef _WIN32
  for (int sig : {SIGINT, SIGTERM}) std::signal(sig, onSignal);
  SetConsoleCtrlHandler(onConsoleControl, TRUE);
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
