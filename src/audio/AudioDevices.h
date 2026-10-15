#ifndef _AUDIODEVICES_H_
#define _AUDIODEVICES_H_

#include <string>
#include <vector>

// What can be selected right now. Each is a snapshot taken on the calling
// thread; nothing here stays connected to the audio server afterwards.
struct AudioDeviceInfo {
  // DeviceSettings value: "" for the system default, else "pw:<node>" or an
  // ALSA PCM name (see DeviceSettings.h).
  std::string name;
  // Human-readable, unique within one listing.
  std::string label;
  // What tells this device from another of the same make (where it is
  // plugged in); used to make `label` unique, empty if unknown.
  std::string detail;
};

// Whether node listing goes through PipeWire itself (built with libpipewire
// and a daemon answered) or falls back to ALSA's own PCM list - which on a
// PipeWire system shows little more than "default" and the raw cards.
bool pipeWireAvailable();

// Entry 0 is always the system default. Without PipeWire this lists the
// machine's sound cards, one entry per input/output - not ALSA's long tail of
// virtual and converting PCMs, which are still usable by name with
// --capture-device/--playback-device.
std::vector<AudioDeviceInfo> listCaptureDevices();
std::vector<AudioDeviceInfo> listPlaybackDevices();

// Whether `name` (a "pw:" value) names a node that exists right now. True
// for anything that isn't a PipeWire name, and when PipeWire can't be
// queried, since nothing can be said then.
bool captureDeviceExists(const std::string & name);
bool playbackDeviceExists(const std::string & name);

// Makes each label unique: identical ones are told apart by their detail
// ("Mic [usb-0:2:1.0]"), numbered if that is not enough.
void uniquifyLabels(std::vector<AudioDeviceInfo> & devices);

#endif
