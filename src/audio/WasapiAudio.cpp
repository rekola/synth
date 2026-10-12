#include "WasapiAudio.h"

#include "AudioBuffer.h"
#include "WasapiEndpoints.h"
#include "../util/Logger.h"

#include <audioclient.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>
#include <vector>

using namespace std;
using winsupport::ComPtr;

namespace {

constexpr REFERENCE_TIME kHundredNs = 10000000;
// Output buffers hold this many blocks; one is queued silent at the start.
constexpr size_t kBufferBlocks = 3;

string
hex(HRESULT hr) {
  char text[16];
  snprintf(text, sizeof(text), "0x%08lX", static_cast<unsigned long>(hr));
  return text;
}

size_t
framesOf(REFERENCE_TIME duration, int rate) {
  return static_cast<size_t>(lround(static_cast<double>(duration) * rate / static_cast<double>(kHundredNs)));
}

// Block sizes are powers of two, like the periods the other backends use.
size_t
blockFor(size_t period_frames) {
  size_t block = 256;
  while (block < period_frames) block *= 2;
  return block;
}

} // namespace

struct WasapiStream {
  ComPtr<IMMDevice> device;
  ComPtr<IAudioClient> client;
  ComPtr<IAudioRenderClient> render;
  ComPtr<IAudioCaptureClient> capture;
  HANDLE event = nullptr;
  UINT32 buffer_frames = 0;
  int rate = 0;
  short channels = 0;

  WasapiStream() = default;
  WasapiStream(const WasapiStream &) = delete;
  WasapiStream & operator=(const WasapiStream &) = delete;
  ~WasapiStream() {
    if (client) client->Stop();
    if (event) CloseHandle(event);
  }

  // Frames of latency the stream adds on top of what is queued.
  size_t latencyFrames() const {
    REFERENCE_TIME latency = 0;
    if (!client || FAILED(client->GetStreamLatency(&latency))) return 0;
    return framesOf(latency, rate);
  }
};

namespace {

// Opens `name` for playback or capture. `rate` 0 takes the device's mix
// rate; otherwise the system converts to it. `block` 0 is derived from the
// device's period and returned; the buffer is made to hold a few blocks.
unique_ptr<WasapiStream>
openStream(const string & name, bool capture, int rate, short channels, size_t & block, Logger & logger) {
  const string what = capture ? "capture" : "playback";
  string error;
  auto stream = make_unique<WasapiStream>();
  stream->device = wasapi::openEndpoint(name, capture, error);
  if (!stream->device) {
    logger.log("WARNING: Can't open " + what + " device '" + name + "': " + error);
    return nullptr;
  }
  HRESULT hr = stream->device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, stream->client.putVoid());
  if (FAILED(hr)) {
    logger.log("WARNING: Can't activate " + what + " device '" + name + "': " + hex(hr));
    return nullptr;
  }

  if (rate == 0) {
    WAVEFORMATEX * mix = nullptr;
    hr = stream->client->GetMixFormat(&mix);
    if (FAILED(hr) || !mix) {
      logger.log("WARNING: Can't read the format of " + what + " device '" + name + "': " + hex(hr));
      return nullptr;
    }
    rate = static_cast<int>(mix->nSamplesPerSec);
    CoTaskMemFree(mix);
  }

  WAVEFORMATEX format = {};
  format.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
  format.nChannels = static_cast<WORD>(channels);
  format.nSamplesPerSec = static_cast<DWORD>(rate);
  format.wBitsPerSample = 32;
  format.nBlockAlign = static_cast<WORD>(channels * 4);
  format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;

  REFERENCE_TIME default_period = 0, minimum_period = 0;
  stream->client->GetDevicePeriod(&default_period, &minimum_period);
  if (default_period <= 0) default_period = kHundredNs / 100;
  size_t period_frames = framesOf(default_period, rate);
  if (block == 0) block = blockFor(period_frames);

  size_t buffer_frames = max(kBufferBlocks * block, 2 * period_frames);
  auto duration = static_cast<REFERENCE_TIME>(static_cast<double>(buffer_frames) * kHundredNs / rate);
  // The system converts channel count (and, for capture, rate) when asked to.
  const DWORD flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK | static_cast<DWORD>(AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM) |
                      AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
  hr = stream->client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, duration, 0, &format, nullptr);
  if (FAILED(hr)) {
    logger.log("WARNING: Can't start " + what + " on '" + name + "' at " + to_string(rate) + " Hz: " + hex(hr));
    return nullptr;
  }
  stream->event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  if (!stream->event || FAILED(stream->client->SetEventHandle(stream->event)) ||
      FAILED(stream->client->GetBufferSize(&stream->buffer_frames))) {
    logger.log("WARNING: Can't set up the " + what + " buffer of '" + name + "'");
    return nullptr;
  }
  hr = capture ? stream->client->GetService(__uuidof(IAudioCaptureClient), stream->capture.putVoid())
               : stream->client->GetService(__uuidof(IAudioRenderClient), stream->render.putVoid());
  if (FAILED(hr)) {
    logger.log("WARNING: Can't get the " + what + " service of '" + name + "': " + hex(hr));
    return nullptr;
  }
  stream->rate = rate;
  stream->channels = channels;
  return stream;
}

