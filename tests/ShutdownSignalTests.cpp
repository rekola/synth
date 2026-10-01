#include "TestFramework.h"

#include "../src/util/ShutdownSignal.h"

#include <csignal>

using namespace std;

TEST(shutdown_flag_starts_clear_and_can_be_requested) {
  clearShutdownRequest();
  CHECK(!shutdownRequested());
  requestShutdown();
  CHECK(shutdownRequested());
  clearShutdownRequest();
  CHECK(!shutdownRequested());
}

// Each of the three signals a service manager or terminal sends to ask a
// process to stop must set the flag instead of killing the process.
TEST(shutdown_signals_set_the_flag) {
  installShutdownSignalHandlers();
  for (int sig : { SIGINT, SIGTERM, SIGHUP }) {
    clearShutdownRequest();
    raise(sig);
    CHECK(shutdownRequested());
  }
  clearShutdownRequest();
}
