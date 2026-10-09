#include "PlaybackContent.h"

#include <functional>

namespace {

uint64_t mix(uint64_t h) {
  h ^= h >> 33;
  h *= 0xff51afd7ed558ccdULL;
  h ^= h >> 33;
  h *= 0xc4ceb9fe1a85ec53ULL;
  h ^= h >> 33;
  return h;
}

uint64_t combine(uint64_t seed, uint64_t value) { return mix(seed ^ (value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2))); }

uint64_t hashString(const std::string & s) { return std::hash<std::string>()(s); }

uint64_t digestSample(const SampleContent & content) {
  uint64_t h = content.identity();
  h = combine(h, static_cast<uint64_t>(content.getBuffer() ? content.getBuffer()->numberOfFrames() : -1));
  h = combine(h, std::hash<float>()(content.getInPoint()));
  h = combine(h, std::hash<float>()(content.getOutPoint()));
  h = combine(h, static_cast<uint64_t>(content.getOriginalTempo()));
  return combine(h, static_cast<uint64_t>(content.getNativeSampleRate()));
}

uint64_t digestPattern(const Pattern & pattern) {
  uint64_t h = static_cast<uint64_t>(pattern.getLength());
  uint64_t rows = 0;
  for (auto & [ row, notes ] : pattern.getNotesByRow()) {
    uint64_t r = combine(1, row);
    for (auto & n : notes) {
      r = combine(r, static_cast<uint64_t>(n.isDefined()));
      r = combine(r, static_cast<uint64_t>(n.getValue() + 1000));
      r = combine(r, static_cast<uint64_t>(n.getVelocity()));
      r = combine(r, static_cast<uint64_t>(n.getDelay()));
    }
    rows += r;
  }
  for (auto & [ row, commands ] : pattern.getCommandsByRow()) {
    uint64_t r = combine(2, row);
    for (auto & c : commands) r = combine(r, hashString(to_string(c)));
    rows += r;
  }
  return combine(h, rows);
}

uint64_t digestClip(const Clip & clip) {
  uint64_t h = hashString(clip.getId());
  h = combine(h, static_cast<uint64_t>(clip.getLeafTrackId()));
  h = combine(h, static_cast<uint64_t>(clip.getLength()));
  h = combine(h, static_cast<uint64_t>(clip.isLooping()));
  h = combine(h, static_cast<uint64_t>(clip.hasStopButton()));
  h = combine(h, digestPattern(clip.getLeafPattern()));
  for (auto & layer : clip.getSampleLayers()) h = combine(h, digestSample(layer));
  return h;
}

}  // namespace

uint64_t
contentDigest(const Arrangement & arrangement, const std::unordered_map<int, std::vector<Clip> > & clips_by_track) {
  uint64_t total = 0;
  for (auto & [ track_id, pattern ] : arrangement.getPatternsByTrack()) total += combine(combine(10, static_cast<uint64_t>(track_id)), digestPattern(pattern));
  for (auto & [ track_id, instances ] : arrangement.getInstancesByTrack()) {
    uint64_t t = combine(11, static_cast<uint64_t>(track_id));
    for (auto & [ row, clip_id ] : instances) t = combine(combine(t, row), hashString(clip_id));
    total += t;
  }
  for (auto & [ track_id, content ] : arrangement.getSampleBackgroundsByTrack()) total += combine(combine(12, static_cast<uint64_t>(track_id)), digestSample(content));
  for (auto & [ track_id, clips ] : clips_by_track) {
    uint64_t t = combine(13, static_cast<uint64_t>(track_id));
    for (auto & clip : clips) t = combine(t, digestClip(clip));
    total += t;
  }
  return total;
}

void
ContentPublisher::publish(std::unique_ptr<PlaybackContent> content) {
  auto generation = published_generation_.load() + 1;
  content->generation = generation;
  auto * raw = content.get();
  owned_.push_back(std::move(content));
  current_.store(raw);
  published_generation_.store(generation);

  // Anything older than what the reader announced (or, with the reader
  // idle, anything but the newest) can no longer be reached.
  auto reader = reader_generation_.load();
  auto keep_from = reader < generation ? reader : generation;
  for (auto it = owned_.begin(); it != owned_.end();) {
    if ((*it)->generation < keep_from) it = owned_.erase(it);
    else ++it;
  }
}