// Queues `frames` of silence so the stream has something to play from its
// first period, and starts it.
bool
startPlayback(WasapiStream & stream, size_t frames) {
  BYTE * data = nullptr;
  auto count = static_cast<UINT32>(min<size_t>(frames, stream.buffer_frames));
  if (FAILED(stream.render->GetBuffer(count, &data))) return false;
  stream.render->ReleaseBuffer(count, AUDCLNT_BUFFERFLAGS_SILENT);
  return SUCCEEDED(stream.client->Start());
}

} // namespace

WasapiAudio::WasapiAudio(int _freq, short _channels) : AudioAPI(_freq, _channels) { }

WasapiAudio::~WasapiAudio() = default;

void
WasapiAudio::initialize(Logger & logger, const DeviceSettings & devices) {
  winsupport::ensureComInitialized();

  auto try_playback = [&](const string & name) {
    size_t block = 0;
    auto stream = openStream(name, false, 0, numberOfChannels(), block, logger);
    if (!stream) return false;
    if (!startPlayback(*stream, block)) {
      logger.log("WARNING: Can't start playback on '" + name + "'");
      return false;
    }
    if (stream->rate != getFrequency()) {
      logger.log("Requested " + to_string(getFrequency()) + "Hz but the device runs at " + to_string(stream->rate) +
                 "Hz - using the device rate.");
      setFrequency(stream->rate);
    }
    block_frames_ = block;
    playback_ = std::move(stream);
    playback_name_ = isDefaultDevice(name) ? "default" : name;
    return true;
  };
  if (!try_playback(devices.playback)) {
    if (isDefaultDevice(devices.playback) || !try_playback("")) {
      logger.log("ERROR: No audio output device could be opened");
      return;
    }
    logger.log("Using the default playback device instead");
  }

  // Capture is optional: a machine without an input still plays songs.
  auto try_capture = [&](const string & name) {
    size_t block = block_frames_;
    auto stream = openStream(name, true, getFrequency(), 1, block, logger);
    if (!stream) return false;
    capture_ = std::move(stream);
    capture_name_ = isDefaultDevice(name) ? "default" : name;
    return true;
  };
  if (!try_capture(devices.capture) && !isDefaultDevice(devices.capture)) {
    if (try_capture("")) logger.log("Using the default capture device instead");
    else logger.log("WARNING: recording disabled");
  } else if (!capture_) {
    logger.log("WARNING: recording disabled");
  }

  logger.log("Playback: " + playback_name_ + " at " + to_string(getFrequency()) + " Hz, " + to_string(block_frames_) +
             " frames per block" + (capture_ ? ", capture: " + capture_name_ : string()));
}

unsigned
WasapiAudio::playbackFree() const {
  UINT32 padding = 0;
  if (FAILED(playback_->client->GetCurrentPadding(&padding))) {
    const_cast<WasapiAudio *>(this)->playback_failed_ = true;
    return static_cast<unsigned>(block_frames_);
  }
  return padding >= playback_->buffer_frames ? 0 : playback_->buffer_frames - padding;
}

WaitResult
WasapiAudio::wait(Notifier & wake, bool want_capture, int timeout_ms) {
  winsupport::ensureComInitialized();
  using Clock = chrono::steady_clock;
  const auto deadline = Clock::now() + chrono::milliseconds(max(timeout_ms, 0));

  for (;;) {
    // The device's events say when to look; what is true now decides.
    WaitResult result;
    result.woken = wake.wait(0);
    if (playback_) result.playback = playback_failed_ || playbackFree() >= block_frames_;
    if (want_capture && capture_) {
      UINT32 packet = 0;
      result.capture = SUCCEEDED(capture_->capture->GetNextPacketSize(&packet)) && packet > 0;
    }
    if (result.woken || result.playback || result.capture) return result;

    DWORD remaining = INFINITE;
    if (timeout_ms >= 0) {
      auto left = chrono::duration_cast<chrono::milliseconds>(deadline - Clock::now()).count();
      if (left <= 0) return result;
      remaining = static_cast<DWORD>(left);
    }
    HANDLE handles[3];
    DWORD count = 0;
    handles[count++] = static_cast<HANDLE>(wake.handle());
    if (playback_) handles[count++] = playback_->event;
    if (want_capture && capture_) handles[count++] = capture_->event;
    WaitForMultipleObjects(count, handles, FALSE, remaining);
  }
}

