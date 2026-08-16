#include "LaunchpadIO.h"
#include "LaunchpadPadEvent.h"
#include "LaunchpadButtonEvent.h"
#include "LaunchpadChannelPressureEvent.h"
#include "../util/Logger.h"

using namespace std;

LaunchpadIO::LaunchpadIO() { }

LaunchpadIO::~LaunchpadIO() {
  if (seq_handle) snd_seq_close(seq_handle);
}

void
LaunchpadIO::initialize(Logger & logger) {
  // Non-blocking, unlike AlsaAudio's seq handle: this is the first code in
  // the codebase to *send* MIDI, and the poll loop that drives it must
  // never stall waiting on a write.
  if (snd_seq_open(&seq_handle, "default", SND_SEQ_OPEN_DUPLEX, SND_SEQ_NONBLOCK) < 0) {
    logger.log("Launchpad: error opening ALSA sequencer");
    seq_handle = nullptr;
    return;
  }
  snd_seq_set_client_name(seq_handle, "musiceditor-launchpad");

  our_port = snd_seq_create_simple_port(seq_handle, "musiceditor-launchpad",
    SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_READ | SND_SEQ_PORT_CAP_SUBS_WRITE,
    SND_SEQ_PORT_TYPE_APPLICATION);
  if (our_port < 0) {
    logger.log("Launchpad: error creating sequencer port");
    snd_seq_close(seq_handle);
    seq_handle = nullptr;
    return;
  }

  logger_ = &logger;

  // Subscribe to the system announce port so PORT_START/PORT_EXIT events
  // (hotplug) arrive on our own port, decoded in the same pollEvents() loop
  // as regular note/aftertouch data - no separate thread or descriptor.
  snd_seq_connect_from(seq_handle, our_port, SND_SEQ_CLIENT_SYSTEM, SND_SEQ_PORT_SYSTEM_ANNOUNCE);

  scanForDevices(logger);
}

void
LaunchpadIO::scanForDevices(Logger & logger) {
  snd_seq_client_info_t * client_info;
  snd_seq_port_info_t * port_info;
  snd_seq_client_info_alloca(&client_info);
  snd_seq_port_info_alloca(&port_info);

  struct Candidate {
    int port;
    LaunchpadProtocol::Model model;
    bool is_daw_port;
  };

  // Each distinct ALSA client that looks like a Launchpad gets its own
  // session - multiple connected devices are all supported (each is
  // typically its own ALSA client), all routed to whatever the current
  // track is (no per-device routing yet, per the plan's scope).
  snd_seq_client_info_set_client(client_info, -1);
  while (snd_seq_query_next_client(seq_handle, client_info) >= 0) {
    int client = snd_seq_client_info_get_client(client_info);
    if (client == snd_seq_client_id(seq_handle)) continue; // skip ourselves

    optional<Candidate> best;

    snd_seq_port_info_set_client(port_info, client);
    snd_seq_port_info_set_port(port_info, -1);
    while (snd_seq_query_next_port(seq_handle, port_info) >= 0) {
      auto caps = snd_seq_port_info_get_capability(port_info);
      if (!(caps & SND_SEQ_PORT_CAP_SUBS_READ) || !(caps & SND_SEQ_PORT_CAP_SUBS_WRITE)) continue;

      string port_name = snd_seq_port_info_get_name(port_info);
      string client_name = snd_seq_client_info_get_name(client_info);

      auto model = LaunchpadProtocol::modelFromDeviceName(port_name);
      if (!model) model = LaunchpadProtocol::modelFromDeviceName(client_name);
      if (!model) continue;

      // A device may expose more than one ALSA port on the same client
      // (e.g. a separate DAW interface); prefer whichever port isn't the
      // DAW one. Confirmed against real hardware: on a Launchpad X the
      // port names are short enough to survive intact ("...LPX DAW In" /
      // "...LPX MIDI In"), but the kernel's snd-usb-audio driver truncates
      // USB-MIDI jack names to 31 characters, and a Launchpad Mini MK3's
      // longer names get cut mid-word right there - "...LPMiniMK3 DAW..."
      // becomes "...LPMiniMK3 DA" and "...LPMiniMK3 MIDI..." becomes
      // "...LPMiniMK3 MI" (verified via `aconnect -l`). A plain find("DAW")
      // never matches that truncated form, so the DAW port went
      // undetected and was silently kept as "best" instead of the real
      // input port - pad presses never arrived even though LED SysEx
      // (apparently accepted on either port) looked fine. Recognize the
      // truncated form too, alongside the untruncated one.
      auto looksLikeDawPort = [](const string & name) {
        return name.find("DAW") != string::npos ||
          (name.size() >= 2 && name.compare(name.size() - 2, 2, "DA") == 0);
      };
      bool is_daw_port = looksLikeDawPort(port_name) || looksLikeDawPort(client_name);
      int port = snd_seq_port_info_get_port(port_info);

      if (!best || (best->is_daw_port && !is_daw_port)) {
	best = Candidate{port, *model, is_daw_port};
      }
    }

    if (best) {
      connectToDevice(logger, client, best->port, best->model);
    }
  }
}

