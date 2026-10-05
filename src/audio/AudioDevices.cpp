#include "AudioDevices.h"

#include "DeviceSettings.h"
#include "../launchpad/LaunchpadProtocol.h"

#include <alsa/asoundlib.h>

#include <algorithm>
#include <map>
#include <mutex>

#ifdef SYNTH_HAVE_PIPEWIRE
#include <pipewire/pipewire.h>
#include <spa/utils/dict.h>
#endif

using namespace std;

namespace {

struct Node {
  string name;
  string label;
  bool is_source = false;
  bool is_sink = false;
};

#ifdef SYNTH_HAVE_PIPEWIRE

struct Snapshot {
  pw_main_loop * loop = nullptr;
  pw_context * context = nullptr;
  pw_core * core = nullptr;
  pw_registry * registry = nullptr;
  spa_hook core_listener{};
  spa_hook registry_listener{};
  int pending = 0;
  vector<Node> nodes;
};

void onGlobal(void * data, uint32_t, uint32_t, const char * type, uint32_t, const spa_dict * props) {
  if (!props || strcmp(type, PW_TYPE_INTERFACE_Node) != 0) return;
  auto * s = static_cast<Snapshot *>(data);
  auto * media_class = spa_dict_lookup(props, PW_KEY_MEDIA_CLASS);
  auto * name = spa_dict_lookup(props, PW_KEY_NODE_NAME);
  if (!media_class || !name) return;
  string cls = media_class;
  Node node;
  node.name = name;
  node.is_source = cls == "Audio/Source" || cls == "Audio/Source/Virtual" || cls == "Audio/Duplex";
  node.is_sink = cls == "Audio/Sink" || cls == "Audio/Duplex";
  if (!node.is_source && !node.is_sink) return;
  auto * description = spa_dict_lookup(props, PW_KEY_NODE_DESCRIPTION);
  auto * nick = spa_dict_lookup(props, PW_KEY_NODE_NICK);
  node.label = description ? description : (nick ? nick : name);
  s->nodes.push_back(std::move(node));
}

void onDone(void * data, uint32_t id, int seq) {
  auto * s = static_cast<Snapshot *>(data);
  if (id == PW_ID_CORE && seq == s->pending) pw_main_loop_quit(s->loop);
}

void onError(void * data, uint32_t, int, int, const char *) {
  pw_main_loop_quit(static_cast<Snapshot *>(data)->loop);
}

void onTimeout(void * data, uint64_t) {
  pw_main_loop_quit(static_cast<Snapshot *>(data)->loop);
}

const pw_registry_events kRegistryEvents = [] {
  pw_registry_events e{};
  e.version = PW_VERSION_REGISTRY_EVENTS;
  e.global = onGlobal;
  return e;
}();

const pw_core_events kCoreEvents = [] {
  pw_core_events e{};
  e.version = PW_VERSION_CORE_EVENTS;
  e.done = onDone;
  e.error = onError;
  return e;
}();

// One round trip to the daemon: connect, collect every node the registry
// announces up to the sync point, disconnect. False when no daemon answers.
bool takeSnapshot(vector<Node> & nodes) {
  static once_flag init_once;
  call_once(init_once, [] { pw_init(nullptr, nullptr); });

  Snapshot s;
  s.loop = pw_main_loop_new(nullptr);
  if (!s.loop) return false;
  bool ok = false;
  s.context = pw_context_new(pw_main_loop_get_loop(s.loop), nullptr, 0);
  if (s.context) s.core = pw_context_connect(s.context, nullptr, 0);
  if (s.core) {
    pw_core_add_listener(s.core, &s.core_listener, &kCoreEvents, &s);
    s.registry = pw_core_get_registry(s.core, PW_VERSION_REGISTRY, 0);
    pw_registry_add_listener(s.registry, &s.registry_listener, &kRegistryEvents, &s);
    s.pending = pw_core_sync(s.core, PW_ID_CORE, 0);

    // A daemon that accepts the connection but never answers must not hang
    // the caller (the UI thread).
    auto * loop = pw_main_loop_get_loop(s.loop);
    auto * timer = pw_loop_add_timer(loop, onTimeout, &s);
    timespec timeout{1, 0}, interval{0, 0};
    pw_loop_update_timer(loop, timer, &timeout, &interval, false);
    pw_main_loop_run(s.loop);
    pw_loop_destroy_source(loop, timer);

    nodes = std::move(s.nodes);
    ok = true;
    spa_hook_remove(&s.registry_listener);
    pw_proxy_destroy(reinterpret_cast<pw_proxy *>(s.registry));
    spa_hook_remove(&s.core_listener);
    pw_core_disconnect(s.core);
  }
  if (s.context) pw_context_destroy(s.context);
  pw_main_loop_destroy(s.loop);
  return ok;
}

#else

bool takeSnapshot(vector<Node> &) { return false; }

#endif

// ALSA's own PCM list: all there is without PipeWire, and little more than
// "default" with it. `want_input` picks capture-capable entries.
vector<AudioDeviceInfo> alsaDevices(bool want_input) {
  vector<AudioDeviceInfo> result;
  void ** hints = nullptr;
  if (snd_device_name_hint(-1, "pcm", &hints) < 0) return result;
  for (void ** h = hints; *h; h++) {
    char * name = snd_device_name_get_hint(*h, "NAME");
    char * description = snd_device_name_get_hint(*h, "DESC");
    char * ioid = snd_device_name_get_hint(*h, "IOID");
    bool usable = !ioid || string(ioid) == (want_input ? "Input" : "Output");
    if (name && usable && string(name) != "default" && string(name) != "null") {
      string label = description ? description : name;
      auto newline = label.find('\n');
      if (newline != string::npos) label = label.substr(0, newline);
      result.push_back({name, label});
    }
    free(name);
    free(description);
    free(ioid);
  }
  snd_device_name_free_hint(hints);
  return result;
}

vector<AudioDeviceInfo> listDevices(bool want_input) {
  vector<AudioDeviceInfo> devices;
  devices.push_back({"", "System default"});
  vector<Node> nodes;
  if (takeSnapshot(nodes)) {
    for (auto & node : nodes) {
      if (want_input ? node.is_source : node.is_sink) {
        devices.push_back({string(kPipeWirePrefix) + node.name, node.label});
      }
    }
  } else {
    for (auto & d : alsaDevices(want_input)) devices.push_back(std::move(d));
  }
  uniquifyLabels(devices);
  return devices;
}

bool nodeExists(const string & name, bool want_input) {
  if (!isPipeWireDevice(name)) return true;
  vector<Node> nodes;
  if (!takeSnapshot(nodes)) return true;
  auto node_name = pipeWireNodeName(name);
  return any_of(nodes.begin(), nodes.end(), [&](const Node & n) {
    return n.name == node_name && (want_input ? n.is_source : n.is_sink);
  });
}

} // namespace

