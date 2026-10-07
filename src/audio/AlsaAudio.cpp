#include "AlsaAudio.h"

#include <chrono>

#include "../util/Logger.h"
#include "AudioBuffer.h"
#include "AudioDevices.h"

#include <cstdio>
#include <fmt/core.h>
#include <unistd.h>
#include <utility>

using namespace std;

// Canonical ALSA XRUN/suspend recovery (the same shape as alsa-lib's own
// xrun_recovery() helper in its aplay/arecord examples), shared by playback
// and capture: -EPIPE is a plain under/overrun - snd_pcm_prepare() drops the
// stream back to PREPARED, ready to auto-start (playback) or be restarted
// (capture) on the next successful write/read. -ESTRPIPE means the device
// was suspended by the system (e.g. power management) - snd_pcm_resume()
// has to be polled until the hardware actually comes back; if it reports it
// can't resume at all, snd_pcm_prepare() is the same fallback as the EPIPE
// case. Returns 0 (or whatever non-negative snd_pcm_prepare() returned) on
// successful recovery, the negative error code otherwise - callers must not
// assume the stream is usable again without checking this.
static int
recoverFromPcmError(Logger & logger, snd_pcm_t * handle, int err, const char * what) {
  if (err == -EPIPE) {
    logger.log(string("XRUN (") + what + ").");
    err = snd_pcm_prepare(handle);
    if (err < 0) logger.log(string("ERROR: Can't recover from XRUN on ") + what + ": " + snd_strerror(err));
  } else if (err == -ESTRPIPE) {
    logger.log(string("Device suspended (") + what + ").");
    while ((err = snd_pcm_resume(handle)) == -EAGAIN) usleep(100 * 1000);
    if (err < 0) {
      err = snd_pcm_prepare(handle);
      if (err < 0) logger.log(string("ERROR: Can't recover from suspend on ") + what + ": " + snd_strerror(err));
    }
  }
  return err;
}

// Opens `name` (a DeviceSettings audio name). A PipeWire node is targeted by
// opening the pipewire PCM plugin through a private one-PCM config naming it -
// the plugin's own capture_node/playback_node setting - so everything after
// the open (period sizes, polling, delay queries, xrun handling) is the same
// ALSA code path as for any other device. The config is scoped to this call,
// not installed globally.
static int
openPcm(snd_pcm_t ** handle, const string & name, snd_pcm_stream_t stream) {
  if (!isPipeWireDevice(name)) {
    return snd_pcm_open(handle, isDefaultDevice(name) ? "default" : name.c_str(), stream, 0);
  }
  auto node = pipeWireNodeName(name);
  // The name goes inside a quoted config string.
  if (node.find_first_of("\"\\\n") != string::npos) return -EINVAL;
  string text = string("pcm.synth_pipewire {\n  type pipewire\n  ") +
                (stream == SND_PCM_STREAM_CAPTURE ? "capture_node" : "playback_node") + " \"" + node + "\"\n}\n";

  snd_config_t * conf = nullptr;
  int r = snd_config_top(&conf);
  if (r < 0) return r;
  snd_input_t * input = nullptr;
  r = snd_input_buffer_open(&input, text.c_str(), static_cast<ssize_t>(text.size()));
  if (r >= 0) {
    r = snd_config_load(conf, input);
    snd_input_close(input);
  }
  if (r >= 0) r = snd_pcm_open_lconf(handle, "synth_pipewire", stream, 0, conf);
  snd_config_delete(conf);
  return r;
}

AlsaAudio::~AlsaAudio() {
  if (pcm_handle) {
    snd_pcm_drain(pcm_handle);
    snd_pcm_close(pcm_handle);
  }
  if (capture_handle) {
    snd_pcm_close(capture_handle);
  }
}

