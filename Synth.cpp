#include "Synth.h"

#include "SampleData.h"
#include "Tuner.h"
#include "TrackEventQueue.h"

#include <cmath>
#include <cassert>

using namespace std;

SampleData
Synth::play(Song & song, size_t frames) {
  auto & mastertrack = song.getMasterTrack();
  auto & tracks = mastertrack.getChildren();

  Tuner tuner;
  TrackEventQueue track_events;
  
#if 0
  size_t tick_frames = getTickInterval(song);
  if (tick_frames > frames) tick_frames = frames;
  auto sinterval = getSampleInterval(song);
#endif

  if (is_playing) {
    for (size_t i = 0; i < frames; i++) {
      if (samplepos == 0) {
	auto & pattern = song.getPattern(getPatternPosition());
	auto tuning = pattern.getTuning() != Tuning::INHERIT ? pattern.getTuning() : song.getTuning();
	int key = pattern.getKey() >= 0 ? pattern.getKey() : song.getKey();

	for (size_t col = 0; col < tracks.size(); col++) {
	  auto & notes = pattern.getNotes(col, getTrackPosition());
	  for (size_t j = 0; j < notes.size(); j++) {
	    if (notes[j].isDefined()) {
	      auto & note = notes[j];
	      float frequency, velocity;
	      if (note.isOff()) {
		frequency = velocity = 0.0f;
	      } else {
		frequency = tuner.getFrequency(tuning, key, note);
		velocity = note.getVelocityAsFloat();
	      }
	      float delay = 0; // song.getRandomizationFactor() * samplerate * rand() / RAND_MAX;
	      track_events.addPendingEvent(col, i, int(j), delay, frequency, velocity);
	    }
	  }
	}
      }

      moveForwardSample(song);
    }
  }

  return mastertrack.render(frames, song, state, track_events);
}
