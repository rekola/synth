#include "Clip.h"
#include "SampleTrack.h"
#include "../audio/AudioBuffer.h"

using namespace std;

void
Clip::rebuildMixedContent(int output_rate, int song_tempo) {
  // getMixedContent() never even looks at the cache with at most one
  // layer - nothing to build, and mixed_content_valid_ stays false so a
  // later layer add/remove can't accidentally find a stale "valid" true
  // left over from some earlier multi-layer state.
  if (sample_layers_.size() <= 1) {
    mixed_content_valid_ = false;
    return;
  }

  // A fresh accumulator every rebuild, not appended to whatever was
  // already cached - this has to be a real, definitive re-sum of every
  // current layer (e.g. after a layer's own trim changed, or one was
  // ever removed in the future), never a running total.
  mixed_content_cache_ = SampleContent();
  mixed_content_cache_.setOriginalTempo(static_cast<short>(song_tempo));

  for (auto & layer : sample_layers_) {
    if (!layer.getBuffer()) continue;
    // resolveSampleAudio() (SampleTrack.h) - a full, synchronous
    // materialization (trim/resample/tempo-stretch all resolved into one
    // real buffer) - only safe to call here because this whole method
    // only ever runs off the audio thread (getMixedContent()'s own
    // comment). Each layer resolved independently, since two takes can
    // easily have been recorded at different native rates.
    auto resolved = resolveSampleAudio(layer, output_rate, song_tempo);
    if (!resolved.samples || resolved.in_frame >= resolved.out_frame) continue; // nothing playable in this layer
    auto src = resolved.samples->getChannelData(0) + resolved.in_frame;
    auto frame_count = static_cast<int64_t>(resolved.out_frame - resolved.in_frame);
    // dest_offset always 0 - every layer starts at the clip's own row 0,
    // same as a fresh trigger always does; mixIntoSampleContent() itself
    // grows the accumulator to fit whichever layer turns out longest.
    mixIntoSampleContent(mixed_content_cache_, output_rate, frame_count, src, frame_count, 0, 1.0f);
  }

  mixed_content_valid_ = true;
}
