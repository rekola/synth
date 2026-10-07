#ifndef _DEVICESETTINGS_H_
#define _DEVICESETTINGS_H_

#include <string>

// The audio/MIDI endpoints the player has chosen. Machine properties rather
// than song properties, so they live in a per-user file, not in any song.
//
// An audio name is "" or "default" (the system default), "pw:<node name>" (a
// PipeWire node, matched by its stable node.name rather than its numeric id,
// which changes every session) or anything else, taken as a raw ALSA PCM name
// (e.g. "hw:1,0"). A MIDI input is "<client name>:<port name>", or "" for none
// (connections made outside the program still work).
struct DeviceSettings {
  std::string capture;
  std::string playback;
  std::string midi_input;

  bool operator==(const DeviceSettings & o) const {
    return capture == o.capture && playback == o.playback && midi_input == o.midi_input;
  }
};

constexpr const char * kPipeWirePrefix = "pw:";

inline bool isPipeWireDevice(const std::string & name) {
  return name.compare(0, 3, kPipeWirePrefix) == 0 && name.size() > 3;
}

inline std::string pipeWireNodeName(const std::string & name) {
  return isPipeWireDevice(name) ? name.substr(3) : std::string();
}

inline bool isDefaultDevice(const std::string & name) {
  return name.empty() || name == "default";
}

// One `key = value` per line; blank lines and lines starting with '#' are
// ignored, as are unknown keys. Values are taken verbatim (they may contain
// spaces) apart from surrounding whitespace.
DeviceSettings parseDeviceSettings(const std::string & text);
std::string serializeDeviceSettings(const DeviceSettings & settings);

// $XDG_CONFIG_HOME/synth/devices.conf, else ~/.config/synth/devices.conf;
// empty if neither variable is set.
std::string defaultDeviceSettingsPath();

// A missing or unreadable file is just empty settings.
DeviceSettings loadDeviceSettings(const std::string & path);
// Creates the directory if needed; false on failure.
bool saveDeviceSettings(const std::string & path, const DeviceSettings & settings);

#endif
