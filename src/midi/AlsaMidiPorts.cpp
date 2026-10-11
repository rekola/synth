#include "AlsaMidiPorts.h"
#include "../util/Logger.h"

using namespace std;

unique_ptr<MidiPortBackend>
createMidiPortBackend() {
  return make_unique<AlsaMidiPorts>();
}

AlsaMidiPorts::~AlsaMidiPorts() {
  if (seq_handle_) snd_seq_close(seq_handle_);
}

bool
AlsaMidiPorts::open(Logger & logger) {
  // Non-blocking, unlike the MIDI input's handle: this is the code that
  // *sends* MIDI, and the loop that drives it must never stall on a write.
  if (snd_seq_open(&seq_handle_, "default", SND_SEQ_OPEN_DUPLEX, SND_SEQ_NONBLOCK) < 0) {
    logger.log("MIDI: error opening ALSA sequencer");
    seq_handle_ = nullptr;
    return false;
  }
  snd_seq_set_client_name(seq_handle_, "synth-launchpad");

  our_port_ = snd_seq_create_simple_port(seq_handle_, "synth-launchpad",
    SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_READ | SND_SEQ_PORT_CAP_SUBS_WRITE,
    SND_SEQ_PORT_TYPE_APPLICATION);
  if (our_port_ < 0) {
    logger.log("MIDI: error creating sequencer port");
    snd_seq_close(seq_handle_);
    seq_handle_ = nullptr;
    return false;
  }

  // Port start and exit announcements arrive on our own port, decoded in
  // receive() with everything else - no separate thread or descriptor.
  snd_seq_connect_from(seq_handle_, our_port_, SND_SEQ_CLIENT_SYSTEM, SND_SEQ_PORT_SYSTEM_ANNOUNCE);
  return true;
}

namespace {

MidiPortInfo
describe(snd_seq_client_info_t * client_info, snd_seq_port_info_t * port_info) {
  MidiPortInfo info;
  info.address = { snd_seq_client_info_get_client(client_info), snd_seq_port_info_get_port(port_info) };
  info.client_name = snd_seq_client_info_get_name(client_info);
  info.port_name = snd_seq_port_info_get_name(port_info);
  info.is_hardware = snd_seq_client_info_get_type(client_info) == SND_SEQ_KERNEL_CLIENT;
  return info;
}

bool
readableAndWritable(snd_seq_port_info_t * port_info) {
  auto caps = snd_seq_port_info_get_capability(port_info);
  return (caps & SND_SEQ_PORT_CAP_SUBS_READ) && (caps & SND_SEQ_PORT_CAP_SUBS_WRITE);
}

} // namespace

vector<MidiPortInfo>
AlsaMidiPorts::ports() {
  vector<MidiPortInfo> result;
  if (!seq_handle_) return result;

  snd_seq_client_info_t * client_info;
  snd_seq_port_info_t * port_info;
  snd_seq_client_info_alloca(&client_info);
  snd_seq_port_info_alloca(&port_info);

  snd_seq_client_info_set_client(client_info, -1);
  while (snd_seq_query_next_client(seq_handle_, client_info) >= 0) {
    int client = snd_seq_client_info_get_client(client_info);
    if (client == snd_seq_client_id(seq_handle_)) continue;

    snd_seq_port_info_set_client(port_info, client);
    snd_seq_port_info_set_port(port_info, -1);
    while (snd_seq_query_next_port(seq_handle_, port_info) >= 0) {
      if (!readableAndWritable(port_info)) continue;
      result.push_back(describe(client_info, port_info));
    }
  }
  return result;
}

