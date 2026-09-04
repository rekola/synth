#include "TestFramework.h"

#include "../src/dsp/RecordingRingBuffer.h"
#include "../src/audio/AudioBuffer.h"

namespace {

AudioBuffer makeBlock(int frames, float start_value) {
  AudioBuffer block(1, frames);
  auto data = block.getChannelData(0);
  for (int i = 0; i < frames; i++) data[i] = start_value + static_cast<float>(i);
  return block;
}

} // namespace

TEST(recording_ring_buffer_starts_empty) {
  RecordingRingBuffer ring(10);
  CHECK(ring.validFrames() == 0);
  auto drained = ring.drain();
  CHECK(drained.numberOfFrames() == 0);
}

TEST(recording_ring_buffer_below_capacity_drains_exactly_what_was_pushed_in_order) {
  RecordingRingBuffer ring(10);
  ring.push(makeBlock(4, 0.0f));  // 0 1 2 3
  ring.push(makeBlock(3, 10.0f)); // 10 11 12
  CHECK(ring.validFrames() == 7);

  auto drained = ring.drain();
  CHECK(drained.numberOfFrames() == 7);
  auto data = drained.getChannelData(0);
  float expected[7] = { 0, 1, 2, 3, 10, 11, 12 };
  for (int i = 0; i < 7; i++) CHECK_NEAR(data[i], expected[i], 1e-6f);
}

TEST(recording_ring_buffer_wraps_and_keeps_only_the_most_recent_capacity_frames) {
  RecordingRingBuffer ring(5);
  ring.push(makeBlock(8, 0.0f)); // 0..7, only the last 5 (3 4 5 6 7) survive
  CHECK(ring.validFrames() == 5);

  auto drained = ring.drain();
  CHECK(drained.numberOfFrames() == 5);
  auto data = drained.getChannelData(0);
  float expected[5] = { 3, 4, 5, 6, 7 };
  for (int i = 0; i < 5; i++) CHECK_NEAR(data[i], expected[i], 1e-6f);
}

TEST(recording_ring_buffer_drain_clears_it_back_to_empty) {
  RecordingRingBuffer ring(10);
  ring.push(makeBlock(4, 0.0f));
  ring.drain();
  CHECK(ring.validFrames() == 0);

  auto redrained = ring.drain();
  CHECK(redrained.numberOfFrames() == 0);
}

TEST(recording_ring_buffer_reset_discards_content_without_a_drain) {
  RecordingRingBuffer ring(10);
  ring.push(makeBlock(4, 0.0f));
  ring.reset();
  CHECK(ring.validFrames() == 0);

  ring.push(makeBlock(2, 100.0f)); // 100 101 - a fresh arm cycle's own audio
  auto drained = ring.drain();
  CHECK(drained.numberOfFrames() == 2);
  auto data = drained.getChannelData(0);
  CHECK_NEAR(data[0], 100.0f, 1e-6f);
  CHECK_NEAR(data[1], 101.0f, 1e-6f);
}

TEST(recording_ring_buffer_zero_capacity_never_stores_anything) {
  RecordingRingBuffer ring(0);
  ring.push(makeBlock(4, 0.0f));
  CHECK(ring.validFrames() == 0);
  auto drained = ring.drain();
  CHECK(drained.numberOfFrames() == 0);
}
