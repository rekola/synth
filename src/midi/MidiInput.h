#ifndef _MIDIINPUT_H_
#define _MIDIINPUT_H_

#include "../playback/MidiEvent.h"
#include "../util/WaitSet.h"

#include <memory>
#include <string>
#include <vector>

class Logger;

struct MidiSourceInfo {
  // DeviceSettings::midi_input value, stable across sessions.
  std::string spec;
  std::string label;
};

// What can be chosen as the MIDI input right now: every readable port that is
// not this program's own or a Launchpad's (those have a connection of their
// own). A snapshot taken on the calling thread.
std::vector<MidiSourceInfo> listMidiSources();

// One MIDI source feeding live note entry. Used from the UI thread only: it
// is read there when its wait group is ready, and set there on a device
// change.
class MidiInput {
 public:
  virtual ~MidiInput() { }

  // Connects the source named by a listMidiSources() spec, replacing the one
  // connected before; "" just disconnects. A source that is not there is
  // connected when it appears (and again when it reappears after being
  // unplugged).
  virtual void setSource(const std::string & spec, Logger & logger) = 0;

  // Drains and decodes whatever is pending.
  virtual std::vector<MidiEvent> readEvents() = 0;

  // Adds what becomes ready when readEvents() has something to return.
  virtual void addTo(WaitSet & waits, WaitSet::Group group) = 0;
};

// The MIDI input of this platform; one that never has events where there is
// no MIDI system (the reason is logged).
std::unique_ptr<MidiInput> createMidiInput(Logger & logger);

#endif