static size_t initialize_alsa_dev(Logger & logger, snd_pcm_t * handle, int rate, short channels, unsigned int * out_negotiated_rate) {
  int r;
  
  // Allocate parameters object and fill it with default values
  snd_pcm_hw_params_t * hw_params;
  snd_pcm_hw_params_alloca(&hw_params);
  snd_pcm_hw_params_any(handle, hw_params);

  // Set parameters
  r = snd_pcm_hw_params_set_access(handle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED);
  if (r < 0) {
    logger.log(string("ERROR: Can't set interleaved mode: ") + snd_strerror(r));
    return 0;
  }

  r = snd_pcm_hw_params_set_format(handle, hw_params, SND_PCM_FORMAT_FLOAT_LE);

  if (r < 0) {
    logger.log(string("ERROR: Can't set format: ") + snd_strerror(r));
    return 0;
  }

  r = snd_pcm_hw_params_set_channels(handle, hw_params, static_cast<unsigned int>(channels));

  if (r < 0) {
    logger.log(string("ERROR: Can't set channels number: ") + snd_strerror(r));
    return 0;
  }

  auto actual_rate = static_cast<unsigned int>(rate);
  r = snd_pcm_hw_params_set_rate_near(handle, hw_params, &actual_rate, 0);
  if (r < 0) {
    logger.log(string("ERROR: Can't set rate: ") + snd_strerror(r));
    return 0;
  }

  unsigned int min_periods;
  int dir;

  r = snd_pcm_hw_params_get_periods_min(hw_params, &min_periods, &dir);

  if (r < 0) {
    logger.log(string("ERROR: Can't get min periods: ") + snd_strerror(r));
    return 0;
  }

  r = snd_pcm_hw_params_set_periods(handle, hw_params, min_periods > 2 ? min_periods : 2, 0);

  if (r < 0) {
    logger.log(string("ERROR: Failed to set periods: ") + snd_strerror(r));
    return 0;
  }

  snd_pcm_uframes_t min_period_size;
  r = snd_pcm_hw_params_get_period_size_min(hw_params, &min_period_size, &dir);
  if (r < 0) {
    logger.log(string("ERROR: Failed to get minimum period size: ") + snd_strerror(r));    
    return 0;
  }

  // 256 frames (~5.3ms at 48kHz, ~10.7ms buffered across the 2 periods
  // set above) rather than the old 1024 (~21ms/~43ms buffered) - that
  // buffering is the dominant term in live-note (Launchpad/keyboard)
  // input-to-sound latency, since a freshly-triggered voice only starts
  // rendering on the next period boundary and then still has to drain
  // through this many frames of queued-but-unplayed audio. Verified
  // XRUN-free against this project's "default" PCM (PipeWire's ALSA
  // compat layer) down to 32 frames with a trivial sine generator: the
  // real floor here is this process's own per-block render cost (no
  // realtime thread priority is requested anywhere - see main.cpp), not
  // the device/driver, so 256 leaves comfortable headroom rather than
  // chasing the lowest number that merely didn't glitch in that test.
  snd_pcm_uframes_t wanted_period = 256;
  r = snd_pcm_hw_params_set_period_size(handle, hw_params, min_period_size > wanted_period ? min_period_size : wanted_period, 0);
  if (r < 0) {
    logger.log(string("ERROR: Failed to set period size: ") + snd_strerror(r));
    return 0;
  }

  // Write parameters
  r = snd_pcm_hw_params(handle, hw_params);
  if (r < 0) {
    logger.log(string("ERROR: Can't set hardware parameters: ") + snd_strerror(r));
    return 0;
  }

  // set_rate_near above negotiates rather than requiring an exact match -
  // it can silently settle on something other than what was asked for.
  // Handed back to the caller (main.cpp, before it constructs Controller/
  // Player) so the render pipeline's own ChannelConfiguration - tempo/row
  // duration, oscillator/SF2 pitch, everything keyed off the sample rate
  // - is built from what the device actually agreed to, not just what
  // was requested.
  if (out_negotiated_rate) {
    unsigned int negotiated_rate = 0;
    r = snd_pcm_hw_params_get_rate(hw_params, &negotiated_rate, 0);
    if (r < 0) {
      logger.log(string("WARNING: Can't read back negotiated rate: ") + snd_strerror(r));
    } else {
      *out_negotiated_rate = negotiated_rate;
      if (static_cast<int>(negotiated_rate) != rate) {
	logger.log("Requested " + to_string(rate) + "Hz but device negotiated " +
		   to_string(negotiated_rate) + "Hz - using the negotiated rate.");
      }
    }
  }

  // avail_min: the free-space threshold (in frames) at which poll()
  // reports this PCM's fd as writable - Player::play()'s poll loop wakes
  // up and renders/writes the next block exactly when this much space has
  // opened in the ring buffer, so it needs to track wanted_period (one
  // block) to actually deliver the latency the period-size choice above
  // is for, not whatever ALSA's own default happened to be.
  snd_pcm_sw_params_t * sw_params;
  snd_pcm_sw_params_alloca(&sw_params);
  snd_pcm_sw_params_current(handle, sw_params);
  snd_pcm_sw_params_set_avail_min(handle, sw_params, wanted_period);
  r = snd_pcm_sw_params(handle, sw_params);
  if (r < 0) {
    logger.log(string("ERROR: Failed to set software parameters: ") + snd_strerror(r));
    return 0;
  }

  snd_pcm_uframes_t frames;
  snd_pcm_hw_params_get_period_size(hw_params, &frames, 0);
  
  return frames;
}

