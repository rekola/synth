#include "LaunchpadIO.h"
#include "../util/Logger.h"

#include <algorithm>
#include <cstdlib>

using namespace std;

LaunchpadIO::LaunchpadIO(unique_ptr<MidiPortBackend> backend) : backend_(std::move(backend)) { }

LaunchpadIO::~LaunchpadIO() {
  clearAllLeds();
}

void
LaunchpadIO::clearAllLeds() {
  for (auto & session : sessions) {
    if (session.state != SessionState::READY) continue;
    vector<LaunchpadProtocol::PadColor> colors;
    for (int y = 0; y < 8; y++) {
      for (int x = 0; x < 8; x++) colors.push_back({LaunchpadProtocol::padToNoteNumber(x, y), 0, 0, 0});
    }
    for (auto led_index : LaunchpadProtocol::allExtraButtonLedIndices()) colors.push_back({led_index, 0, 0, 0});
    sendLeds(session.session_id, colors);
  }
}

void
LaunchpadIO::initialize(Logger & logger) {
  if (!backend_ || !backend_->open(logger)) {
    backend_.reset();
    logger.log("Launchpad: no MIDI system");
    return;
  }

  // See ignore_hardware_'s own comment (LaunchpadIO.h) - test-harness-only.
  ignore_hardware_ = getenv("SYNTH_LAUNCHPAD_NO_HARDWARE") != nullptr;

  logger_ = &logger;

  scanForDevices(logger);
}

namespace {

// Confirmed against real hardware: a Launchpad X's port names survive intact
// ("...LPX DAW In" / "...LPX MIDI In"), but snd-usb-audio truncates USB-MIDI
// jack names to 31 characters, so a Mini MK3's become "...LPMiniMK3 DA" and
// "...LPMiniMK3 MI". A plain find("DAW") misses the truncated form, so
// recognize it too. Pad presses arrive on the non-DAW port only (LED SysEx is
// accepted on either, which hides the mistake).
bool
looksLikeDawPort(const string & name) {
  return name.find("DAW") != string::npos ||
    (name.size() >= 2 && name.compare(name.size() - 2, 2, "DA") == 0);
}

optional<LaunchpadProtocol::Model>
modelOf(const MidiPortInfo & info) {
  auto model = LaunchpadProtocol::modelFromDeviceName(info.port_name);
  if (!model) model = LaunchpadProtocol::modelFromDeviceName(info.client_name);
  return model;
}

} // namespace

void
LaunchpadIO::scanForDevices(Logger & logger) {
  struct Candidate {
    int client;
    MidiPortAddress address;
    LaunchpadProtocol::Model model;
    bool is_daw_port;
  };

  // Each distinct client that looks like a Launchpad gets its own session -
  // multiple connected devices are all supported (each is typically its own
  // client), all routed to whatever the current track is (no per-device
  // routing yet, per the plan's scope).
  vector<Candidate> best; // one per client, in the order the ports came
  for (auto & info : backend_->ports()) {
    if (!acceptsPort(info)) continue;
    auto model = modelOf(info);
    if (!model) continue;

    // A device may expose a separate DAW port; prefer the other one (pad
    // presses only arrive there). See looksLikeDawPort().
    bool is_daw_port = looksLikeDawPort(info.port_name) || looksLikeDawPort(info.client_name);
    auto existing = find_if(best.begin(), best.end(), [&](const Candidate & c) { return c.client == info.address.client; });
    if (existing == best.end()) {
      best.push_back({ info.address.client, info.address, *model, is_daw_port });
    } else if (existing->is_daw_port && !is_daw_port) {
      *existing = Candidate{ info.address.client, info.address, *model, is_daw_port };
    }
  }

  for (auto & candidate : best) connectToDevice(logger, candidate.address, candidate.model, candidate.is_daw_port);
}

bool
LaunchpadIO::acceptsPort(const MidiPortInfo & info) const {
  if (ignore_hardware_) return !info.is_hardware;
  return info.client_name.find(kSimulatorMarker) == string::npos;
}

