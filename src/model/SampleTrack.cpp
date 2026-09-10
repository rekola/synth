#include "SampleTrack.h"
#include "Clip.h"
#include "SampleContent.h"
#include "../state/SampleTrackState.h"
#include "../state/PositionedVoice.h"
#include "../dsp/Resampler.h"
#include "../audio/TimeStretcher.h"

#include <cmath>

using namespace std;

namespace {

// Plain one-shot raw-sample playback voice, fed a Clip's own buffer and
// its already-resolved (clamped) in/out frame bounds - the successor to
// the never-instantiated FileInstrumentVoice this replaces (see
// FileInstrument's removal): click-safe start at a specific frame (not
// always 0), PositionedVoice::encodePosition() for spatial placement/
// gain, no pitch handling at all - a sample clip always plays back at
// its own native rate, never note-pitch-following, so this derives from
// PositionedVoice directly rather than InstrumentVoice - there's no
// frequency/detune/phase-accumulator concept to inherit or repurpose
// here, just its own plain frame index. Deliberately has no looping
// concept of its own, and no idea why or when it's being started or
// stopped either - both are entirely SampleTrackState::render()'s own
// concern (its own chunked loop against RenderContext::
// getPendingSampleEvents(), mirroring how a pattern note's own chunked
// render already works), not this voice's. A *looping* clip is realized
// by its own track creating a fresh voice each lap (SongState.h's
// per-row scheduling, mirroring how a looping Pattern's own note re-fires
// every time its row wraps around, and how LaunchpadManager::
// fireOrTriggerClipStep() already does this for Session-view triggering)
// rather than one voice looping internally - closer to how every other
// track type already works, and it naturally gets both halves of "the
// clip's own length is the loop, not the audio's" for free: a new
// trigger's own stopVoices(0) fades out whatever's still sounding from
// the previous lap if it ran long, and a shorter one simply finishes and
// stays silent until the next trigger arrives, no dedicated lap-timing
// logic of its own needed here at all.
class SampleClipVoice : public PositionedVoice {
public:
  // `playback_ratio` is native frames per output frame - 1.0 for
  // ordinary same-rate playback, or content-native-rate/output-rate for
  // real-time resampling (resolveRealtimeSampleAudio()'s own comment).
  // `start_frame` is a fractional position for the same reason.
  SampleClipVoice(const ChannelConfiguration & channel_config, const SphericalPosition & position, shared_ptr<AudioBuffer> samples, double start_frame, int64_t end_frame, double playback_ratio, const SendLevels & sends)
    : PositionedVoice(channel_config, position, sends),
      samples_(move(samples)), source_position_(start_frame), end_frame_(end_frame), playback_ratio_(playback_ratio),
      release_length_frames_(std::max(1, static_cast<int>(kReleaseSeconds * channel_config.getAudioOutSampleRate()))) {
    // Full velocity, unity gain, a fixed identity - triggerClip() always
    // fires a fresh voice at the same nominal strength (there's no
    // performance-velocity input for a Session-view/transport-triggered
    // clip the way a played note has); note_value_ still gets a real
    // value (0, not -1) so getOwnLoudnessFactor()/getAllActiveVoices()
    // report this voice as genuinely active for LED/UI feedback.
    velocity_ = 1.0f;
    note_value_ = 0;
  }