void
LaunchpadIO::handlePortStart(int client, int port) {
  if (!logger_ || client == snd_seq_client_id(seq_handle)) return;

  // Don't create a second session for a client we're already connected to
  // - e.g. its DAW-interface port announcing after we already grabbed its
  // plain MIDI port. Unlike the startup scan (which sees every port of a
  // client before choosing the best one), hotplug processes one port at a
  // time, so whichever port of a given client announces first is the one
  // used - a simpler, accepted limitation for this incremental step.
  for (auto & session : sessions) {
    if (session.client == client) return;
  }

  snd_seq_client_info_t * client_info;
  snd_seq_port_info_t * port_info;
  snd_seq_client_info_alloca(&client_info);
  snd_seq_port_info_alloca(&port_info);

  if (snd_seq_get_any_client_info(seq_handle, client, client_info) < 0) return;
  if (snd_seq_get_any_port_info(seq_handle, client, port, port_info) < 0) return;

  auto caps = snd_seq_port_info_get_capability(port_info);
  if (!(caps & SND_SEQ_PORT_CAP_SUBS_READ) || !(caps & SND_SEQ_PORT_CAP_SUBS_WRITE)) return;

  string port_name = snd_seq_port_info_get_name(port_info);
  string client_name = snd_seq_client_info_get_name(client_info);

  auto model = LaunchpadProtocol::modelFromDeviceName(port_name);
  if (!model) model = LaunchpadProtocol::modelFromDeviceName(client_name);
  if (!model) return;

  connectToDevice(*logger_, client, port, *model);
}

void
LaunchpadIO::handlePortExit(int client, int port) {
  for (auto it = sessions.begin(); it != sessions.end(); ) {
    if (it->client == client && it->port == port) it = sessions.erase(it);
    else ++it;
  }
}

void
LaunchpadIO::connectToDevice(Logger & logger, int client, int port, LaunchpadProtocol::Model model) {
  if (snd_seq_connect_from(seq_handle, our_port, client, port) < 0 ||
      snd_seq_connect_to(seq_handle, our_port, client, port) < 0) {
    logger.log("Launchpad: failed to connect to detected device");
    return;
  }

  sessions.push_back({model, client, port, SessionState::DETECTED, next_session_id_++});

  sendSysEx(LaunchpadProtocol::buildProgrammerModeEnter(model), client, port);
  // No blocking wait for any reply - see the plan's design decision on the
  // Programmer-Mode handshake never stalling the UI thread. Device Inquiry
  // below is fire-and-forget confirmation only (its reply, if any, isn't
  // needed for the session to be usable).
  sessions.back().state = SessionState::READY;
  sendSysEx(LaunchpadProtocol::buildDeviceInquiry(), client, port);

  logger.log("Launchpad: connected");
}

void
LaunchpadIO::sendSysEx(const vector<uint8_t> & bytes, int dest_client, int dest_port) {
  if (!seq_handle || our_port < 0) return;

  snd_seq_event_t ev;
  snd_seq_ev_clear(&ev);
  snd_seq_ev_set_source(&ev, our_port);
  snd_seq_ev_set_dest(&ev, dest_client, dest_port);
  snd_seq_ev_set_direct(&ev);
  // snd_seq_ev_set_sysex() is a macro (<alsa/seqmid.h>) that expands to a
  // plain ~mask-style bit-clear on ev.flags (an unsigned char field) -
  // GCC attributes -Wsign-conversion diagnostics for macro-expanded code
  // to the expansion site (here), not the system header where the macro
  // is actually defined, so the usual system-header suppression doesn't
  // apply (same issue as RealFFT.h's PocketFFT wrapper - see its comment).
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-conversion"
  snd_seq_ev_set_sysex(&ev, static_cast<int>(bytes.size()), const_cast<uint8_t *>(bytes.data()));
#pragma GCC diagnostic pop

  // Non-blocking output can transiently fail (e.g. EAGAIN) for a small
  // SysEx message; dropping it here is preferable to retrying inline and
  // risking a stall - LED updates get re-sent on the next (tuning, key)
  // change anyway, so a dropped message just means a stale grid for one
  // interval, not a silent permanent failure.
  snd_seq_event_output_direct(seq_handle, &ev);
}

vector<pollfd>
LaunchpadIO::getPollDescriptors() const {
  if (!seq_handle) return {};
  size_t nfds = static_cast<size_t>(snd_seq_poll_descriptors_count(seq_handle, POLLIN));
  vector<pollfd> result(nfds);
  snd_seq_poll_descriptors(seq_handle, result.data(), nfds, POLLIN);
  return result;
}

