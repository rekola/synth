#include "TestFramework.h"

#include "../src/launchpad/LaunchpadIO.h"
#include "../src/launchpad/LaunchpadInput.h"
#include "../src/util/StderrLogger.h"

#include <cstdlib>
#include <map>

using namespace std;

namespace {

// The variable the e2e harness sets to take simulators and leave hardware alone.
void setIgnoreHardware(bool on) {
#ifdef _WIN32
  _putenv_s("SYNTH_LAUNCHPAD_NO_HARDWARE", on ? "1" : "");
#else
  if (on) setenv("SYNTH_LAUNCHPAD_NO_HARDWARE", "1", 1);
  else unsetenv("SYNTH_LAUNCHPAD_NO_HARDWARE");
#endif
}

// A MIDI port system with no hardware: ports and incoming messages are set by
// the test, what is sent is recorded.
class FakeMidiPorts : public MidiPortBackend {
 public:
  struct Sent {
    MidiPortAddress to;
    vector<uint8_t> bytes;
  };

  bool open(Logger &) override { return true; }
  vector<MidiPortInfo> ports() override { return ports_; }
  optional<MidiPortInfo> port(MidiPortAddress address) override {
    for (auto & info : ports_) {
      if (info.address == address) return info;
    }
    return nullopt;
  }
  bool connect(MidiPortAddress address) override {
    if (refuse_connect) return false;
    connected.push_back(address);
    return true;
  }
  void disconnect(MidiPortAddress address) override { disconnected.push_back(address); }
  void sendSysEx(MidiPortAddress address, const vector<uint8_t> & bytes) override {
    sent.push_back({ address, bytes });
    if (sink) sink->push_back({ address, bytes });
  }
  vector<MidiMessage> receive() override {
    auto result = std::move(incoming);
    incoming.clear();
    return result;
  }
  void addTo(WaitSet &, WaitSet::Group) override { }

  MidiPortInfo & add(int client, int port, const string & client_name, const string & port_name, bool hardware = true) {
    MidiPortInfo info;
    info.address = { client, port };
    info.client_name = client_name;
    info.port_name = port_name;
    info.is_hardware = hardware;
    ports_.push_back(info);
    return ports_.back();
  }

  vector<MidiPortInfo> ports_;
  vector<MidiMessage> incoming;
  vector<MidiPortAddress> connected, disconnected;
  vector<Sent> sent;
  bool refuse_connect = false;
  // Outlives the backend, which the LaunchpadIO owns.
  vector<Sent> * sink = nullptr;
};

struct Rig {
  // Declared first so it outlives the LaunchpadIO, whose destructor still
  // sends (the LEDs going dark).
  vector<FakeMidiPorts::Sent> log;
  StderrLogger logger;
  FakeMidiPorts * fake = nullptr;
  unique_ptr<LaunchpadIO> io;

  Rig() {
    setIgnoreHardware(false);
    auto backend = make_unique<FakeMidiPorts>();
    fake = backend.get();
    fake->sink = &log;
    io = make_unique<LaunchpadIO>(std::move(backend));
  }
  void start() { io->initialize(logger); }
};

MidiMessage message(MidiMessage::Type type, MidiPortAddress source, int number, int value) {
  MidiMessage m;
  m.type = type;
  m.source = source;
  m.number = number;
  m.value = value;
  return m;
}

} // namespace

TEST(launchpad_io_connects_to_a_launchpad_among_the_ports_and_enters_programmer_mode) {
  Rig rig;
  rig.fake->add(20, 0, "Some Synth", "Some Synth MIDI");
  rig.fake->add(24, 1, "Launchpad X", "Launchpad X LPX MIDI In");
  rig.start();

  CHECK(rig.fake->connected.size() == 1);
  CHECK(rig.fake->connected[0] == (MidiPortAddress{ 24, 1 }));
  auto ids = rig.io->readySessionIds();
  CHECK(ids.size() == 1);
  CHECK(rig.io->modelForSession(ids[0]) == LaunchpadProtocol::Model::X);
  // Programmer mode, then the device inquiry, both to the connected port.
  CHECK(rig.fake->sent.size() == 2);
  CHECK(rig.fake->sent[0].bytes == LaunchpadProtocol::buildProgrammerModeEnter(LaunchpadProtocol::Model::X));
  CHECK(rig.fake->sent[1].bytes == LaunchpadProtocol::buildDeviceInquiry());
}

