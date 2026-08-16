#ifndef _OFFLINERENDERER_H_
#define _OFFLINERENDERER_H_

#include "../ambisonic/ChannelConfiguration.h"
#include "../ambisonic/MixerType.h"

#include <cstddef>
#include <vector>

class Song;

struct OfflineRenderResult {
  int channels = 0;
  int sampleRate = 0;
  std::vector<float> interleaved;

  size_t numberOfFrames() const {
    return channels ? interleaved.size() / static_cast<size_t>(channels) : 0;
  }
};

// Renders `song` from the start through all pattern rows, then continues
// rendering the tail (voice releases, reverb/delay decay) until the output
// falls silent or max_tail_seconds elapses. Used by both --render and the
// test suite so they exercise identical playback logic. `mixer_type` is
// only consulted when `channel_config` is AMBISONIC (see MixerType.h) - a
// MONO config never attempts binaural decoding regardless of this
// parameter (see MixerFactory.cpp). `use_legacy_binaural` is passed
// straight through to createMixer() - see its own doc comment.
OfflineRenderResult renderSongOffline(const Song & song, const ChannelConfiguration & channel_config,
				      MixerType mixer_type = MixerType::AMBISONIC_STEREO,
				      int block_frames = 1024, int max_tail_seconds = 10,
				      float silence_threshold = 1e-5f,
				      bool use_legacy_binaural = false);

#endif
