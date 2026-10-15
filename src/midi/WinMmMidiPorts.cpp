#include "MidiPorts.h"

#include "WinMmDevices.h"
#include "../util/Logger.h"
#include "../util/WinSupport.h"

#include <mmsystem.h>

#include <algorithm>
#include <deque>
#include <map>
#include <memory>
#include <mutex>

using namespace std;

namespace {

// WinMM names the further ports of a device "MIDIIN2 (name)" on the input
// side and "MIDIOUT2 (name)" on the output side; without that prefix an
// input and an output of one port have the same name.
string
baseName(const string & name) {
  for (const char * prefix : {"MIDIIN", "MIDIOUT"}) {
    string p = prefix;
    if (name.compare(0, p.size(), p) != 0) continue;
    auto open = name.find(" (", p.size());
    if (open != string::npos && name.back() == ')' && name.find_first_not_of("0123456789", p.size()) == open) {
      return name.substr(open + 2, name.size() - open - 3);
    }
  }
  return name;
}

// A port WinMM shows as an input and an output of the same name; several of
// one name pair up in the order they are listed.
struct Pair {
  string key;
  string name;
  unsigned input = 0, output = 0;
};

vector<Pair>
currentPairs() {
  auto ins = winmm::inputs();
  auto outs = winmm::outputs();
  vector<Pair> result;
  map<string, int> seen;
  vector<bool> used(outs.size(), false);
  for (auto & in : ins) {
    auto base = baseName(in.name);
    for (size_t i = 0; i < outs.size(); i++) {
      if (used[i] || baseName(outs[i].name) != base) continue;
      used[i] = true;
      int n = ++seen[base];
      result.push_back({n > 1 ? base + " (" + to_string(n) + ")" : base, in.name, in.id, outs[i].id});
      break;
    }
  }
  return result;
}

// A system-exclusive message handed to the driver, kept until it is done.
struct Outgoing {
  vector<uint8_t> bytes;
  MIDIHDR header {};
};

class WinMmMidiPorts : public MidiPortBackend {
 public:
  ~WinMmMidiPorts() override {
    while (!connections_.empty()) disconnect({connections_.begin()->first, 0});
  }

  bool open(Logger &) override {
    watcher_ = make_unique<winmm::Watcher>(notifier_);
    return true;
  }

  vector<MidiPortInfo> ports() override {
    refresh();
    vector<MidiPortInfo> result;
    for (auto & port : known_) {
      if (!port.present) continue;
      port.announced = true;
      result.push_back(describe(port));
    }
    return result;
  }

  optional<MidiPortInfo> port(MidiPortAddress address) override {
    auto * found = find(address);
    if (!found || !found->present) return nullopt;
    return describe(*found);
  }