TEST(launchpad_io_prefers_the_non_daw_port_of_a_device) {
  Rig rig;
  rig.fake->add(24, 0, "Launchpad X", "Launchpad X LPX DAW In");
  rig.fake->add(24, 1, "Launchpad X", "Launchpad X LPX MIDI In");
  rig.start();

  CHECK(rig.fake->connected.size() == 1);
  CHECK(rig.fake->connected[0] == (MidiPortAddress{ 24, 1 }));

  // Truncated names (31 characters in the USB-MIDI jack name) still count.
  Rig mini;
  mini.fake->add(30, 0, "Launchpad Mini MK3", "Launchpad Mini MK3 LPMiniMK3 DA");
  mini.fake->add(30, 1, "Launchpad Mini MK3", "Launchpad Mini MK3 LPMiniMK3 MI");
  mini.start();
  CHECK(mini.fake->connected.size() == 1);
  CHECK(mini.fake->connected[0] == (MidiPortAddress{ 30, 1 }));
}

TEST(launchpad_io_connects_each_device_once_and_skips_simulators_unless_told_otherwise) {
  Rig rig;
  rig.fake->add(24, 1, "Launchpad X", "Launchpad X LPX MIDI In");
  rig.fake->add(25, 1, "Launchpad Mini MK3", "Launchpad Mini MK3 LPMiniMK3 MI");
  rig.fake->add(40, 0, "Launchpad X (e2e)", "Launchpad X LPX MIDI In", false);
  rig.start();
  CHECK(rig.io->readySessionIds().size() == 2); // the simulator is left alone

  // A test harness's run takes the simulators and leaves hardware alone.
  setIgnoreHardware(true);
  auto backend = make_unique<FakeMidiPorts>();
  auto * fake = backend.get();
  fake->add(24, 1, "Launchpad X", "Launchpad X LPX MIDI In", true);
  fake->add(40, 0, "Launchpad X (e2e)", "Launchpad X LPX MIDI In", false);
  LaunchpadIO io(std::move(backend));
  io.initialize(rig.logger);
  setIgnoreHardware(false);
  CHECK(fake->connected.size() == 1);
  CHECK(fake->connected[0] == (MidiPortAddress{ 40, 0 }));
}

TEST(launchpad_io_follows_ports_coming_and_going) {
  Rig rig;
  rig.start();
  CHECK(rig.io->readySessionIds().empty());

  // The DAW port announces first, then the real input port replaces it.
  rig.fake->add(24, 0, "Launchpad X", "Launchpad X LPX DAW In");
  rig.fake->incoming.push_back(message(MidiMessage::PORT_ADDED, { 24, 0 }, 0, 0));
  rig.io->pollEvents();
  CHECK(rig.io->readySessionIds().size() == 1);
  auto daw_session = rig.io->readySessionIds()[0];

  rig.fake->add(24, 1, "Launchpad X", "Launchpad X LPX MIDI In");
  rig.fake->incoming.push_back(message(MidiMessage::PORT_ADDED, { 24, 1 }, 0, 0));
  rig.io->pollEvents();
  auto ids = rig.io->readySessionIds();
  CHECK(ids.size() == 1);
  CHECK(ids[0] != daw_session); // a new session, never a reused id
  CHECK(rig.fake->disconnected.size() == 1);
  CHECK(rig.fake->disconnected[0] == (MidiPortAddress{ 24, 0 }));

  // A port that is not a Launchpad is ignored; unplugging ends the session.
  rig.fake->add(50, 0, "Keyboard", "Keyboard MIDI");
  rig.fake->incoming.push_back(message(MidiMessage::PORT_ADDED, { 50, 0 }, 0, 0));
  rig.fake->incoming.push_back(message(MidiMessage::PORT_REMOVED, { 24, 1 }, 0, 0));
  rig.io->pollEvents();
  CHECK(rig.io->readySessionIds().empty());
}

