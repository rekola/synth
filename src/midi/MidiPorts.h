#ifndef _MIDIPORTS_H_
#define _MIDIPORTS_H_

#include "../util/WaitSet.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class Logger;

// A MIDI port that can be both read from and written to, as the platform
// names it. `client` and `port` are the backend's own identifiers, opaque to
// the caller and stable while the port exists.
struct MidiPortAddress {
  int client = 0;
  int port = 0;
  bool operator==(const MidiPortAddress & other) const { return client == other.client && port == other.port; }
};

struct MidiPortInfo {
  MidiPortAddress address;
  std::string client_name;
  std::string port_name;
  // A real device rather than another program's port.
  bool is_hardware = false;
};

// One short message received from a connected port, or a change in which
// ports exist.
struct MidiMessage {
  enum Type { NOTE_ON, NOTE_OFF, KEY_PRESSURE, CONTROL_CHANGE, CHANNEL_PRESSURE, PORT_ADDED, PORT_REMOVED };
  Type type = NOTE_ON;
  MidiPortAddress source;
  // The note or controller number; unused for channel pressure.
  int number = 0;
  // Velocity, controller value or pressure.
  int value = 0;
};

// Two-way MIDI to devices that need messages sent as well as received (a
// Launchpad). The platform's sequencer or port API lives behind this.
class MidiPortBackend {
 public:
  virtual ~MidiPortBackend() { }

  // Opens the platform's MIDI system and starts watching for ports coming
  // and going; false (already logged) if there is none.
  virtual bool open(Logger & logger) = 0;

  // Every readable and writable port now, except this backend's own.
  virtual std::vector<MidiPortInfo> ports() = 0;
  virtual std::optional<MidiPortInfo> port(MidiPortAddress address) = 0;

  // Starts receiving from and sending to a port; false if that failed.
  virtual bool connect(MidiPortAddress address) = 0;
  virtual void disconnect(MidiPortAddress address) = 0;

  // Sends one system-exclusive message (F0 ... F7 included). Never blocks: a
  // message the system cannot take now is dropped.
  virtual void sendSysEx(MidiPortAddress address, const std::vector<uint8_t> & bytes) = 0;

  // Drains what arrived from connected ports, and port changes.
  virtual std::vector<MidiMessage> receive() = 0;

  virtual void addTo(WaitSet & waits, WaitSet::Group group) = 0;
};

// The backend of this platform, or null where there is none.
std::unique_ptr<MidiPortBackend> createMidiPortBackend();

#endif
