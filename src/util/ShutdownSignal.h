#ifndef _SHUTDOWNSIGNAL_H_
#define _SHUTDOWNSIGNAL_H_

// Process-wide "please shut down" flag, set by SIGINT/SIGTERM/SIGHUP (on
// Windows by Ctrl-C, Ctrl-Break and the console closing, which has no SIGHUP).
// The handlers are installed without SA_RESTART, so a blocking poll() in a UI
// main loop returns EINTR right away and gets to see the flag; a loop that
// waits on a WaitSet with a timeout sees it within that timeout.

// Installs the handlers (idempotent).
void installShutdownSignalHandlers();

bool shutdownRequested();

// Sets the flag the way a signal would - for tests and for code that needs
// to stop the main loop programmatically.
void requestShutdown();

// Clears the flag (tests).
void clearShutdownRequest();

#endif
