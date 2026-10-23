#include "TestFramework.h"

#include <cellui/BackendChoice.h>

#include <cstdlib>

using namespace cellui;

TEST(backend_names_parse_and_nothing_else_does) {
  BackendKind kind = BackendKind::TERMINAL;
  CHECK(parseBackendKind("auto", kind) && kind == BackendKind::AUTO);
  CHECK(parseBackendKind("terminal", kind) && kind == BackendKind::TERMINAL);
  CHECK(parseBackendKind("raster", kind) && kind == BackendKind::RASTER);
  kind = BackendKind::RASTER;
  // "window" was the raster backend's old name; every window is a window.
  CHECK(!parseBackendKind("window", kind) && kind == BackendKind::RASTER);
  CHECK(!parseBackendKind("", kind));
  CHECK(!parseBackendKind("Terminal", kind));
  CHECK(std::string(backendName(BackendKind::RASTER)) == "raster");
}

TEST(backend_auto_prefers_the_window_when_it_is_built_and_a_display_is_there) {
  CHECK((backendCandidates(BackendKind::AUTO, {true, true}) == std::vector{BackendKind::RASTER, BackendKind::TERMINAL}));
  // No display (a remote login, a container): the terminal.
  CHECK((backendCandidates(BackendKind::AUTO, {true, false}) == std::vector{BackendKind::TERMINAL}));
  // A build without the window.
  CHECK((backendCandidates(BackendKind::AUTO, {false, true}) == std::vector{BackendKind::TERMINAL}));
  CHECK((backendCandidates(BackendKind::AUTO, {false, false}) == std::vector{BackendKind::TERMINAL}));
}

TEST(backend_a_named_choice_is_the_only_candidate_even_when_unavailable) {
  CHECK((backendCandidates(BackendKind::TERMINAL, {true, true}) == std::vector{BackendKind::TERMINAL}));
  CHECK((backendCandidates(BackendKind::RASTER, {true, true}) == std::vector{BackendKind::RASTER}));
  // It is for the caller to say why it could not start; nothing falls through.
  CHECK((backendCandidates(BackendKind::RASTER, {false, false}) == std::vector{BackendKind::RASTER}));
}

#ifndef _WIN32
TEST(backend_probe_sees_a_display_from_the_environment) {
  const char * wayland = std::getenv("WAYLAND_DISPLAY");
  const char * x11 = std::getenv("DISPLAY");
  std::string saved_wayland = wayland ? wayland : "", saved_x11 = x11 ? x11 : "";
  unsetenv("WAYLAND_DISPLAY");
  unsetenv("DISPLAY");
  CHECK(!probeBackendEnvironment().display);
  setenv("DISPLAY", ":0", 1);
  CHECK(probeBackendEnvironment().display);
  unsetenv("DISPLAY");
  setenv("WAYLAND_DISPLAY", "wayland-0", 1);
  CHECK(probeBackendEnvironment().display);
  setenv("WAYLAND_DISPLAY", "", 1);
  CHECK(!probeBackendEnvironment().display);  // set but empty is not a display
  if (wayland) setenv("WAYLAND_DISPLAY", saved_wayland.c_str(), 1);
  else unsetenv("WAYLAND_DISPLAY");
  if (x11) setenv("DISPLAY", saved_x11.c_str(), 1);
  else unsetenv("DISPLAY");
}
#endif
