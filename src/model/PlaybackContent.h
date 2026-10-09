#ifndef _PLAYBACKCONTENT_H_
#define _PLAYBACKCONTENT_H_

#include "Arrangement.h"
#include "BarGrid.h"
#include "Clip.h"
#include "Swing.h"
#include "TimeSignature.h"
#include "../instruments/Tuning.h"
#include "../util/constants.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

// The part of the song the audio thread reads while rendering - the
// arrangement's patterns/instances/sample beds and every track's clips -
// as an immutable copy. The UI thread builds one when an edit finishes
// (Song::Edit) and publishes it; the audio thread only ever reads the
// latest published one, so it never sees a half-made edit and never shares
// a container with a writer. Sample audio is shared with the live model by
// pointer, not copied.
// The song-level values playback reads (tempo, bars, tuning and the room).
struct SongScalars {
  int tempo = 140;
  int swing = swing::kStraight;
  TimeSignature time_signature{4, 4};
  RunningBars running_bars;
  Tuning tuning = Tuning::EDO31;
  float ear_height = constants::DEFAULT_EAR_HEIGHT;
  bool floor_reflection_enabled = constants::DEFAULT_FLOOR_REFLECTION_ENABLED;
  float floor_reflection_strength = constants::DEFAULT_FLOOR_REFLECTION_STRENGTH;
  float ground_absorption = constants::DEFAULT_GROUND_ABSORPTION;

  bool operator==(const SongScalars & o) const {
    return tempo == o.tempo && swing == o.swing && time_signature == o.time_signature && running_bars.signature == o.running_bars.signature &&
      running_bars.origin == o.running_bars.origin && tuning == o.tuning && ear_height == o.ear_height && floor_reflection_enabled == o.floor_reflection_enabled &&
      floor_reflection_strength == o.floor_reflection_strength && ground_absorption == o.ground_absorption;
  }
};

struct CompiledTracks;

struct PlaybackContent {
  SongScalars scalars;
  // The tracks, instrument pool and bus as of this content. Shared with the
  // UI thread's copy; the audio thread only reads through it, and the last
  // reference is dropped on the UI thread (the publisher frees displaced
  // content there).
  std::shared_ptr<const CompiledTracks> tracks;
  Arrangement arrangement;
  std::unordered_map<int, std::vector<Clip> > clips_by_track;
  uint64_t generation = 0;

  const std::vector<Clip> & getClips(int track_id) const {
    static const std::vector<Clip> kNone;
    auto it = clips_by_track.find(track_id);
    return it == clips_by_track.end() ? kNone : it->second;
  }
};

// An order-independent hash of everything in a PlaybackContent. Comparing
// the digest of the live model with the published copy's finds an edit that
// never closed a Song::Edit.
uint64_t contentDigest(const SongScalars & scalars, const Arrangement & arrangement, const std::unordered_map<int, std::vector<Clip> > & clips_by_track);

// Hands the audio thread the latest published content without locks or
// reference counts, and frees displaced content on the UI thread only.
//
// The reader announces the newest generation it may touch before loading the
// pointer, and the publisher frees nothing at or after the announced one, so
// a reader can never be holding something that is freed under it. One reader
// thread per publisher.
class ContentPublisher {
 public:
  ContentPublisher() { publish(std::make_unique<PlaybackContent>()); }
  ContentPublisher(const ContentPublisher &) = delete;
  ContentPublisher & operator=(const ContentPublisher &) = delete;

  // UI thread.
  void publish(std::unique_ptr<PlaybackContent> content);

  // Audio thread: valid until the Reader goes out of scope.
  class Reader {
   public:
    explicit Reader(const ContentPublisher & publisher) : publisher_(publisher) {
      publisher_.reader_generation_.store(publisher_.published_generation_.load());
      content_ = publisher_.current_.load();
    }
    ~Reader() { publisher_.reader_generation_.store(kIdle); }
    Reader(const Reader &) = delete;
    Reader & operator=(const Reader &) = delete;
    const PlaybackContent & operator*() const { return *content_; }
    const PlaybackContent * operator->() const { return content_; }

   private:
    const ContentPublisher & publisher_;
    const PlaybackContent * content_;
  };

  // How many published copies are still held (the current one and any a
  // reader might be on), for tests.
  size_t heldCount() const { return owned_.size(); }

 private:
  static constexpr uint64_t kIdle = ~uint64_t(0);

  std::atomic<const PlaybackContent *> current_{nullptr};
  std::atomic<uint64_t> published_generation_{0};
  mutable std::atomic<uint64_t> reader_generation_{kIdle};
  std::vector<std::unique_ptr<PlaybackContent> > owned_; // UI thread only
};

#endif
