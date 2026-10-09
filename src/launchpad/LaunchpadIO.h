#ifndef _LAUNCHPADIO_H_
#define _LAUNCHPADIO_H_

#include "LaunchpadProtocol.h"

#include <alsa/asoundlib.h>

#include <memory>
#include <optional>
#include <vector>

class Logger;
class Event;

// ALSA-facing Launchpad I/O: the only part of Launchpad support that
// touches real hardware (everything else - layout math, SysEx encode/decode
// - lives in the hardware-agnostic LaunchpadLayout/LaunchpadProtocol).
// Owns its own dedicated ALSA sequencer client/port (separate from
// AlsaAudio's), since sending SysEx/LEDs requires a read+write-capable
// port that doesn't exist anywhere in this codebase yet.
//
// v1 scope (see the Launchpad plan): multiple simultaneously-connected
// devices are supported (including hotplugged while running), but all
// route to whatever the current track is (no per-device routing yet).
class LaunchpadIO {
 public:
  LaunchpadIO();
  ~LaunchpadIO();

  // Opens the ALSA sequencer client/port, scans already-connected clients
  // by name for known Launchpad models (auto-connecting to each match),
  // and subscribes to hotplug (device connect/disconnect) notifications
  // for as long as this object lives. Never blocks waiting for any device
  // reply - see the plan's design decision on not stalling the UI thread's
  // poll loop.
  void initialize(Logger & logger);

  std::vector<pollfd> getPollDescriptors() const;

  // Drains and decodes all currently-pending events (LaunchpadPadEvent for
  // grid presses/aftertouch, LaunchpadButtonEvent for the extra CC-numbered
  // buttons) into a common Event vector - the consumer (TerminalUI) only
  // ever needs Event& to dispatch, via EventHandler::handleEvent. Must only
  // be called after poll() has reported one of getPollDescriptors()'s fds
  // ready.
  std::vector<std::unique_ptr<Event>> pollEvents();

  // Stable ids (see Session::session_id below) of every currently-ready
  // connected device - what a caller wanting per-device state (assigned
  // track, octave, ...) should iterate.
  std::vector<int> readySessionIds() const;

  // The model of one currently-ready session (a readySessionIds() member) -
  // nullopt if session_id doesn't name a ready session. Lets a caller
  // (LaunchpadManager, to pick a sensible per-model default like an
  // initial octave register) key off which physical device this is
  // without needing to wait for a pad press event to learn its model.
  std::optional<LaunchpadProtocol::Model> modelForSession(int session_id) const;

  // Sends LED colors to one specific ready device (addressed with its own
  // model's SysEx header, filtering out CC numbers that model doesn't
  // have). Silently does nothing if session_id doesn't name a ready
  // session.
  void sendLeds(int session_id, const std::vector<LaunchpadProtocol::PadColor> & colors);

 private:
  enum class SessionState { DETECTED, READY };

  struct Session {
    LaunchpadProtocol::Model model;
    int client, port;
    SessionState state;
    // Stable identity for this connection, assigned once at connect time -
    // unlike the session's position in the `sessions` vector, this never
    // changes when an earlier session disconnects (see handlePortExit's
    // erase). LaunchpadPadEvent/LaunchpadButtonEvent's device_index is this
    // id, so per-device state keyed on it survives hotplug churn.
    int session_id;
    // Connected to the device's DAW port rather than its MIDI one.
    bool is_daw_port = false;
  };

  void scanForDevices(Logger & logger);
  void connectToDevice(Logger & logger, int client, int port, LaunchpadProtocol::Model model, bool is_daw_port);
  void sendSysEx(const std::vector<uint8_t> & bytes, int dest_client, int dest_port);

  // Blanks every LED on every currently-ready device - the destructor's
  // own last act, so a Launchpad doesn't sit there still showing whatever
  // Session view/step grid/etc. happened to be lit when the app quit.
  // Just an all-black LED-lighting message, not a Programmer Mode exit -
  // this codebase never actually leaves Programmer Mode once entered, and
  // going dark is a clearer, more predictable "we're done" signal than
  // guessing at whatever a device's own standalone light show would
  // otherwise resume showing.
  void clearAllLeds();

  // Hotplug: handles PORT_START/PORT_EXIT events arriving via the system
  // announce port subscription set up in initialize().
  void handlePortStart(int client, int port);
  void handlePortExit(int client, int port);

  // Whether a device-named ALSA client is one to connect to at all:
  // real hardware and the e2e simulators never mix. The simulators
  // (tools/e2e/fake_launchpad_*.c) register under a real Launchpad's own
  // port name, so their client name carries kSimulatorMarker - an
  // interactive session skips those, and a harness-spawned one (see
  // ignore_hardware_) skips kernel (hardware) clients instead.
  bool acceptsClient(snd_seq_client_info_t * client_info) const;
  static constexpr const char * kSimulatorMarker = "(e2e)";

  // True when the SYNTH_LAUNCHPAD_NO_HARDWARE environment variable is set
  // (any non-empty value), read once in initialize() - the e2e test
  // harness (tools/e2e/harness.py) sets it, so a synth spawned for a test
  // never connects to a real Launchpad also plugged into the machine
  // (updating its LEDs, or taking its stray traffic as the simulator's).
  bool ignore_hardware_ = false;

  snd_seq_t * seq_handle = nullptr;
  int our_port = -1;
  Logger * logger_ = nullptr;
  std::vector<Session> sessions;
  int next_session_id_ = 0;
};

#endif