vector<unique_ptr<Event>>
LaunchpadIO::pollEvents() {
  vector<unique_ptr<Event>> result;
  if (!seq_handle) return result;

  do {
    snd_seq_event_t * ev;
    if (snd_seq_event_input(seq_handle, &ev) < 0) break;

    if (ev->type == SND_SEQ_EVENT_PORT_START) {
      handlePortStart(ev->data.addr.client, ev->data.addr.port);
      snd_seq_free_event(ev);
      continue;
    }
    if (ev->type == SND_SEQ_EVENT_PORT_EXIT) {
      handlePortExit(ev->data.addr.client, ev->data.addr.port);
      snd_seq_free_event(ev);
      continue;
    }

    for (size_t session_index = 0; session_index < sessions.size(); session_index++) {
      auto & session = sessions[session_index];
      if (ev->source.client != session.client || ev->source.port != session.port) continue;
      session.state = SessionState::READY;

      if (ev->type == SND_SEQ_EVENT_CONTROLLER) {
	// Extra buttons outside the 8x8 grid send Control Change. 127=press,
	// 0=release is a reasonable inference from Novation's documented
	// button behavior but hasn't been confirmed against real hardware.
	auto cc_number = ev->data.control.param;
	auto value = ev->data.control.value;
	auto kind = value == 0 ? LaunchpadButtonEvent::RELEASE : LaunchpadButtonEvent::PRESS;
	result.push_back(make_unique<LaunchpadButtonEvent>(session.session_id, cc_number, kind, session.model));
	break;
      }

      if (ev->type == SND_SEQ_EVENT_CHANPRESS) {
	// Device-wide aftertouch - the alternative mode to per-pad KEYPRESS
	// (LaunchpadPadEvent::AFTERTOUCH) below, mutually exclusive with it
	// on real hardware (a device is configured to send one or the
	// other, never both for the same gesture).
	result.push_back(make_unique<LaunchpadChannelPressureEvent>(session.session_id, ev->data.control.value, session.model));
	break;
      }

      LaunchpadPadEvent::Kind kind;
      int note, velocity;
      bool matched = true;

      switch (ev->type) {
      case SND_SEQ_EVENT_NOTEON:
	note = ev->data.note.note;
	velocity = ev->data.note.velocity;
	kind = velocity == 0 ? LaunchpadPadEvent::RELEASE : LaunchpadPadEvent::PRESS;
	break;
      case SND_SEQ_EVENT_NOTEOFF:
	note = ev->data.note.note;
	velocity = 0;
	kind = LaunchpadPadEvent::RELEASE;
	break;
      case SND_SEQ_EVENT_KEYPRESS:
	note = ev->data.note.note;
	velocity = ev->data.note.velocity;
	kind = LaunchpadPadEvent::AFTERTOUCH;
	break;
      default:
	matched = false;
	break;
      }

      if (matched) {
	auto pad = LaunchpadProtocol::noteNumberToPad(note);
	if (pad) {
	  result.push_back(make_unique<LaunchpadPadEvent>(session.session_id, pad->first, pad->second, kind, velocity, session.model));
	}
      }
      break;
    }

    snd_seq_free_event(ev);
  } while (snd_seq_event_input_pending(seq_handle, 0) > 0);

  return result;
}

vector<int>
LaunchpadIO::readySessionIds() const {
  vector<int> result;
  for (auto & session : sessions) {
    if (session.state == SessionState::READY) result.push_back(session.session_id);
  }
  return result;
}

optional<LaunchpadProtocol::Model>
LaunchpadIO::modelForSession(int session_id) const {
  for (auto & session : sessions) {
    if (session.state == SessionState::READY && session.session_id == session_id) return session.model;
  }
  return nullopt;
}

void
LaunchpadIO::sendLeds(int session_id, const vector<LaunchpadProtocol::PadColor> & colors) {
  for (auto & session : sessions) {
    if (session.state != SessionState::READY || session.session_id != session_id) continue;

    if (session.model == LaunchpadProtocol::Model::PRO_MK3) {
      sendSysEx(LaunchpadProtocol::buildRgbLedSysEx(session.model, colors), session.client, session.port);
      return;
    }

    // X/Mini MK3 lack Pro MK3's extra buttons entirely and cap out at 81
    // colourspecs (64 grid + the 17 CC numbers shared by all three models) -
    // drop anything Pro-MK3-exclusive rather than risk sending an
    // oversized/malformed message.
    vector<LaunchpadProtocol::PadColor> filtered;
    for (auto & color : colors) {
      if (!LaunchpadProtocol::isProMk3OnlyLedIndex(color.led_index)) filtered.push_back(color);
    }
    sendSysEx(LaunchpadProtocol::buildRgbLedSysEx(session.model, filtered), session.client, session.port);
    return;
  }
}
