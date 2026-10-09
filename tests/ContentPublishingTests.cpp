#include "TestFramework.h"

#include "../src/Controller.h"
#include "../src/model/ArrangementOps.h"
#include "../src/model/InstrumentTrack.h"
#include "../src/model/PlaybackContent.h"
#include "../src/model/SampleTrack.h"
#include "../src/model/Song.h"

#include <atomic>
#include <thread>

namespace {

std::unique_ptr<PlaybackContent> contentWithNotes(int notes) {
  auto content = std::make_unique<PlaybackContent>();
  for (int i = 0; i < notes; i++) content->arrangement.setNote(i, 1, 0, Note(60, 100));
  return content;
}

int noteCount(const PlaybackContent & content) {
  auto it = content.arrangement.getPatternsByTrack().find(1);
  return it == content.arrangement.getPatternsByTrack().end() ? 0 : static_cast<int>(it->second.getNotesByRow().size());
}

}  // namespace

TEST(content_publisher_reader_sees_the_latest_and_keeps_what_it_holds) {
  ContentPublisher publisher;
  publisher.publish(contentWithNotes(1));
  {
    ContentPublisher::Reader reader(publisher);
    CHECK(noteCount(*reader) == 1);
    auto held = reader->generation;
    publisher.publish(contentWithNotes(2));
    publisher.publish(contentWithNotes(3));
    // The reader's copy was not freed under it, and is still what it read.
    CHECK(reader->generation == held);
    CHECK(noteCount(*reader) == 1);
    CHECK(publisher.heldCount() >= 3);
  }
  publisher.publish(contentWithNotes(4));
  CHECK(publisher.heldCount() == 1); // nothing is held back once no reader is
  ContentPublisher::Reader reader(publisher);
  CHECK(noteCount(*reader) == 4);
}

TEST(content_publisher_survives_a_reader_thread_running_against_a_publishing_one) {
  ContentPublisher publisher;
  std::atomic<bool> stop{false};
  std::atomic<int> bad{0};
  std::thread reader_thread([&]() {
    while (!stop.load()) {
      ContentPublisher::Reader reader(publisher);
      // Each published copy holds as many notes as its generation, so a copy
      // freed or half-built under the reader would not add up.
      auto notes = noteCount(*reader);
      if (notes != static_cast<int>(reader->generation) - 1 && reader->generation > 1) bad++;
      if (notes != noteCount(*reader)) bad++;
    }
  });
  for (int i = 2; i < 400; i++) publisher.publish(contentWithNotes(i - 1));
  stop = true;
  reader_thread.join();
  CHECK(bad.load() == 0);
}

TEST(published_song_shows_the_audio_thread_only_finished_edits) {
  Song song;
  song.setContentPublished(true);
  auto track_id = song.addTrack(std::make_unique<InstrumentTrack>(0)).getInternalId();

  {
    Song::Edit edit(song, "write");
    song.getArrangement().setNote(4, track_id, 0, Note(60, 100));
    // Mid-edit: the audio thread still sees the song as it was.
    CHECK(song.readContent()->arrangement.getPatternsByTrack().count(track_id) == 0);
    CHECK(!song.publishedContentIsCurrent());
  }
  CHECK(song.readContent()->arrangement.getPatternsByTrack().count(track_id) == 1);
  CHECK(song.publishedContentIsCurrent());
}

TEST(published_song_exposes_a_write_that_skipped_the_edit) {
  Song song;
  song.setContentPublished(true);
  song.getArrangement().setNote(0, 7, 0, Note(60, 100)); // no Song::Edit
  CHECK(!song.publishedContentIsCurrent());
  { Song::Edit edit(song, "later edit"); }
  CHECK(song.publishedContentIsCurrent());
}

TEST(unpublished_song_reads_the_model_as_it_is) {
  Song song;
  song.getArrangement().setNote(0, 7, 0, Note(60, 100));
  CHECK(song.readContent()->arrangement.getPatternsByTrack().count(7) == 1);
  song.getArrangement().setNote(1, 7, 0, Note(62, 100));
  CHECK(song.readContent()->arrangement.getPatternsByTrack().at(7).getNotesByRow().size() == 2);
}

TEST(controller_paths_that_write_clips_and_notes_keep_the_published_content_current) {
  ChannelConfiguration config(8000, 1);
  Controller controller(config);
  controller.switchToBuffer(controller.freshBufferName());
  auto & song = controller.getSong();
  auto track_id = song.getPlayableTrackIds().front();
  CHECK(song.isContentPublished());

  controller.setTempo(150);
  CHECK(song.publishedContentIsCurrent());

  controller.clearNoteCell(0, track_id, 0);
  std::set<std::pair<int, int> > unused;
  controller.writeReleaseOff(unused, false, 3, track_id, 0, 0);
  CHECK(song.publishedContentIsCurrent());

  std::set<std::pair<int, int> > cleared;
  controller.ensureRowCleared(cleared, 5, track_id);
  CHECK(song.publishedContentIsCurrent());

  std::unordered_map<int, std::string> clip_ids;
  controller.ensureNoteRecordingClip(clip_ids, track_id, 8);
  CHECK(song.publishedContentIsCurrent());
  CHECK(!song.getClips(track_id).empty());
  CHECK(song.readContent()->getClips(track_id).size() == song.getClips(track_id).size());

  controller.deleteClipSlot(track_id, 0);
  CHECK(song.publishedContentIsCurrent());
}

TEST(sample_capture_keeps_the_published_content_current) {
  ChannelConfiguration config(8000, 1);
  Controller controller(config);
  controller.switchToBuffer(controller.freshBufferName());
  auto & song = controller.getSong();
  auto track_id = song.addTrack(std::make_unique<SampleTrack>()).getInternalId();
  controller.setRecordingTrackId(track_id);
  controller.startRecording();

  AudioBuffer block(1, 400);
  for (int i = 0; i < 400; i++) block.getChannelData(0)[i] = 0.3f;
  controller.addToSample(block);
  controller.beginSampleCapture(track_id);
  CHECK(song.publishedContentIsCurrent());
  controller.addToSample(block);
  controller.finishSampleCapture();
  CHECK(song.publishedContentIsCurrent());
  CHECK(song.readContent()->getClips(track_id).size() == 1);
}