bool pipeWireAvailable() {
  vector<Node> nodes;
  return takeSnapshot(nodes);
}

vector<AudioDeviceInfo>
listCaptureDevices() {
  return listDevices(true);
}

vector<AudioDeviceInfo>
listPlaybackDevices() {
  return listDevices(false);
}

bool captureDeviceExists(const string & name) {
  return nodeExists(name, true);
}

bool playbackDeviceExists(const string & name) {
  return nodeExists(name, false);
}

void uniquifyLabels(vector<AudioDeviceInfo> & devices) {
  map<string, int> seen;
  for (auto & device : devices) {
    int n = ++seen[device.label];
    if (n > 1) device.label += " (" + to_string(n) + ")";
  }
}

vector<MidiSourceInfo>
listMidiSources() {
  vector<MidiSourceInfo> result;
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

      MidiSourceInfo info;
      info.client = client;
      info.port = snd_seq_port_info_get_port(port_info);
      info.spec = client_name + ":" + port_name;
      info.label = client_name == port_name ? client_name : client_name + ": " + port_name;
      result.push_back(std::move(info));
    }
  }
  snd_seq_close(seq);

  // Two ports sharing a spec could not be told apart on reconnect.
  map<string, int> seen;
  for (auto & info : result) {
    int n = ++seen[info.spec];
    if (n > 1) {
      info.spec += " (" + to_string(n) + ")";
      info.label += " (" + to_string(n) + ")";
    }
  }
  return result;
}