size_t
AlsaAudio::openCapture(const string & name, Logger & logger, snd_pcm_t *& handle) {
  handle = nullptr;
  int r = openPcm(&handle, name, SND_PCM_STREAM_CAPTURE);
  if (r < 0) {
    handle = nullptr;
    logger.log(string("WARNING: Can't open PCM device '") + name + "' for capture: " + snd_strerror(r));
    return 0;
  }
  // Capture always follows the now-finalized playback rate rather than
  // negotiating (and potentially adopting) a rate of its own - a second
  // adjustment here could silently pull the whole song's sample rate
  // away from what output_frames/negotiated_rate already settled.
  auto frames = initialize_alsa_dev(logger, handle, getFrequency(), 1, nullptr);
  if (!frames) {
    logger.log("WARNING: Can't configure capture device '" + name + "'");
    snd_pcm_close(handle);
    handle = nullptr;
  }
  return frames;
}

void AlsaAudio::initialize(Logger & logger, const DeviceSettings & devices) {
  int r;

  // Open the PCM device in playback mode. Without this, there is nothing
  // useful this class can do, so give up entirely on failure. A chosen
  // device that has gone away falls back to the system default first.
  auto try_playback = [&](const string & name) {
    if (!isDefaultDevice(name) && !playbackDeviceExists(name)) {
      logger.log("WARNING: Playback device '" + name + "' not found");
      return false;
    }
    r = openPcm(&pcm_handle, name, SND_PCM_STREAM_PLAYBACK);
    if (r < 0) {
      pcm_handle = nullptr;
      logger.log(string("ERROR: Can't open PCM device '") + name + "' for playback: " + snd_strerror(r));
      return false;
    }
    unsigned int negotiated_rate = 0;
    output_frames = initialize_alsa_dev(logger, pcm_handle, getFrequency(), numberOfChannels(), &negotiated_rate);
    if (!output_frames) {
      snd_pcm_close(pcm_handle);
      pcm_handle = nullptr;
      return false;
    }
    // Adopt whatever the device actually negotiated (see
    // initialize_alsa_dev's comment) so every caller downstream of this
    // point - main.cpp builds its ChannelConfiguration from this, before
    // Controller/Player exist - sees the rate audio will really play back
    // at, not just what was requested.
    if (negotiated_rate) setFrequency(static_cast<int>(negotiated_rate));
    playback_device_name_ = isDefaultDevice(name) ? "default" : name;
    return true;
  };
  if (!try_playback(devices.playback)) {
    if (isDefaultDevice(devices.playback) || !try_playback("default")) return;
    logger.log("Using the default playback device instead");
  }

  // Capture (used for sampling/recording) is optional: a machine without a
  // capture device (or without permission to open one) should still be able
  // to play songs.
  auto try_capture = [&](const string & name) {
    if (!isDefaultDevice(name) && !captureDeviceExists(name)) {
      logger.log("WARNING: Capture device '" + name + "' not found");
      return false;
    }
    input_frames = openCapture(isDefaultDevice(name) ? "default" : name, logger, capture_handle);
    if (input_frames) capture_device_name_ = isDefaultDevice(name) ? "default" : name;
    return input_frames != 0;
  };
  if (!try_capture(devices.capture) && !isDefaultDevice(devices.capture)) {
    if (try_capture("default"))
      logger.log("Using the default capture device instead");
    else
      logger.log("WARNING: recording disabled");
  } else if (!capture_handle) {
    logger.log("WARNING: recording disabled");
  }

  // MIDI is optional like capture: a machine without the sequencer (no
  // snd-seq module, say) still plays and records audio.
  if (snd_seq_open(&seq_handle, "default", SND_SEQ_OPEN_DUPLEX, 0) < 0) {
    seq_handle = nullptr;
    logger.log("Error opening ALSA sequencer, MIDI input disabled");
  } else {
    snd_seq_set_client_name(seq_handle, "synth");
    midi_own_port_ = snd_seq_create_simple_port(seq_handle, "synth",
                                                SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE,
                                                SND_SEQ_PORT_TYPE_APPLICATION);
    if (midi_own_port_ < 0) {
      logger.log("Error creating sequencer port");
      exit(1);
    }
    // Port and client announcements, so a chosen MIDI source that is plugged
    // in later (or replugged) gets connected - see recordMIDI().
    snd_seq_connect_from(seq_handle, midi_own_port_, SND_SEQ_CLIENT_SYSTEM, SND_SEQ_PORT_SYSTEM_ANNOUNCE);
  }

  auto status = string("Playback: name = ") + string(snd_pcm_name(pcm_handle)) + string(", state = ") + string(snd_pcm_state_name(snd_pcm_state(pcm_handle)));
  if (capture_handle) {
    status += string(" Capture: name = ") + string(snd_pcm_name(capture_handle)) + string(", state = ") + string(snd_pcm_state_name(snd_pcm_state(capture_handle)));
  }
  logger.log(status);

  setPlaybackDescriptors(getPollDescriptors(pcm_handle));
  if (capture_handle) setCaptureDescriptors(getPollDescriptors(capture_handle));
  if (seq_handle) setMidiCaptureDescriptors(getMidiPollDescriptors(seq_handle));

  if (!devices.midi_input.empty()) setMidiInput(devices.midi_input, logger);
}

