#include "MidiInput.h"

#include "WinMmDevices.h"
#include "../launchpad/LaunchpadProtocol.h"
#include "../util/Logger.h"
#include "../util/WinSupport.h"

#include <mmsystem.h>

#include <map>
#include <mutex>

using namespace std;

namespace {

struct Source {
  MidiSourceInfo info;
  unsigned device = 0;
};

vector<Source>
listSources() {
  vector<Source> result;
  for (auto & device : winmm::inputs()) {
    // A Launchpad has a connection of its own.
    if (LaunchpadProtocol::modelFromDeviceName(device.name)) continue;
    Source source;
    source.device = device.id;
    source.info.spec = device.name;
    source.info.label = device.name;
    result.push_back(std::move(source));
  }
  // Two ports sharing a name could not be told apart on reconnect.
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

class WinMmMidiInput : public MidiInput {
 public:
  WinMmMidiInput() = default;
  ~WinMmMidiInput() override { close(); }

  void setSource(const string & spec, Logger & logger) override {
    spec_ = spec;
    close();
    if (spec_.empty()) {
      logger.log("MIDI input: none selected");
      return;
    }
    if (!open(&logger)) logger.log("MIDI input '" + spec_ + "' not found, connecting when it appears");
  }

  vector<MidiEvent> readEvents() override {
    notifier_.clear();
    if (watcher_.takeChanged() && !spec_.empty()) {
      // WinMM numbers devices by position, so a list that changed may have
      // moved or taken the chosen one.
      bool present = false;
      for (auto & source : listSources()) present = present || source.info.spec == spec_;
      if (handle_ && !present) close();
      if (!handle_ && present) open(nullptr);
    }
    vector<MidiEvent> result;
    lock_guard<mutex> lock(mutex_);
    result.swap(queue_);
    return result;
  }

  void addTo(WaitSet & waits, WaitSet::Group group) override { waits.add(group, notifier_); }

 private:
  bool open(Logger * logger) {
    for (auto & source : listSources()) {
      if (source.info.spec != spec_) continue;
      HMIDIIN handle = nullptr;
      auto r = midiInOpen(&handle, source.device, reinterpret_cast<DWORD_PTR>(&onMessage), reinterpret_cast<DWORD_PTR>(this), CALLBACK_FUNCTION);
      if (r != MMSYSERR_NOERROR) {
        if (logger) logger->log("WARNING: Can't connect MIDI input '" + source.info.label + "' (another program may be using it)");
        return false;
      }
      midiInStart(handle);
      handle_ = handle;
      if (logger) logger->log("MIDI input: " + source.info.label);
      return true;
    }
    return false;
  }

  void close() {
    if (!handle_) return;
    midiInStop(handle_);
    midiInReset(handle_);
    midiInClose(handle_);
    handle_ = nullptr;
  }

  static void CALLBACK onMessage(HMIDIIN, UINT message, DWORD_PTR instance, DWORD_PTR first, DWORD_PTR) {
    if (message != MIM_DATA) return;
    auto * self = reinterpret_cast<WinMmMidiInput *>(instance);
    auto m = winmm::unpack(first);
    switch (m.channel_status) {
      case 0x80:
        self->push(MidiEvent(MidiEvent::NOTE_OFF, static_cast<short>(m.data1), 0));
        break;
      case 0x90:
        // A note-on with no velocity is a release.
        if (m.data2 == 0) self->push(MidiEvent(MidiEvent::NOTE_OFF, static_cast<short>(m.data1), 0));
        else self->push(MidiEvent(MidiEvent::NOTE_ON, static_cast<short>(m.data1), static_cast<short>(m.data2)));
        break;
      case 0xA0:
        self->push(MidiEvent(MidiEvent::NOTE_PRESSURE, static_cast<short>(m.data1), static_cast<short>(m.data2)));
        break;
      case 0xD0:
        self->push(MidiEvent(MidiEvent::CHANNEL_PRESSURE, 0, static_cast<short>(m.data1)));
        break;
      default:
        break;
    }
  }

  void push(const MidiEvent & event) {
    {
      lock_guard<mutex> lock(mutex_);
      queue_.push_back(event);
    }
    notifier_.signal();
  }

  Notifier notifier_;
  // After notifier_: it stops before the notifier goes.
  winmm::Watcher watcher_ { notifier_ };
  HMIDIIN handle_ = nullptr;
  string spec_;
  mutex mutex_;
  vector<MidiEvent> queue_;
};

} // namespace

vector<MidiSourceInfo>
listMidiSources() {
  vector<MidiSourceInfo> result;
  for (auto & source : listSources()) result.push_back(source.info);
  return result;
}

unique_ptr<MidiInput>
createMidiInput(Logger &) {
  return make_unique<WinMmMidiInput>();
}