void
WasapiAudio::play(const AudioBuffer & data, Logger & logger) {
  if (!playback_) return;
  const auto frames = static_cast<size_t>(data.size());
  if (playback_failed_) {
    // A device that has gone away fails every block: pace the caller.
    if (!failure_logged_) logger.log("ERROR. The audio output device stopped working.");
    failure_logged_ = true;
    this_thread::sleep_for(chrono::microseconds(static_cast<long long>(1000000.0 * static_cast<double>(frames) / getFrequency())));
    return;
  }
  auto count = min<size_t>(frames, playbackFree());
  if (count == 0) return;
  BYTE * out = nullptr;
  HRESULT hr = playback_->render->GetBuffer(static_cast<UINT32>(count), &out);
  if (FAILED(hr)) {
    playback_failed_ = true;
    return;
  }
  auto * target = reinterpret_cast<float *>(out);
  const int data_channels = data.numberOfChannels();
  for (int c = 0; c < data_channels; c++) {
    const float * channel = data.getChannelData(c);
    for (size_t i = 0; i < count; i++) target[i * static_cast<size_t>(data_channels) + static_cast<size_t>(c)] = channel[i];
  }
  playback_->render->ReleaseBuffer(static_cast<UINT32>(count), 0);
}

AudioBuffer
WasapiAudio::record(Logger & logger) {
  if (!capture_) return AudioBuffer();
  startRecording();

  vector<float> samples;
  for (;;) {
    UINT32 packet = 0;
    HRESULT hr = capture_->capture->GetNextPacketSize(&packet);
    if (FAILED(hr)) {
      logger.log("ERROR. Can't read the audio input: " + hex(hr));
      break;
    }
    if (packet == 0) break;
    BYTE * in = nullptr;
    UINT32 frames = 0;
    DWORD flags = 0;
    if (FAILED(capture_->capture->GetBuffer(&in, &frames, &flags, nullptr, nullptr))) break;
    auto at = samples.size();
    samples.resize(at + frames);
    if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT)) memcpy(samples.data() + at, in, frames * sizeof(float));
    capture_->capture->ReleaseBuffer(frames);
  }

  AudioBuffer data(1, static_cast<int>(samples.size()));
  if (!samples.empty()) memcpy(data.getChannelData(0), samples.data(), samples.size() * sizeof(float));
  return data;
}

void
WasapiAudio::startRecording() {
  if (!capture_ || capture_started_) return;
  capture_started_ = SUCCEEDED(capture_->client->Start());
}

void
WasapiAudio::stopRecording() {
  if (!capture_ || !capture_started_) return;
  capture_->client->Stop();
  // Discard what was queued, so the next take starts clean.
  capture_->client->Reset();
  capture_started_ = false;
}

int
WasapiAudio::getPlaybackDelayFrames() const {
  if (!playback_) return 0;
  UINT32 padding = 0;
  if (FAILED(playback_->client->GetCurrentPadding(&padding))) return 0;
  return static_cast<int>(padding + playback_->latencyFrames());
}

int
WasapiAudio::getCaptureDelayFrames() const {
  if (!capture_ || !capture_started_) return 0;
  UINT32 packet = 0;
  if (FAILED(capture_->capture->GetNextPacketSize(&packet))) return 0;
  return static_cast<int>(packet + capture_->latencyFrames());
}

bool
WasapiAudio::setPlaybackDevice(const string & requested, Logger & logger) {
  winsupport::ensureComInitialized();
  string name = isDefaultDevice(requested) ? "default" : requested;
  if (name == playback_name_) return true;
  if (!playback_) {
    logger.log("ERROR: No playback device is open, can't switch");
    return false;
  }
  // Blocks and the rate are fixed by the song; the system converts the rest.
  size_t block = block_frames_;
  auto stream = openStream(requested, false, getFrequency(), numberOfChannels(), block, logger);
  if (!stream) return false;
  if (stream->buffer_frames < 2 * block_frames_) {
    logger.log("WARNING: Playback device '" + name + "' has too small a buffer for this block size");
    return false;
  }
  if (!startPlayback(*stream, block_frames_)) {
    logger.log("WARNING: Can't start playback on '" + name + "'");
    return false;
  }
  playback_ = std::move(stream);
  playback_failed_ = failure_logged_ = false;
  playback_name_ = name;
  return true;
}

bool
WasapiAudio::setCaptureDevice(const string & requested, Logger & logger) {
  winsupport::ensureComInitialized();
  string name = isDefaultDevice(requested) ? "default" : requested;
  if (capture_ && name == capture_name_) return true;
  size_t block = block_frames_;
  auto stream = openStream(requested, true, getFrequency(), 1, block, logger);
  if (!stream) return false;
  const bool was_recording = capture_started_;
  capture_ = std::move(stream);
  capture_name_ = name;
  capture_started_ = false;
  if (was_recording) startRecording();
  return true;
}