bool AlsaAudio::setPlaybackDevice(const string & requested, Logger & logger) {
  string name = isDefaultDevice(requested) ? "default" : requested;
  if (name == playback_device_name_) return true;
  if (!pcm_handle) {
    logger.log("ERROR: No playback device is open, can't switch");
    return false;
  }
  if (!playbackDeviceExists(name)) {
    logger.log("WARNING: Playback device '" + name + "' not found");
    return false;
  }
  snd_pcm_t * handle = nullptr;
  int r = openPcm(&handle, name, SND_PCM_STREAM_PLAYBACK);
  if (r < 0) {
    logger.log(string("WARNING: Can't open PCM device '") + name + "' for playback: " + snd_strerror(r));
    return false;
  }
  unsigned int negotiated_rate = 0;
  auto frames = initialize_alsa_dev(logger, handle, getFrequency(), numberOfChannels(), &negotiated_rate);
  // The song, the visualization thread and every tempo-derived figure were
  // built for this rate and block size; a device that can't match both can't
  // be switched to while running.
  if (!frames || frames != output_frames || (negotiated_rate && static_cast<int>(negotiated_rate) != getFrequency())) {
    if (frames) logger.log("WARNING: Playback device '" + name + "' can't run at the current rate and block size; restart to use it");
    snd_pcm_close(handle);
    return false;
  }
  auto old = pcm_handle;
  pcm_handle = handle;
  playback_device_name_ = name;
  last_play_error_ = 0;
  setPlaybackDescriptors(getPollDescriptors(pcm_handle));
  // Dropped, not drained: draining would stall the audio thread for the
  // length of the old queue.
  snd_pcm_drop(old);
  snd_pcm_close(old);
  return true;
}

