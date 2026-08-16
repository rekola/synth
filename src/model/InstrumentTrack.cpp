#include "InstrumentTrack.h"

#include "../state/SongState.h"
#include "../state/InstrumentTrackState.h"

#include <cmath>

using namespace std;

namespace {

// sendA/sendB/sendMain are persisted and edited in dB (a perceptual/log
// quantity is far easier to dial a subtle send with than a linear
// fraction), but SendLevels.h's fields are a plain linear multiplier read
// directly per sample in the audio callback - conversion happens only
// here, at the XML boundary. Self-contained rather than
// TreeNode::decibelsToGain()/gainToDecibels() (only reachable from
// TreeNode<Derived> subclasses - VoiceState/TrackState - and
// InstrumentTrack is a model class, not one of those) - the same "each
// file keeps its own small dB helper" convention effects/Compressor.cpp's
// db2lin()/lin2db() and dsp/TapeTransport.cpp's/effects/TapeDegradation.cpp's
// own dbToLinear() already use, including the same -100dB "off" floor
// TreeNode's version establishes.
float dbToLinear(float db) { return db > -100.0f ? powf(10.0f, db * 0.05f) : 0.0f; }
float linearToDb(float linear) { return linear <= 0.00001f ? -100.0f : 20.0f * log10f(linear); }

}
  
std::unique_ptr<TrackState>
InstrumentTrack::createState(const ChannelConfiguration & config) const {
  assert(getInstrumentId() >= 0);
  return std::make_unique<InstrumentTrackState>(config, isSolo(), isMuted(), getInternalId(), getInstrumentId(), getPosition(), sends_);
}

void
InstrumentTrack::loadParameters(const ParameterSource & input) {
  Track::loadParameters(input);

  setInstrumentId(input.getInt("instrument"));
  setSolo(input.getBool("solo"));
  setMuted(input.getBool("mute"));
  setAzimuth(input.getFloat("azimuth"));
  setDistance(input.getFloat("distance"));
  setElevation(input.getFloat("elevation"));
  setExtent(input.getFloat("extent", -1.0f));
  setColor(input.getText("color"));
  sends_.a = dbToLinear(input.getFloat("sendA", -100.0f));
  sends_.b = dbToLinear(input.getFloat("sendB", -100.0f));
  sends_.main = dbToLinear(input.getFloat("sendMain", 0.0f));
  setMinNoteColumns(input.getInt("noteColumns", 1));
}

void
InstrumentTrack::storeParameters(ParameterSource & output) const {
  Track::storeParameters(output);

  output.set("instrument", getInstrumentId());
  if (isSolo()) output.set("solo", true);
  if (isMuted()) output.set("mute", true);
  output.set("azimuth", getAzimuth());
  output.set("distance", getDistance());
  output.set("elevation", getElevation());
  output.set("extent", getExtent(), -1.0f);
  output.set("color", getColor());
  if (sends_.a > 0.0f) output.set("sendA", linearToDb(sends_.a));
  if (sends_.b > 0.0f) output.set("sendB", linearToDb(sends_.b));
  output.set("sendMain", linearToDb(sends_.main), 0.0f);
  if (min_note_columns_ != 1) output.set("noteColumns", min_note_columns_);
}
