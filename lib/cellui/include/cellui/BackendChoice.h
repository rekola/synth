#pragma once

#include <string_view>
#include <vector>

namespace cellui {

// What draws the UI. One switch names it (`--ui`), by how it draws and not by
// the window it sits in: the terminal's escape sequences, or the platform's
// own 2D window (the GTK one on Linux).
enum class BackendKind {
  AUTO,      // the best the machine can run
  TERMINAL,
  RASTER,
};

const char * backendName(BackendKind kind);

// "auto", "terminal" or "raster"; false for anything else.
bool parseBackendKind(std::string_view name, BackendKind & kind);

// What the machine offers, known before anything is shown.
struct BackendEnvironment {
  bool raster_built = false;  // this build has the platform's 2D window
  bool display = false;       // a display server can be reached
};

BackendEnvironment probeBackendEnvironment();

// The backends to try, best first. A named backend is the only candidate, so
// one that cannot start is an error and not a quiet change of look; `auto`
// lists the window when the build has it and a display is there, then the
// terminal. The choice is made once, at start: nothing switches while the
// program runs.
std::vector<BackendKind> backendCandidates(BackendKind requested, const BackendEnvironment & environment);

}  // namespace cellui
