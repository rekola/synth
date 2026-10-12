#include "AlsaMidiInput.h"
#include "../launchpad/LaunchpadProtocol.h"
#include "../util/Logger.h"

#include <map>

using namespace std;

namespace {

struct AlsaSource {
  MidiSourceInfo info;
  int client = 0;
  int port = 0;
};

vector<AlsaSource>
listSources() {
  vector<AlsaSource> result;
  snd_seq_t * seq = nullptr;
  if (snd_seq_open(&seq, "default", SND_SEQ_OPEN_INPUT, 0) < 0) return result;
  snd_seq_set_client_name(seq, "synth-list");

  snd_seq_client_info_t * client_info;
  snd_seq_port_info_t * port_info;
  snd_seq_client_info_alloca(&client_info);
  snd_seq_port_info_alloca(&port_info);

  snd_seq_client_info_set_client(client_info, -1);
  while (snd_seq_query_next_client(seq, client_info) >= 0) {
    int client = snd_seq_client_info_get_client(client_info);
    if (client == SND_SEQ_CLIENT_SYSTEM || client == snd_seq_client_id(seq)) continue;
    string client_name = snd_seq_client_info_get_name(client_info);
    // Our own player and Launchpad connections, and any other running copy.
    if (client_name == "synth" || client_name.compare(0, 6, "synth-") == 0) continue;

    snd_seq_port_info_set_client(port_info, client);
    snd_seq_port_info_set_port(port_info, -1);
    while (snd_seq_query_next_port(seq, port_info) >= 0) {
      auto caps = snd_seq_port_info_get_capability(port_info);
      if (!(caps & SND_SEQ_PORT_CAP_READ) || !(caps & SND_SEQ_PORT_CAP_SUBS_READ)) continue;
      if (caps & SND_SEQ_PORT_CAP_NO_EXPORT) continue;
      string port_name = snd_seq_port_info_get_name(port_info);
      if (LaunchpadProtocol::modelFromDeviceName(port_name) ||
          LaunchpadProtocol::modelFromDeviceName(client_name)) continue;

      AlsaSource source;
      source.client = client;
      source.port = snd_seq_port_info_get_port(port_info);
      source.info.spec = client_name + ":" + port_name;
      source.info.label = client_name == port_name ? client_name : client_name + ": " + port_name;
      result.push_back(std::move(source));
    }
  }
  snd_seq_close(seq);

  // Two ports sharing a spec could not be told apart on reconnect.
  map<string, int> seen;
  for (auto & source : result) {
    int n = ++seen[source.info.spec];
    if (n > 1) {
      source.info.spec += " (" + to_string(n) + ")";
      source.info.label += " (" + to_string(n) + ")";
    }
  }
  return result;
}

} // namespace

vector<MidiSourceInfo>
listMidiSources() {
  vector<MidiSourceInfo> result;
  for (auto & source : listSources()) result.push_back(source.info);
  return result;
}

unique_ptr<MidiInput>
createMidiInput(Logger & logger) {
  return make_unique<AlsaMidiInput>(logger);
}

AlsaMidiInput::AlsaMidiInput(Logger & logger) {
  if (snd_seq_open(&seq_handle_, "default", SND_SEQ_OPEN_DUPLEX, 0) < 0) {
    seq_handle_ = nullptr;
    logger.log("Error opening ALSA sequencer, MIDI input disabled");
    return;
  }
  snd_seq_set_client_name(seq_handle_, "synth");
  own_port_ = snd_seq_create_simple_port(seq_handle_, "synth",
                                         SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE,
                                         SND_SEQ_PORT_TYPE_APPLICATION);
  if (own_port_ < 0) {
    logger.log("Error creating sequencer port, MIDI input disabled");
    snd_seq_close(seq_handle_);
    seq_handle_ = nullptr;
    return;
  }
  // Port and client announcements, so a chosen MIDI source that is plugged
  // in later (or replugged) gets connected - see readEvents().
  snd_seq_connect_from(seq_handle_, own_port_, SND_SEQ_CLIENT_SYSTEM, SND_SEQ_PORT_SYSTEM_ANNOUNCE);
}

AlsaMidiInput::~AlsaMidiInput() {
  if (seq_handle_) snd_seq_close(seq_handle_);
}