TEST(launchpad_io_decodes_messages_from_a_connected_device_only) {
  Rig rig;
  rig.fake->add(24, 1, "Launchpad X", "Launchpad X LPX MIDI In");
  rig.start();
  auto session = rig.io->readySessionIds()[0];

  rig.fake->incoming.push_back(message(MidiMessage::NOTE_ON, { 24, 1 }, LaunchpadProtocol::padToNoteNumber(3, 2), 90));
  rig.fake->incoming.push_back(message(MidiMessage::NOTE_ON, { 24, 1 }, LaunchpadProtocol::padToNoteNumber(3, 2), 0));
  rig.fake->incoming.push_back(message(MidiMessage::KEY_PRESSURE, { 24, 1 }, LaunchpadProtocol::padToNoteNumber(3, 2), 40));
  rig.fake->incoming.push_back(message(MidiMessage::CONTROL_CHANGE, { 24, 1 }, 95, 127));
  rig.fake->incoming.push_back(message(MidiMessage::CONTROL_CHANGE, { 24, 1 }, 95, 0));
  rig.fake->incoming.push_back(message(MidiMessage::NOTE_ON, { 99, 0 }, LaunchpadProtocol::padToNoteNumber(0, 0), 90)); // someone else

  auto events = rig.io->pollEvents();
  CHECK(events.size() == 5);

  auto * press = std::get_if<LaunchpadPadEvent>(&events[0]);
  CHECK(press != nullptr);
  CHECK(press->getKind() == LaunchpadPadEvent::PRESS);
  CHECK(press->getX() == 3 && press->getY() == 2);
  CHECK(press->getVelocity() == 90);
  CHECK(press->getDeviceIndex() == session);

  auto * release = std::get_if<LaunchpadPadEvent>(&events[1]);
  CHECK(release != nullptr && release->getKind() == LaunchpadPadEvent::RELEASE); // velocity 0
  auto * aftertouch = std::get_if<LaunchpadPadEvent>(&events[2]);
  CHECK(aftertouch != nullptr && aftertouch->getKind() == LaunchpadPadEvent::AFTERTOUCH);
  CHECK(aftertouch->getVelocity() == 40);

  auto * button_down = std::get_if<LaunchpadButtonEvent>(&events[3]);
  CHECK(button_down != nullptr && button_down->getCCNumber() == 95 && button_down->getKind() == LaunchpadButtonEvent::PRESS);
  auto * button_up = std::get_if<LaunchpadButtonEvent>(&events[4]);
  CHECK(button_up != nullptr && button_up->getKind() == LaunchpadButtonEvent::RELEASE);
}

TEST(launchpad_io_sends_leds_to_the_device_and_blanks_them_on_exit) {
  Rig rig;
  rig.fake->add(24, 1, "Launchpad X", "Launchpad X LPX MIDI In");
  rig.start();
  auto session = rig.io->readySessionIds()[0];
  auto before = rig.fake->sent.size();

  rig.io->sendLeds(session, { { LaunchpadProtocol::padToNoteNumber(0, 0), 127, 0, 0 } });
  CHECK(rig.fake->sent.size() == before + 1);
  CHECK(rig.fake->sent.back().to == (MidiPortAddress{ 24, 1 }));

  rig.io->sendLeds(session + 100, { { LaunchpadProtocol::padToNoteNumber(0, 0), 127, 0, 0 } });
  CHECK(rig.fake->sent.size() == before + 1); // no such session: nothing sent


  rig.io.reset();
  // One all-dark message to the device as the last thing it sees.
  CHECK(rig.log.size() == before + 2);
  CHECK(rig.log.back().to == (MidiPortAddress{ 24, 1 }));
}

TEST(launchpad_io_without_a_midi_system_has_no_devices) {
  StderrLogger logger;
  LaunchpadIO none(nullptr);
  none.initialize(logger);
  CHECK(none.readySessionIds().empty());
  CHECK(none.pollEvents().empty());

  struct Refusing : FakeMidiPorts {
    bool open(Logger &) override { return false; }
  };
  LaunchpadIO refused(make_unique<Refusing>());
  refused.initialize(logger);
  CHECK(refused.readySessionIds().empty());
}
