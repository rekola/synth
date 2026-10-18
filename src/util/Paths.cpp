#include "Paths.h"

#include "generated/InstallPaths.h"

#include <cstdlib>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>

#include <cstdint>
#endif
#endif

namespace fs = std::filesystem;

namespace {

fs::path
fromEnvironment(const char * name) {
  auto value = std::getenv(name);
  return value && *value ? fs::path(value) : fs::path();
}

} // namespace

namespace paths {

fs::path
configDir() {
#ifdef _WIN32
  auto appdata = fromEnvironment("APPDATA");
  return appdata.empty() ? appdata : appdata / "synth";
#else
  auto xdg = fromEnvironment("XDG_CONFIG_HOME");
  if (!xdg.empty()) return xdg / "synth";
  auto home = fromEnvironment("HOME");
  return home.empty() ? home : home / ".config" / "synth";
#endif
}

fs::path
userDataDir() {
#ifdef _WIN32
  auto local = fromEnvironment("LOCALAPPDATA");
  return local.empty() ? local : local / "synth";
#else
  auto home = fromEnvironment("HOME");
  return home.empty() ? home : home / ".local" / "share";
#endif
}

fs::path
installDataDir() {
#ifdef _WIN32
  auto exe = executableDir();
  return exe.empty() ? fs::path("share") : exe / "share";
#else
  return "/usr/share";
#endif
}

fs::path
executableDir() {
#ifdef _WIN32
  wchar_t buffer[MAX_PATH];
  auto n = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
  if (n == 0 || n >= MAX_PATH) return {};
  return fs::path(buffer).parent_path();
#elif defined(__APPLE__)
  uint32_t size = 0;
  _NSGetExecutablePath(nullptr, &size);
  std::string buffer(size, '\0');
  if (_NSGetExecutablePath(buffer.data(), &size) != 0) return {};
  std::error_code ec;
  auto exe = fs::canonical(fs::path(buffer.c_str()), ec);
  return ec ? fs::path() : exe.parent_path();
#else
  std::error_code ec;
  auto exe = fs::read_symlink("/proc/self/exe", ec);
  return ec ? fs::path() : exe.parent_path();
#endif
}

std::string
installedWelcomeSongPath() {
#ifdef _WIN32
  return (installDataDir() / "synth" / "songs" / "welcome.xml").string();
#else
  return kInstalledWelcomeSongPath;
#endif
}

} // namespace paths
