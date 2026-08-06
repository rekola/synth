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
  };

  void scanForDevices(Logger & logger);
  void connectToDevice(Logger & logger, int client, int port, LaunchpadProtocol::Model model);
  void sendSysEx(const std::vector<uint8_t> & bytes, int dest_client, int dest_port);

  // Hotplug: handles PORT_START/PORT_EXIT events arriving via the system
  // announce port subscription set up in initialize().
  void handlePortStart(int client, int port);
  void handlePortExit(int client, int port);

  snd_seq_t * seq_handle = nullptr;
  int our_port = -1;
  Logger * logger_ = nullptr;
  std::vector<Session> sessions;
  int next_session_id_ = 0;
};

#endif
