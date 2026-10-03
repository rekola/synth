#ifndef _DAEMON_H_
#define _DAEMON_H_

#include <string>

// Detaches the process from its terminal (fork + setsid), points stdin at
// /dev/null and stdout/stderr at `log_file` (appended; /dev/null if empty),
// and writes the daemon's pid to `pid_file` if given. The working directory
// is left alone so relative song paths keep working.
//
// Returns in the daemon process only. The original process waits for the
// daemon to call daemonStarted() and then exits 0, or exits 1 if the daemon
// died first - so a shell or init script learns whether startup worked.
// Must run before any thread is created.
void daemonize(const std::string & log_file, const std::string & pid_file);

// Tells the waiting original process startup succeeded.
void daemonStarted();

#endif
