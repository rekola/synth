#include "AlsaAudio.h"

#include "Logger.h"
#include "AudioBuffer.h"

#include <cstdio>
#include <fmt/core.h>
#include <unistd.h>

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
  if ((r = snd_pcm_hw_params_set_access(handle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0) {
    logger.log(string("ERROR: Can't set interleaved mode: ") + snd_strerror(r));
    return 0;
  }

  if ((r = snd_pcm_hw_params_set_format(handle, hw_params, SND_PCM_FORMAT_FLOAT_LE)) < 0) {
    logger.log(string("ERROR: Can't set format: ") + snd_strerror(r));
    return 0;
  }

  if ((r = snd_pcm_hw_params_set_channels(handle, hw_params, channels)) < 0) {
    logger.log(string("ERROR: Can't set channels number: ") + snd_strerror(r));
    return 0;
  }

  unsigned int actual_rate = rate;
  if ((r = snd_pcm_hw_params_set_rate_near(handle, hw_params, &actual_rate, 0)) < 0) {
    logger.log(string("ERROR: Can't set rate: ") + snd_strerror(r));
    return 0;
  }

  unsigned int min_periods;
  int dir;
  
  if ((r = snd_pcm_hw_params_get_periods_min(hw_params, &min_periods, &dir)) < 0) {
    logger.log(string("ERROR: Can't get min periods: ") + snd_strerror(r));
    return 0;
  }

  if ((r = snd_pcm_hw_params_set_periods(handle, hw_params, min_periods > 2 ? min_periods : 2, 0)) < 0) {
    logger.log(string("ERROR: Failed to set periods: ") + snd_strerror(r));
    return 0;
  }

  snd_pcm_uframes_t min_period_size;
  if ((r = snd_pcm_hw_params_get_period_size_min(hw_params, &min_period_size, &dir)) < 0) {
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
  int wanted_period = 256;
  if ((r = snd_pcm_hw_params_set_period_size(handle, hw_params, min_period_size > wanted_period ? min_period_size : wanted_period, 0)) < 0) {
    logger.log(string("ERROR: Failed to set period size: ") + snd_strerror(r));
    return 0;
  }

  // Write parameters
  if ((r = snd_pcm_hw_params(handle, hw_params)) < 0) {
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
    if ((r = snd_pcm_hw_params_get_rate(hw_params, &negotiated_rate, 0)) < 0) {
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
  if ((r = snd_pcm_sw_params(handle, sw_params)) < 0) {
    logger.log(string("ERROR: Failed to set software parameters: ") + snd_strerror(r));
    return 0;
  }

  snd_pcm_uframes_t frames;
  snd_pcm_hw_params_get_period_size(hw_params, &frames, 0);
  
  return frames;
}

void
AlsaAudio::initialize(Logger & logger) {
  int r;

  // Open the PCM device in playback mode. Without this, there is nothing
  // useful this class can do, so give up entirely on failure.
  if ((r = snd_pcm_open(&pcm_handle, "default", SND_PCM_STREAM_PLAYBACK, 0)) < 0) {
    logger.log(string("ERROR: Can't open PCM device for playback: ") + snd_strerror(r));
    return;
  }

  unsigned int negotiated_rate = 0;
  output_frames = initialize_alsa_dev(logger, pcm_handle, getFrequency(), numberOfChannels(), &negotiated_rate);
  if (!output_frames) return;

  // Adopt whatever the device actually negotiated (see
  // initialize_alsa_dev's comment) so every caller downstream of this
  // point - main.cpp builds its ChannelConfiguration from this, before
  // Controller/Player exist - sees the rate audio will really play back
  // at, not just what was requested.
  if (negotiated_rate) setFrequency(static_cast<int>(negotiated_rate));

  // Capture (used for sampling/recording) is optional: a machine without a
  // capture device (or without permission to open one) should still be able
  // to play songs.
  if ((r = snd_pcm_open(&capture_handle, "default", SND_PCM_STREAM_CAPTURE, 0)) < 0) {
    logger.log(string("WARNING: Can't open PCM device for capture, recording disabled: ") + snd_strerror(r));
    capture_handle = nullptr;
  } else {
    // Capture always follows the now-finalized playback rate rather than
    // negotiating (and potentially adopting) a rate of its own - a second
    // adjustment here could silently pull the whole song's sample rate
    // away from what output_frames/negotiated_rate above already settled.
    input_frames = initialize_alsa_dev(logger, capture_handle, getFrequency(), 1, nullptr);
    if (!input_frames) {
      logger.log("WARNING: Can't configure capture device, recording disabled");
      snd_pcm_close(capture_handle);
      capture_handle = nullptr;
    }
  }

  if (snd_seq_open(&seq_handle, "default", SND_SEQ_OPEN_DUPLEX, 0) < 0) {
    logger.log("Error opening ALSA sequencer");
    return;
  }
  snd_seq_set_client_name(seq_handle, "musiceditor");
  if (snd_seq_create_simple_port(seq_handle, "musiceditor",
				 SND_SEQ_PORT_CAP_WRITE|SND_SEQ_PORT_CAP_SUBS_WRITE,
				 SND_SEQ_PORT_TYPE_APPLICATION) < 0) {
    logger.log("Error creating sequencer port");
    exit(1);
  }

  auto status = string("Playback: name = ") + string(snd_pcm_name(pcm_handle)) + string(", state = ") + string(snd_pcm_state_name(snd_pcm_state(pcm_handle)));
  if (capture_handle) {
    status += string(" Capture: name = ") + string(snd_pcm_name(capture_handle)) + string(", state = ") + string(snd_pcm_state_name(snd_pcm_state(capture_handle)));
  }
  logger.log(status);

  setPlaybackDescriptors(getPollDescriptors(pcm_handle));
  if (capture_handle) setCaptureDescriptors(getPollDescriptors(capture_handle));
  setMidiCaptureDescriptors(getMidiPollDescriptors(seq_handle));
}

std::vector<pollfd>
AlsaAudio::getPollDescriptors(snd_pcm_t * handle) {
  size_t nfds = snd_pcm_poll_descriptors_count(handle);
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
  size_t nfds = snd_seq_poll_descriptors_count(handle, POLLIN);

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

  int r = snd_pcm_writei(pcm_handle, tmp_ptr, data.size());
  if (r < 0) {
    r = recoverFromPcmError(logger, pcm_handle, r, "playback");
    if (r >= 0) {
      // Recovery alone only re-primes the stream (PREPARED, silent) - it
      // never actually delivers this block's audio. Retry the write now
      // that it's back so this block isn't just dropped, and so playback
      // starts accumulating toward its start threshold again immediately
      // rather than waiting for the next render block to come around.
      r = snd_pcm_writei(pcm_handle, tmp_ptr, data.size());
    }
  }
  if (r < 0) {
    logger.log(string("ERROR. Can't write to PCM device. ") + snd_strerror(r));
  }
}

AudioBuffer
AlsaAudio::record(Logger & logger) {
  if (!capture_handle) return AudioBuffer();

  startRecording();

  auto frames = snd_pcm_avail_update(capture_handle);
  AudioBuffer data(1, frames);

  if (frames) {
    int r = snd_pcm_readi(capture_handle, data.getChannelData(0), frames);
    if (r < 0) {
      r = recoverFromPcmError(logger, capture_handle, r, "capture");
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
    if ((r = snd_pcm_start(capture_handle)) < 0) {
      exit(1);
    }
    recording_started = true;
  }
}

void
AlsaAudio::stopRecording() {
  
}

vector<MidiEvent>
AlsaAudio::recordMIDI() {
  vector<MidiEvent> r;

  do {
    snd_seq_event_t *ev;
    snd_seq_event_input(seq_handle, &ev);

    switch (ev->type) {
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