  bool connect(MidiPortAddress address) override {
    auto * found = find(address);
    if (!found || !found->present) return false;
    if (connections_.count(found->id)) return true;

    auto connection = make_unique<Connection>();
    connection->owner = this;
    connection->id = found->id;
    if (midiInOpen(&connection->input, found->input, reinterpret_cast<DWORD_PTR>(&onMessage), reinterpret_cast<DWORD_PTR>(connection.get()),
                   CALLBACK_FUNCTION) != MMSYSERR_NOERROR) {
      return false;
    }
    if (midiOutOpen(&connection->output, found->output, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
      midiInClose(connection->input);
      return false;
    }
    midiInStart(connection->input);
    connections_[found->id] = std::move(connection);
    return true;
  }

  void disconnect(MidiPortAddress address) override {
    auto it = connections_.find(address.client);
    if (it == connections_.end()) return;
    auto & c = *it->second;
    midiInStop(c.input);
    midiInReset(c.input);
    midiInClose(c.input);
    midiOutReset(c.output);
    reap(c, true);
    midiOutClose(c.output);
    connections_.erase(it);
  }

  void sendSysEx(MidiPortAddress address, const vector<uint8_t> & bytes) override {
    auto it = connections_.find(address.client);
    if (it == connections_.end() || bytes.empty()) return;
    auto & c = *it->second;
    reap(c, false);
    // The driver is not keeping up: drop, as a full queue elsewhere would.
    if (c.pending.size() >= kMaxPending) return;

    auto message = make_unique<Outgoing>();
    message->bytes = bytes;
    message->header.lpData = reinterpret_cast<LPSTR>(message->bytes.data());
    message->header.dwBufferLength = static_cast<DWORD>(message->bytes.size());
    if (midiOutPrepareHeader(c.output, &message->header, sizeof(MIDIHDR)) != MMSYSERR_NOERROR) return;
    if (midiOutLongMsg(c.output, &message->header, sizeof(MIDIHDR)) != MMSYSERR_NOERROR) {
      midiOutUnprepareHeader(c.output, &message->header, sizeof(MIDIHDR));
      return;
    }
    c.pending.push_back(std::move(message));
  }

  vector<MidiMessage> receive() override {
    notifier_.clear();
    vector<MidiMessage> result;
    if (watcher_ && watcher_->takeChanged()) {
      refresh();
      for (auto & port : known_) {
        if (port.present && !port.announced) {
          port.announced = true;
          result.push_back({MidiMessage::PORT_ADDED, {port.id, 0}, 0, 0});
        } else if (!port.present && port.announced) {
          port.announced = false;
          result.push_back({MidiMessage::PORT_REMOVED, {port.id, 0}, 0, 0});
          // The driver has dropped it; let go of our handles.
          disconnect({port.id, 0});
        }
      }
    }
    lock_guard<mutex> lock(mutex_);
    result.insert(result.end(), queue_.begin(), queue_.end());
    queue_.clear();
    return result;
  }

  void addTo(WaitSet & waits, WaitSet::Group group) override { waits.add(group, notifier_); }

 private:
  static constexpr size_t kMaxPending = 32;

  struct Known {
    int id = 0;
    string key;
    string name;
    unsigned input = 0, output = 0;
    bool present = false;
    // Whether the caller has been told about it, by ports() or an event.
    bool announced = false;
  };

  struct Connection {
    WinMmMidiPorts * owner = nullptr;
    int id = 0;
    HMIDIIN input = nullptr;
    HMIDIOUT output = nullptr;
    deque<unique_ptr<Outgoing>> pending;
  };

  // Updates what is known from the devices WinMM lists now. A port keeps its
  // id for as long as this backend lives, however the others come and go.
  void refresh() {
    for (auto & port : known_) port.present = false;
    for (auto & pair : currentPairs()) {
      auto it = find_if(known_.begin(), known_.end(), [&](const Known & k) { return k.key == pair.key; });
      if (it == known_.end()) {
        known_.push_back({next_id_++, pair.key, pair.name, pair.input, pair.output, false, false});
        it = known_.end() - 1;
      }
      it->name = pair.name;
      it->input = pair.input;
      it->output = pair.output;
      it->present = true;
    }
  }

  Known * find(MidiPortAddress address) {
    for (auto & port : known_) {
      if (port.id == address.client) return &port;
    }
    return nullptr;
  }

  static MidiPortInfo describe(const Known & port) {
    MidiPortInfo info;
    info.address = { port.id, 0 };
    info.client_name = port.name;
    info.port_name = port.name;
    info.is_hardware = true;
    return info;
  }

  // Frees the messages the driver has finished with (all of them after a reset).
  static void reap(Connection & c, bool all) {
    while (!c.pending.empty() && (all || (c.pending.front()->header.dwFlags & MHDR_DONE))) {
      midiOutUnprepareHeader(c.output, &c.pending.front()->header, sizeof(MIDIHDR));
      c.pending.pop_front();
    }
  }

  static void CALLBACK onMessage(HMIDIIN, UINT message, DWORD_PTR instance, DWORD_PTR first, DWORD_PTR) {
    if (message != MIM_DATA) return;
    auto * c = reinterpret_cast<Connection *>(instance);
    auto m = winmm::unpack(first);
    MidiMessage out;
    out.source = { c->id, 0 };
    switch (m.channel_status) {
      case 0xB0:
        out.type = MidiMessage::CONTROL_CHANGE;
        out.number = m.data1;
        out.value = m.data2;
        break;
      case 0xD0:
        out.type = MidiMessage::CHANNEL_PRESSURE;
        out.value = m.data1;
        break;
      case 0x90:
        out.type = MidiMessage::NOTE_ON;
        out.number = m.data1;
        out.value = m.data2;
        break;
      case 0x80:
        out.type = MidiMessage::NOTE_OFF;
        out.number = m.data1;
        break;
      case 0xA0:
        out.type = MidiMessage::KEY_PRESSURE;
        out.number = m.data1;
        out.value = m.data2;
        break;
      default:
        return;
    }
    {
      lock_guard<mutex> lock(c->owner->mutex_);
      c->owner->queue_.push_back(out);
    }
    c->owner->notifier_.signal();
  }

  Notifier notifier_;
  unique_ptr<winmm::Watcher> watcher_;
  vector<Known> known_;
  int next_id_ = 1;
  map<int, unique_ptr<Connection>> connections_;
  mutex mutex_;
  vector<MidiMessage> queue_;
};

} // namespace

unique_ptr<MidiPortBackend>
createMidiPortBackend() {
  return make_unique<WinMmMidiPorts>();
}