  AudioBuffer render(int frames) override {
    auto base_gain = decibelsToGain(getGainDB());
    if (static_cast<int>(dry_.size()) != frames) dry_.resize(static_cast<size_t>(frames));

    auto data = samples_->getChannelData(0);
    auto total_frames = samples_->numberOfFrames();
    for (int k = 0; k < frames; k++) {
      if (!active_) {
        dry_[static_cast<size_t>(k)] = 0.0f;
        continue;
      }

      // A short linear fade rather than an abrupt cut - stopNote()/
      // killNote()/fastRelease() all just start it (below); reaching 0
      // here is what actually retires the voice, not the stop call
      // itself, so whatever's already sounding never jumps straight to
      // silence.
      float gain = base_gain;
      if (releasing_) {
        gain *= static_cast<float>(release_frames_remaining_) / static_cast<float>(release_length_frames_);
        if (--release_frames_remaining_ <= 0) active_ = false;
      }

      if (source_position_ >= static_cast<double>(end_frame_)) {
        active_ = false;
        dry_[static_cast<size_t>(k)] = 0.0f;
        continue;
      }

      // Linear interpolation between the two source samples straddling
      // this fractional position - degenerates to reading exactly one
      // real sample per output sample (frac always 0) when
      // playback_ratio_ is 1.0, the ordinary same-rate case.
      auto idx0 = static_cast<int64_t>(source_position_);
      auto idx1 = idx0 + 1 < total_frames ? idx0 + 1 : idx0;
      auto frac = static_cast<float>(source_position_ - static_cast<double>(idx0));
      auto sample = data[idx0] * (1.0f - frac) + data[idx1] * frac;

      dry_[static_cast<size_t>(k)] = sample * gain;
      source_position_ += playback_ratio_;
    }
    return encodePosition(dry_.data(), frames);
  }

  void stopNote() override { beginRelease(); }
  void killNote() override { beginRelease(); }
  void fastRelease() override { beginRelease(); }
  bool isActive() const override { return active_; }

private:
  // Idempotent - a second stop/choke on an already-releasing voice
  // (e.g. SongState.h's transition-detection firing right after
  // triggerClip()'s own stopVoices(0) already started one) must not
  // restart or prolong the ramp.
  void beginRelease() {
    if (releasing_) return;
    releasing_ = true;
    release_frames_remaining_ = release_length_frames_;
  }

  static constexpr float kReleaseSeconds = 0.01f;

  shared_ptr<AudioBuffer> samples_;
  vector<float> dry_;
  double source_position_;
  // int64_t: a 32-bit frame count runs short well within a plausible
  // session at high sample rates (~3 hours at 192kHz).
  int64_t end_frame_;
  double playback_ratio_;
  bool active_ = true;
  bool releasing_ = false;
  int release_frames_remaining_ = 0;
  int release_length_frames_;
};

}

