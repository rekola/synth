#include "OfflineRenderer.h"

#include "Song.h"
#include "SongState.h"
#include "MixerFactory.h"

#include <cmath>

OfflineRenderResult
renderSongOffline(const Song & song, const ChannelConfiguration & channel_config, MixerType mixer_type,
		  int block_frames, int max_tail_seconds, float silence_threshold, bool use_legacy_binaural) {
  OfflineRenderResult result;
  result.channels = channel_config.getDeviceChannels();
  result.sampleRate = channel_config.getAudioOutSampleRate();

  int total_rows = 0;
  for (auto & pattern : song.getPatterns()) total_rows += pattern.getNumRows();
  if (!total_rows) return result;

  SongState state(channel_config);
  state.initialize(song);
  state.setIsPlaying(true);

  auto mixer = createMixer(channel_config, mixer_type, use_legacy_binaural);

  int tail_frames = 0;

  while (true) {
    if (state.isPlaying() && state.getAbsolutePosition() >= total_rows) {
      state.setIsPlaying(false); // song ended; keep rendering the tail
    }

    state.render(block_frames, song, *mixer);
    auto master = mixer->encode();

    auto base = result.interleaved.size();
    result.interleaved.resize(base + static_cast<size_t>(block_frames) * result.channels);

    float peak = 0.0f;
    for (int c = 0; c < result.channels; c++) {
      auto channel_data = master.getChannelData(c);
      for (int i = 0; i < block_frames; i++) {
	auto v = channel_data[i];
	result.interleaved[base + static_cast<size_t>(i) * result.channels + c] = v;
	auto a = fabsf(v);
	if (a > peak) peak = a;
      }
    }

    if (!state.isPlaying()) {
      tail_frames += block_frames;
      if (peak < silence_threshold || tail_frames >= max_tail_seconds * result.sampleRate) break;
    }
  }

  return result;
}