void
AlsaMidiInput::setSource(const string & spec, Logger & logger) {
  spec_ = spec;
  connect(&logger);
}

void
AlsaMidiInput::connect(Logger * logger) {
  if (!seq_handle_) {
    if (logger && !spec_.empty()) logger->log("MIDI input unavailable: no ALSA sequencer");
    return;
  }
  if (connected_client_ >= 0) {
    snd_seq_disconnect_from(seq_handle_, own_port_, connected_client_, connected_port_);
    connected_client_ = connected_port_ = -1;
  }
  if (spec_.empty()) {
    if (logger) logger->log("MIDI input: none selected");
    return;
  }
  for (auto & source : listSources()) {
    if (source.info.spec != spec_) continue;
    int r = snd_seq_connect_from(seq_handle_, own_port_, source.client, source.port);
    if (r < 0) {
      if (logger) logger->log(string("WARNING: Can't connect MIDI input '") + source.info.label + "': " + snd_strerror(r));
    } else {
      connected_client_ = source.client;
      connected_port_ = source.port;
      if (logger) logger->log("MIDI input: " + source.info.label);
    }
    return;
  }
  if (logger) logger->log("MIDI input '" + spec_ + "' not found, connecting when it appears");
}

void
AlsaMidiInput::addTo(WaitSet & waits, WaitSet::Group group) {
  if (!seq_handle_) return;
  auto nfds = static_cast<size_t>(snd_seq_poll_descriptors_count(seq_handle_, POLLIN));
  vector<pollfd> fds(nfds);
  if (snd_seq_poll_descriptors(seq_handle_, fds.data(), static_cast<unsigned>(nfds), POLLIN) < 0) return;
  for (auto & fd : fds) waits.addFd(group, fd.fd);
}

vector<MidiEvent>
AlsaMidiInput::readEvents() {
  vector<MidiEvent> r;
  if (!seq_handle_) return r;

  do {
    snd_seq_event_t *ev;
    snd_seq_event_input(seq_handle_, &ev);

    switch (ev->type) {
      case SND_SEQ_EVENT_PORT_START:
        // A chosen source that wasn't there, or was unplugged, is back.
        if (connected_client_ < 0 && !spec_.empty() && ev->data.addr.client != snd_seq_client_id(seq_handle_)) {
          connect(nullptr);
        }
        break;
      case SND_SEQ_EVENT_PORT_EXIT:
      case SND_SEQ_EVENT_CLIENT_EXIT:
        // ALSA drops the subscription itself; just remember that it's gone.
        if (ev->data.addr.client == connected_client_ &&
            (ev->type == SND_SEQ_EVENT_CLIENT_EXIT || ev->data.addr.port == connected_port_)) {
          connected_client_ = connected_port_ = -1;
        }
        break;
      case SND_SEQ_EVENT_SYSTEM:
        break;
      case SND_SEQ_EVENT_RESULT:
        break;
      case SND_SEQ_EVENT_KEYPRESS:
        r.push_back(MidiEvent(MidiEvent::NOTE_PRESSURE, ev->data.note.note, ev->data.note.velocity));
        break;
      case SND_SEQ_EVENT_CHANPRESS:
        // Channel-wide value, same union member as PITCHBEND/CONTROLLER below
        // (no specific note involved, unlike KEYPRESS's per-note aftertouch
        // above) - note field is unused.
        r.push_back(MidiEvent(MidiEvent::CHANNEL_PRESSURE, 0, ev->data.control.value));
        break;
      case SND_SEQ_EVENT_PITCHBEND:
        break;
      case SND_SEQ_EVENT_CONTROLLER:
        break;
      case SND_SEQ_EVENT_NOTEON:
        r.push_back(MidiEvent(MidiEvent::NOTE_ON, ev->data.note.note, ev->data.note.velocity));
        break;
      case SND_SEQ_EVENT_NOTEOFF:
        r.push_back(MidiEvent(MidiEvent::NOTE_OFF, ev->data.note.note, 0));
        break;
    }

    snd_seq_free_event(ev);
  } while (snd_seq_event_input_pending(seq_handle_, 0) > 0);

  return r;
}
