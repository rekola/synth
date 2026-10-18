#include "TestFramework.h"

#include "../src/Controller.h"
#include "../src/audio/NullAudio.h"
#include "../src/playback/PlaybackControlEvent.h"
#include "../src/playback/Player.h"
#include "../src/util/Notifier.h"
#include "../src/util/StderrLogger.h"
#include "../src/audio/AudioBuffer.h"

#include <atomic>
#include <chrono>
#include <thread>

using namespace std;
using namespace std::chrono;

TEST(notifier_stays_raised_until_cleared_and_wait_times_out_when_not) {
  Notifier n;
  CHECK(!n.wait(0));
  n.signal();
  CHECK(n.wait(0));
  CHECK(n.wait(0)); // level-triggered: waiting does not clear
  n.clear();
  CHECK(!n.wait(0));
}

TEST(notifier_wakes_a_waiter_on_another_thread) {
  Notifier n;
  atomic<bool> woke{ false };
  thread waiter([&] { woke = n.wait(5000); });
  this_thread::sleep_for(milliseconds(20));
  n.signal();
  waiter.join();
  CHECK(woke);
}

TEST(null_audio_wait_paces_blocks_by_the_clock_and_reports_wakeups) {
  NullAudio audio(48000, 2, 480); // 10 ms blocks
  Notifier wake;
  AudioBuffer block;
  StderrLogger logger;

  auto start = steady_clock::now();
  int blocks = 0;
  while (blocks < 5) {
    auto ready = audio.wait(wake, false, 1000);
    CHECK(!ready.woken);
    if (ready.playback) {
      audio.play(block, logger);
      blocks++;
    }
  }
  auto elapsed = duration_cast<milliseconds>(steady_clock::now() - start).count();
  CHECK(elapsed >= 35); // the first block is due at once, then 4 x 10 ms
  CHECK(elapsed < 1000);

  wake.signal();
  CHECK(audio.wait(wake, false, 1000).woken);
}

// The real Player loop over a device-less backend: it renders blocks until
// told to terminate, which is what lets it run on machines with no sound
// hardware.
TEST(player_loop_runs_over_null_audio_and_terminates_on_request) {
  ChannelConfiguration config(48000, 1);
  Controller controller(config);
  NullAudio audio(48000, 2, 480);

  Player player(controller.getChannelConfiguration(), &controller);
  thread audio_thread([&] { player.play(audio); });

  auto deadline = steady_clock::now() + seconds(10);
  while (audio.blocksPlayed() < 5 && steady_clock::now() < deadline) this_thread::sleep_for(milliseconds(5));
  CHECK(audio.blocksPlayed() >= 5);

  controller.getPlaybackEventQueue().push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::TERMINATE));
  audio_thread.join();
}