void
LaunchpadIO::handlePortStart(MidiPortAddress address) {
  if (!logger_) return;

  // Don't create a second session for a client we're already connected to.
  // Unlike the startup scan, hotplug sees one port at a time, so the DAW port
  // may announce first; the real input port then replaces it (below).
  bool replace_daw_session = false;
  for (auto & session : sessions) {
    if (session.address.client != address.client) continue;
    if (!session.is_daw_port) return;
    replace_daw_session = true;
  }

  auto info = backend_->port(address);
  if (!info || !acceptsPort(*info)) return;

  auto detected = modelOf(*info);
  if (!detected) return;
  auto model = *detected;

  bool is_daw_port = looksLikeDawPort(info->port_name) || looksLikeDawPort(info->client_name);
  if (replace_daw_session) {
    if (is_daw_port) return;
    for (auto it = sessions.begin(); it != sessions.end(); ++it) {
      if (it->address.client != address.client) continue;
      backend_->disconnect(it->address);
      sessions.erase(it);
      break;
    }
  }

  connectToDevice(*logger_, address, model, is_daw_port);
}

void
LaunchpadIO::handlePortExit(MidiPortAddress address) {
  for (auto it = sessions.begin(); it != sessions.end(); ) {
    if (it->address == address) it = sessions.erase(it);
    else ++it;
  }
}

void
LaunchpadIO::connectToDevice(Logger & logger, MidiPortAddress address, LaunchpadProtocol::Model model, bool is_daw_port) {
  if (!backend_->connect(address)) {
    logger.log("Launchpad: failed to connect to detected device");
    return;
  }

  sessions.push_back({model, address, SessionState::DETECTED, next_session_id_++, is_daw_port});

  backend_->sendSysEx(address, LaunchpadProtocol::buildProgrammerModeEnter(model));
  // No blocking wait for any reply - see the plan's design decision on the
  // Programmer-Mode handshake never stalling the UI thread. Device Inquiry
  // below is fire-and-forget confirmation only (its reply, if any, isn't
  // needed for the session to be usable).
  sessions.back().state = SessionState::READY;
  backend_->sendSysEx(address, LaunchpadProtocol::buildDeviceInquiry());

  logger.log("Launchpad: connected");
}

void
LaunchpadIO::addTo(WaitSet & waits, WaitSet::Group group) {
  if (backend_) backend_->addTo(waits, group);
}

vector<LaunchpadInput>
LaunchpadIO::pollEvents() {
  vector<LaunchpadInput> result;
  if (!backend_) return result;

  for (auto & message : backend_->receive()) {
    if (message.type == MidiMessage::PORT_ADDED) {
      handlePortStart(message.source);
      continue;
    }
    if (message.type == MidiMessage::PORT_REMOVED) {
      handlePortExit(message.source);
      continue;
    }

    for (auto & session : sessions) {
      if (!(message.source == session.address)) continue;
      session.state = SessionState::READY;

      if (message.type == MidiMessage::CONTROL_CHANGE) {
	// Extra buttons outside the 8x8 grid send Control Change. 127=press,
	// 0=release is a reasonable inference from Novation's documented
	// button behavior but hasn't been confirmed against real hardware.
	auto kind = message.value == 0 ? LaunchpadButtonEvent::RELEASE : LaunchpadButtonEvent::PRESS;
        result.push_back(LaunchpadButtonEvent(session.session_id, message.number, kind, session.model));
        break;
      }

      if (message.type == MidiMessage::CHANNEL_PRESSURE) {
	// Device-wide aftertouch - the alternative mode to per-pad KEYPRESS
	// (LaunchpadPadEvent::AFTERTOUCH) below, mutually exclusive with it
	// on real hardware (a device is configured to send one or the
	// other, never both for the same gesture).
        result.push_back(LaunchpadChannelPressureEvent(session.session_id, message.value, session.model));
        break;
      }

      LaunchpadPadEvent::Kind kind;
      int velocity;
      switch (message.type) {
      case MidiMessage::NOTE_ON:
	velocity = message.value;
	kind = velocity == 0 ? LaunchpadPadEvent::RELEASE : LaunchpadPadEvent::PRESS;
	break;
      case MidiMessage::NOTE_OFF:
	velocity = 0;
	kind = LaunchpadPadEvent::RELEASE;
	break;
      case MidiMessage::KEY_PRESSURE:
	velocity = message.value;
	kind = LaunchpadPadEvent::AFTERTOUCH;
	break;
      default:
	velocity = -1;
	break;
      }

      if (velocity >= 0) {
	auto pad = LaunchpadProtocol::noteNumberToPad(message.number);
	if (pad) {
          result.push_back(LaunchpadPadEvent(session.session_id, pad->first, pad->second, kind, velocity, session.model));
        }
      }
      break;
    }
  }

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
      backend_->sendSysEx(session.address, LaunchpadProtocol::buildLedLightingSysEx(session.model, colors));
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
    backend_->sendSysEx(session.address, LaunchpadProtocol::buildLedLightingSysEx(session.model, filtered));
    return;
  }
}