optional<MidiPortInfo>
AlsaMidiPorts::port(MidiPortAddress address) {
  if (!seq_handle_ || address.client == snd_seq_client_id(seq_handle_)) return nullopt;

  snd_seq_client_info_t * client_info;
  snd_seq_port_info_t * port_info;
  snd_seq_client_info_alloca(&client_info);
  snd_seq_port_info_alloca(&port_info);

  if (snd_seq_get_any_client_info(seq_handle_, address.client, client_info) < 0) return nullopt;
  if (snd_seq_get_any_port_info(seq_handle_, address.client, address.port, port_info) < 0) return nullopt;
  if (!readableAndWritable(port_info)) return nullopt;
  return describe(client_info, port_info);
}

bool
AlsaMidiPorts::connect(MidiPortAddress address) {
  return seq_handle_ &&
    snd_seq_connect_from(seq_handle_, our_port_, address.client, address.port) >= 0 &&
    snd_seq_connect_to(seq_handle_, our_port_, address.client, address.port) >= 0;
}

void
AlsaMidiPorts::disconnect(MidiPortAddress address) {
  if (!seq_handle_) return;
  snd_seq_disconnect_from(seq_handle_, our_port_, address.client, address.port);
  snd_seq_disconnect_to(seq_handle_, our_port_, address.client, address.port);
}

void
AlsaMidiPorts::sendSysEx(MidiPortAddress address, const vector<uint8_t> & bytes) {
  if (!seq_handle_ || our_port_ < 0) return;

  snd_seq_event_t ev;
  snd_seq_ev_clear(&ev);
  snd_seq_ev_set_source(&ev, our_port_);
  snd_seq_ev_set_dest(&ev, address.client, address.port);
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
  snd_seq_event_output_direct(seq_handle_, &ev);
}

void
AlsaMidiPorts::addTo(WaitSet & waits, WaitSet::Group group) {
  if (!seq_handle_) return;
  auto nfds = static_cast<size_t>(snd_seq_poll_descriptors_count(seq_handle_, POLLIN));
  vector<pollfd> fds(nfds);
  snd_seq_poll_descriptors(seq_handle_, fds.data(), static_cast<unsigned>(nfds), POLLIN);
  for (auto & fd : fds) waits.addFd(group, fd.fd);
}

vector<MidiMessage>
AlsaMidiPorts::receive() {
  vector<MidiMessage> result;
  if (!seq_handle_) return result;

  do {
    snd_seq_event_t * ev;
    if (snd_seq_event_input(seq_handle_, &ev) < 0) break;

    MidiMessage message;
    message.source = { ev->source.client, ev->source.port };
    bool known = true;
    switch (ev->type) {
    case SND_SEQ_EVENT_PORT_START:
      message.type = MidiMessage::PORT_ADDED;
      message.source = { ev->data.addr.client, ev->data.addr.port };
      break;
    case SND_SEQ_EVENT_PORT_EXIT:
      message.type = MidiMessage::PORT_REMOVED;
      message.source = { ev->data.addr.client, ev->data.addr.port };
      break;
    case SND_SEQ_EVENT_CONTROLLER:
      message.type = MidiMessage::CONTROL_CHANGE;
      message.number = static_cast<int>(ev->data.control.param);
      message.value = ev->data.control.value;
      break;
    case SND_SEQ_EVENT_CHANPRESS:
      message.type = MidiMessage::CHANNEL_PRESSURE;
      message.value = ev->data.control.value;
      break;
    case SND_SEQ_EVENT_NOTEON:
      message.type = MidiMessage::NOTE_ON;
      message.number = ev->data.note.note;
      message.value = ev->data.note.velocity;
      break;
    case SND_SEQ_EVENT_NOTEOFF:
      message.type = MidiMessage::NOTE_OFF;
      message.number = ev->data.note.note;
      break;
    case SND_SEQ_EVENT_KEYPRESS:
      message.type = MidiMessage::KEY_PRESSURE;
      message.number = ev->data.note.note;
      message.value = ev->data.note.velocity;
      break;
    default:
      known = false;
      break;
    }
    if (known) result.push_back(message);

    snd_seq_free_event(ev);
  } while (snd_seq_event_input_pending(seq_handle_, 0) > 0);

  return result;
}