bool AlsaAudio::setCaptureDevice(const string & requested, Logger & logger) {
  string name = isDefaultDevice(requested) ? "default" : requested;
  if (capture_handle && name == capture_device_name_) return true;
  if (!captureDeviceExists(name)) {
    logger.log("WARNING: Capture device '" + name + "' not found");
    return false;
  }
  snd_pcm_t * handle = nullptr;
  auto frames = openCapture(name, logger, handle);
  if (!frames) return false;
  if (capture_handle) {
    if (recording_started) snd_pcm_drop(capture_handle);
    snd_pcm_close(capture_handle);
  }
  capture_handle = handle;
  input_frames = frames;
  capture_device_name_ = name;
  // The new stream is prepared but not started, the state startRecording()
  // expects.
  recording_started = false;
  setCaptureDescriptors(getPollDescriptors(capture_handle));
  return true;
}

void AlsaAudio::setMidiInput(const string & spec, Logger & logger) {
  midi_input_spec_ = spec;
  connectMidiInput(&logger);
}

void AlsaAudio::connectMidiInput(Logger * logger) {
  if (!seq_handle) {
    if (logger && !midi_input_spec_.empty()) logger->log("MIDI input unavailable: no ALSA sequencer");
    return;
  }
  if (midi_connected_client_ >= 0) {
    snd_seq_disconnect_from(seq_handle, midi_own_port_, midi_connected_client_, midi_connected_port_);
    midi_connected_client_ = midi_connected_port_ = -1;
  }
  if (midi_input_spec_.empty()) {
    if (logger) logger->log("MIDI input: none selected");
    return;
  }
  for (auto & source : listMidiSources()) {
    if (source.spec != midi_input_spec_) continue;
    int r = snd_seq_connect_from(seq_handle, midi_own_port_, source.client, source.port);
    if (r < 0) {
      if (logger) logger->log(string("WARNING: Can't connect MIDI input '") + source.label + "': " + snd_strerror(r));
    } else {
      midi_connected_client_ = source.client;
      midi_connected_port_ = source.port;
      if (logger) logger->log("MIDI input: " + source.label);
    }
    return;
  }
  if (logger) logger->log("MIDI input '" + midi_input_spec_ + "' not found, connecting when it appears");
}

std::vector<pollfd>
AlsaAudio::getPollDescriptors(snd_pcm_t * handle) {
  auto nfds = static_cast<size_t>(snd_pcm_poll_descriptors_count(handle));
  struct pollfd * pfds = (struct pollfd *)alloca(sizeof(struct pollfd) * (nfds + 1));
    
  if (snd_pcm_poll_descriptors(handle, pfds, nfds) < 0) {
    fmt::print(stderr, "Error getting descriptor\n");
    exit(1);
  }

  vector<pollfd> r;
  for (size_t i = 0; i < nfds; i++) {
    r.push_back(pfds[i]);
  }
  return r;
}

