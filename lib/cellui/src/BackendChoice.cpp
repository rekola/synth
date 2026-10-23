#include <cellui/BackendChoice.h>

#include <cstdlib>

namespace cellui {

const char * backendName(BackendKind kind) {
  switch (kind) {
    case BackendKind::AUTO: return "auto";
    case BackendKind::TERMINAL: return "terminal";
    case BackendKind::RASTER: return "raster";
  }
  return "";
}

bool parseBackendKind(std::string_view name, BackendKind & kind) {
  for (BackendKind k : {BackendKind::AUTO, BackendKind::TERMINAL, BackendKind::RASTER}) {
    if (name == backendName(k)) {
      kind = k;
      return true;
    }
  }
  return false;
}

BackendEnvironment probeBackendEnvironment() {
  BackendEnvironment environment;
#ifdef CELLUI_HAS_GTK
  environment.raster_built = true;
#endif
#if defined(_WIN32)
  environment.display = true;
#else
  auto set = [](const char * name) {
    const char * value = std::getenv(name);
    return value && *value;
  };
  environment.display = set("WAYLAND_DISPLAY") || set("DISPLAY");
#endif
  return environment;
}

std::vector<BackendKind> backendCandidates(BackendKind requested, const BackendEnvironment & environment) {
  if (requested != BackendKind::AUTO) return {requested};
  std::vector<BackendKind> candidates;
  if (environment.raster_built && environment.display) candidates.push_back(BackendKind::RASTER);
  candidates.push_back(BackendKind::TERMINAL);
  return candidates;
}

}  // namespace cellui