ResolvedSampleAudio
resolveSampleAudio(const SampleContent & content, int output_rate, int song_tempo) {
  ResolvedSampleAudio result;
  if (!content.getBuffer()) return result;

  // Resampled on demand, never in place - content.getBuffer() itself is
  // never mutated to do this (see SampleContent::getNativeSampleRate()'s
  // own doc comment on why: saving must always write the original,
  // untouched audio, regardless of whatever output rate the current
  // session happens to be running under). Not cached (a per-trigger
  // resample is cheap relative to how rarely a clip is actually
  // (re-)triggered - a real cache is a plausible future optimization, not
  // needed for correctness).
  auto native_rate = content.getNativeSampleRate();
  shared_ptr<AudioBuffer> samples = content.getBuffer();
  if (native_rate > 0 && native_rate != output_rate) {
    auto frames = samples->numberOfFrames();
    vector<float> mono(static_cast<size_t>(frames));
    auto src = samples->getChannelData(0);
    for (int i = 0; i < frames; i++) mono[static_cast<size_t>(i)] = src[i];
    auto resampled = resampleMonoLinear(mono, native_rate, output_rate);
    if (!resampled.empty()) {
      auto buf = make_shared<AudioBuffer>(1, static_cast<int>(resampled.size()));
      auto dst = buf->getChannelData(0);
      for (size_t i = 0; i < resampled.size(); i++) dst[i] = resampled[i];
      samples = move(buf);
    }
  }

  auto total_frames = samples->numberOfFrames();
  if (total_frames <= 0) return result;

  // Trim points are authored in seconds, each "how much to cut from that
  // end" (SampleContent::getInPoint()/getOutPoint()'s own doc comment) -
  // resolved to clamped frame indices only here, at resolve time, against
  // whatever the (possibly just-resampled) buffer's real size actually
  // is right now, rather than trusting stored values that could have
  // gone stale (a hand-edited trim past a since-replaced, shorter
  // recording).
  auto in_frame = static_cast<int>(lround(static_cast<double>(content.getInPoint()) * output_rate));
  auto out_frame = total_frames - static_cast<int>(lround(static_cast<double>(content.getOutPoint()) * output_rate));
  if (in_frame < 0) in_frame = 0;
  if (out_frame > total_frames) out_frame = total_frames;
  if (in_frame >= out_frame) {
    // Degenerate hand-edit (trim amounts that together exceed the
    // buffer's own duration, or either negative after clamping) - fall
    // back to the full buffer rather than producing silence or reading
    // out of bounds.
    in_frame = 0;
    out_frame = total_frames;
  }

  // Pitch-preserving time-stretch when this content's own recorded tempo
  // disagrees with the song's current one - 0 means unknown/not set
  // (SampleContent::getOriginalTempo()'s own "don't guess" convention),
  // so an originalTempo-less clip always plays at its own real duration,
  // regardless of song_tempo. Stretches only the
  // already-trimmed, already-resampled-to-output-rate range above (never
  // audio that will never actually play), and only once per (content,
  // song_tempo) pair - a cache hit here means `samples` becomes the
  // stretched buffer, ready to play in full, and in_frame/out_frame are
  // reset to its own whole extent (the trim was already baked in when it
  // was built, so nothing left to re-trim). Cached on SampleContent
  // itself, not here - see getStretchedBuffer()'s own comment for why.
  if (content.getOriginalTempo() > 0 && content.getOriginalTempo() != song_tempo) {
    auto cached = content.getStretchedBuffer(song_tempo);
    if (cached) {
      samples = cached;
    } else {
      auto trimmed_frames = out_frame - in_frame;
      vector<float> trimmed(static_cast<size_t>(trimmed_frames));
      auto src = samples->getChannelData(0);
      for (int i = 0; i < trimmed_frames; i++) trimmed[static_cast<size_t>(i)] = src[in_frame + i];

      auto ratio = static_cast<double>(song_tempo) / static_cast<double>(content.getOriginalTempo());
      auto stretched = stretchMono(trimmed, output_rate, ratio);

      auto buf = make_shared<AudioBuffer>(1, static_cast<int>(stretched.size()));
      auto dst = buf->getChannelData(0);
      for (size_t i = 0; i < stretched.size(); i++) dst[i] = stretched[i];

      content.setStretchedBuffer(buf, song_tempo);
      samples = move(buf);
    }
    in_frame = 0;
    out_frame = samples->numberOfFrames();
  }

  result.samples = move(samples);
  result.in_frame = in_frame;
  result.out_frame = out_frame;
  return result;
}

RealtimeSampleAudio
resolveRealtimeSampleAudio(const SampleContent & content, int output_rate, int song_tempo) {
  RealtimeSampleAudio result;
  if (!content.getBuffer()) return result;

  // Tempo-stretching still has to happen synchronously, whole-buffer -
  // see this function's own doc comment for why that's a separate,
  // already-documented problem. Everything else about the result
  // (in/out-trimmed range, native-vs-output-rate math) was already
  // solved there, so just reuse it rather than duplicating it here.
  if (content.getOriginalTempo() > 0 && content.getOriginalTempo() != song_tempo) {
    auto resolved = resolveSampleAudio(content, output_rate, song_tempo);
    if (!resolved.samples) return result;
    result.samples = resolved.samples;
    result.start_frame = resolved.in_frame;
    result.end_frame = resolved.out_frame;
    result.playback_ratio = 1.0;
    return result;
  }

  auto samples = content.getBuffer();
  auto total_frames = samples->numberOfFrames();
  if (total_frames <= 0) return result;

  auto native_rate = content.getNativeSampleRate();
  // Trim points are authored in seconds - converted to frame indices
  // against whichever rate `samples` is actually still at: its own
  // native rate when known, or output_rate when not (the same "treat an
  // unknown native rate as already matching" convention
  // resolveSampleAudio() above uses).
  auto trim_rate = native_rate > 0 ? native_rate : output_rate;
  auto in_frame = static_cast<int>(lround(static_cast<double>(content.getInPoint()) * trim_rate));
  auto out_frame = total_frames - static_cast<int>(lround(static_cast<double>(content.getOutPoint()) * trim_rate));
  if (in_frame < 0) in_frame = 0;
  if (out_frame > total_frames) out_frame = total_frames;
  if (in_frame >= out_frame) {
    in_frame = 0;
    out_frame = total_frames;
  }

  result.samples = move(samples);
  result.start_frame = in_frame;
  result.end_frame = out_frame;
  result.playback_ratio = native_rate > 0 ? static_cast<double>(native_rate) / static_cast<double>(output_rate) : 1.0;
  return result;
}