std::vector<pollfd>
AlsaAudio::getMidiPollDescriptors(snd_seq_t * handle) {
  auto nfds = static_cast<size_t>(snd_seq_poll_descriptors_count(handle, POLLIN));

  struct pollfd * pfds = (struct pollfd *)alloca(sizeof(struct pollfd) * (nfds + 1));

  if (snd_seq_poll_descriptors(handle, pfds, nfds, POLLIN) < 0) {
    fmt::print(stderr, "Error getting descriptor\n");
    exit(1);
  }

  vector<pollfd> r;
  for (size_t i = 0; i < nfds; i++) {
    r.push_back(pfds[i]);
  }
  return r;
}

void
AlsaAudio::play(const AudioBuffer & data, Logger & logger) {
  auto numChannels = data.numberOfChannels();
  auto tmp_data = unique_ptr<float[]>(new float[data.size() * numChannels]);
  auto tmp_ptr = tmp_data.get();

  for (int j = 0; j < numChannels; j++) {
    auto channel_data = data.getChannelData(j);
    for (int i = 0; i < data.size(); i++) {
      tmp_ptr[i * numChannels + j] = channel_data[i];
    }
  }

  int r = snd_pcm_writei(pcm_handle, tmp_ptr, static_cast<snd_pcm_uframes_t>(data.size()));
  if (r < 0) {
    r = recoverFromPcmError(logger, pcm_handle, r, "playback");
    if (r >= 0) {
      // Recovery alone only re-primes the stream (PREPARED, silent) - it
      // never actually delivers this block's audio. Retry the write now
      // that it's back so this block isn't just dropped, and so playback
      // starts accumulating toward its start threshold again immediately
      // rather than waiting for the next render block to come around.
      r = snd_pcm_writei(pcm_handle, tmp_ptr, static_cast<snd_pcm_uframes_t>(data.size()));
    }
  }
  if (r < 0) {
    // A device that has gone away fails every block: log it once per
    // distinct error (and again every few seconds), not once per block,
    // and pace the caller so its poll loop doesn't spin on the dead fd.
    auto now = chrono::steady_clock::now();
    if (r != last_play_error_ || now - last_play_error_log_ > chrono::seconds(5)) {
      logger.log(string("ERROR. Can't write to PCM device. ") + snd_strerror(r));
      last_play_error_ = r;
      last_play_error_log_ = now;
    }
    usleep(static_cast<useconds_t>(1000000.0 * static_cast<double>(data.size()) / getFrequency()));
  } else if (last_play_error_ != 0) {
    logger.log("PCM device is writable again.");
    last_play_error_ = 0;
  }
}

AudioBuffer
AlsaAudio::record(Logger & logger) {
  if (!capture_handle) return AudioBuffer();

  startRecording();

  auto avail = snd_pcm_avail_update(capture_handle);
  if (avail < 0) {
    // A negative return here is an error code (e.g. -EPIPE on an XRUN), not
    // a frame count - passing it straight through as the AudioBuffer's own
    // frame count and then on to snd_pcm_readi() below (cast to the
    // unsigned snd_pcm_uframes_t it takes) turned a small negative int into
    // a huge read request against a buffer that was never actually
    // allocated for it, tripping ALSA's own `size == 0 || buffer` assertion.
    int r = recoverFromPcmError(logger, capture_handle, static_cast<int>(avail), "capture");
    if (r >= 0) {
      // Unlike playback, capture never auto-starts just by accumulating
      // reads - it needs an explicit snd_pcm_start(), same as the very
      // first call (see startRecording()). recording_started latches
      // that to a one-shot, so without clearing it here the stream would
      // sit at PREPARED forever after recovering from an XRUN: never
      // running, never producing frames again.
      recording_started = false;
      startRecording();
    } else {
      logger.log(string("ERROR. Can't read PCM device. ") + snd_strerror(static_cast<int>(avail)));
    }
    return AudioBuffer();
  }

  auto frames = static_cast<int>(avail);
  AudioBuffer data(1, frames);

  if (frames > 0) {
    int r = snd_pcm_readi(capture_handle, data.getChannelData(0), static_cast<snd_pcm_uframes_t>(frames));
    if (r < 0) {
      r = recoverFromPcmError(logger, capture_handle, r, "capture");
      if (r >= 0) {
        recording_started = false;
        startRecording();
      } else {
        logger.log(string("ERROR. Can't read PCM device. ") + snd_strerror(r));
      }
    }
  }

  return data;
}

