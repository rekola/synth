#include "DeviceSettings.h"
#include "../util/Paths.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace {

std::string trim(const std::string & s) {
  auto first = s.find_first_not_of(" \t\r");
  if (first == std::string::npos) return "";
  auto last = s.find_last_not_of(" \t\r");
  return s.substr(first, last - first + 1);
}

} // namespace

DeviceSettings
parseDeviceSettings(const std::string & text) {
  DeviceSettings settings;
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) {
    line = trim(line);
    if (line.empty() || line[0] == '#') continue;
    auto eq = line.find('=');
    if (eq == std::string::npos) continue;
    auto key = trim(line.substr(0, eq));
    auto value = trim(line.substr(eq + 1));
    if (key == "capture")
      settings.capture = value;
    else if (key == "playback")
      settings.playback = value;
    else if (key == "midi_input")
      settings.midi_input = value;
  }
  return settings;
}

std::string
serializeDeviceSettings(const DeviceSettings & settings) {
  // A value holding a newline would split into a second, bogus line.
  auto clean = [](std::string s) {
    for (auto & c : s)
      if (c == '\n' || c == '\r') c = ' ';
    return s;
  };
  std::ostringstream out;
  out << "# synth audio/MIDI device selection (written by the program)\n";
  out << "capture = " << clean(settings.capture) << "\n";
  out << "playback = " << clean(settings.playback) << "\n";
  out << "midi_input = " << clean(settings.midi_input) << "\n";
  return out.str();
}

std::string
defaultDeviceSettingsPath() {
  auto dir = paths::configDir();
  return dir.empty() ? "" : (dir / "devices.conf").string();
}

DeviceSettings
loadDeviceSettings(const std::string & path) {
  if (path.empty()) return {};
  std::ifstream in(path);
  if (!in) return {};
  std::stringstream buffer;
  buffer << in.rdbuf();
  return parseDeviceSettings(buffer.str());
}

bool saveDeviceSettings(const std::string & path, const DeviceSettings & settings) {
  if (path.empty()) return false;
  std::error_code ec;
  fs::create_directories(fs::path(path).parent_path(), ec);
  // Written beside the target and renamed over it, so a crash or a full disk
  // never leaves a truncated file that would silently reset the selection.
  auto tmp = path + ".tmp";
  {
    std::ofstream out(tmp, std::ios::trunc);
    if (!out) return false;
    out << serializeDeviceSettings(settings);
    out.flush();
    if (!out) return false;
  }
  fs::rename(tmp, path, ec);
  return !ec;
}