void
SampleTrackState::triggerVoice(const SampleContent & content, int song_tempo, int start_offset_frames, int voice_id) {
  auto output_rate = getChannelConfiguration().getAudioOutSampleRate();
  auto resolved = resolveRealtimeSampleAudio(content, output_rate, song_tempo);
  if (!resolved.samples) return;

  // start_offset_frames arrives in output-frame units (SongState.h's own
  // row-to-frame math) - scaled by playback_ratio to land in whatever
  // domain start_frame is actually in (native, unless a stretch already
  // resolved everything to output_rate - see triggerClip()'s own doc
  // comment for when a caller actually passes a nonzero value). Clamped
  // against the resolved range rather than trusted outright - a clip's
  // own row length never exactly matches its real audio duration
  // (rounded up when it was first derived), so an offset derived from it
  // can legitimately land past this buffer's own real end - nothing
  // should keep ringing from before either way, so stopVoices(voice_id)
  // below still applies, just no new voice starts.
  auto start_position = resolved.start_frame + static_cast<double>(start_offset_frames) * resolved.playback_ratio;
  if (start_position >= static_cast<double>(resolved.end_frame)) {
    stopVoices(voice_id);
    return;
  }

  // Explicitly stops whatever this same voice was already playing first:
  // a live Session-view swap between two different clips on this track
  // has no other mechanism to end the old one the way a transport-driven
  // transition already does via SongState.h's own stopAllVoices() call,
  // and a fresh background-bed trigger (a new section entered, a resume)
  // needs the same treatment. Never touches the *other* voice - that's
  // the whole point of keeping the two roles as separate voices
  // (SampleTrackState's own doc comment). No noteOn()/Instrument
  // indirection needed to get here - unlike a pooled, pitched instrument,
  // a sample clip's voice needs nothing a fake Track subclass would
  // resolve for it (no exclusive-class choking applies to raw playback; a
  // SampleTrack has no children, so its own default extent is
  // unconditionally 0, the same base case Track::getDefaultExtent()
  // would already resolve to).
  stopVoices(voice_id);

  auto resolved_position = getPosition();
  if (resolved_position.extent < 0.0f) resolved_position.extent = 0.0f;

  auto voice = make_unique<SampleClipVoice>(getChannelConfiguration(), resolved_position, resolved.samples, start_position, resolved.end_frame, resolved.playback_ratio, getSends());
  addVoice(voice_id, move(voice));
}

void
SampleTrackState::triggerClip(const Clip & clip, int song_tempo, int start_offset_frames) {
  auto * content = clip.getSampleContent();
  if (!content) return;
  triggerVoice(*content, song_tempo, start_offset_frames, kClipVoiceId);
}

unique_ptr<TrackState>
SampleTrack::createState(const ChannelConfiguration & config, const SongStructure &) const {
  return make_unique<SampleTrackState>(config, isSolo(), isMuted(), getInternalId(), getPosition(), getSends());
}