void
AlsaAudio::startRecording() {
  if (!capture_handle) return;

  if (!recording_started) {
    int r;
    r = snd_pcm_start(capture_handle);
    if (r < 0) {
      exit(1);
    }
    recording_started = true;
  }
}

void
AlsaAudio::stopRecording() {
  if (!capture_handle || !recording_started) return;

  // Actually halts the stream and discards whatever's sitting in its own
  // buffer, unread - without this, the stream kept running the entire
  // time capture wasn't needed (Player.cpp only ever stops *reading* from
  // it, never stops it at the ALSA level), silently accumulating however
  // much audio arrived during that whole gap. The next arm's own first
  // record() call would otherwise read all of that backlog at once,
  // indistinguishable from genuine signal - stale audio (an earlier,
  // unrelated arm/disarm cycle's own leftover capture) landing at the
  // front of a brand new take instead of silence.
  snd_pcm_drop(capture_handle);
  // Back to PREPARED - snd_pcm_start() (startRecording()) requires it,
  // the same state initialize_alsa_dev() already left this stream in
  // before its very first start.
  snd_pcm_prepare(capture_handle);
  recording_started = false;
}

// snd_pcm_delay() is the standard ALSA call for exactly this - how many
// frames are sitting in this stream's own ring buffer right now, ahead of
// wherever the hardware pointer actually is. Its accuracy/availability can
// vary by driver (this project's own "default" PCM in practice is
// PipeWire's ALSA compat layer - initialize_alsa_dev()'s own period-size
// comment already notes this), so a negative delay (can happen transiently
// around an XRUN) or a failed call both fail open to 0 - AudioAPI's own
// doc comment on why that's the right default, not an error.
int
AlsaAudio::getPlaybackDelayFrames() const {
  if (!pcm_handle) return 0;
  snd_pcm_sframes_t delay = 0;
  if (snd_pcm_delay(pcm_handle, &delay) < 0 || delay < 0) return 0;
  return static_cast<int>(delay);
}

int
AlsaAudio::getCaptureDelayFrames() const {
  if (!capture_handle) return 0;
  snd_pcm_sframes_t delay = 0;
  if (snd_pcm_delay(capture_handle, &delay) < 0 || delay < 0) return 0;
  return static_cast<int>(delay);
}

vector<MidiEvent>
AlsaAudio::recordMIDI() {
  vector<MidiEvent> r;

  do {
    snd_seq_event_t *ev;
    snd_seq_event_input(seq_handle, &ev);

    switch (ev->type) {
      case SND_SEQ_EVENT_PORT_START:
        // A chosen source that wasn't there, or was unplugged, is back.
        if (midi_connected_client_ < 0 && !midi_input_spec_.empty() && ev->data.addr.client != snd_seq_client_id(seq_handle)) {
          connectMidiInput(nullptr);
        }
        break;
      case SND_SEQ_EVENT_PORT_EXIT:
      case SND_SEQ_EVENT_CLIENT_EXIT:
        // ALSA drops the subscription itself; just remember that it's gone.
        if (ev->data.addr.client == midi_connected_client_ &&
            (ev->type == SND_SEQ_EVENT_CLIENT_EXIT || ev->data.addr.port == midi_connected_port_)) {
          midi_connected_client_ = midi_connected_port_ = -1;
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
  } while (snd_seq_event_input_pending(seq_handle, 0) > 0);
  
  return r;
}
