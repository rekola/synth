#ifndef _LEAFTRACKSTATE_H_
#define _LEAFTRACKSTATE_H_

#include "TrackState.h"
#include "VoiceState.h"
#include "../audio/AudioBuffer.h"
#include "../ambisonic/SphericalPosition.h"
#include "../model/SendLevels.h"
#include "../dsp/ValueRamp.h"

#include <algorithm>
#include <cmath>

namespace {
  // Self-contained (not TreeNode::decibelsToGain()/gainToDecibels(), only
  // reachable from TreeNode<Derived> subclasses - VoiceState/TrackState,
  // and LeafTrackState's own send ramp needs this before it's fully one)
  // - the same "each file keeps its own small dB helper" convention
  // model/LeafTrack.cpp's own dbToLinear()/linearToDb() already use,
  // including the same -100dB "off" floor. Needed here (not just at the
  // call sites feeding glideSendMain()/etc.) because the ramp itself now
  // interpolates in dB - the same space the Launchpad row layout (and
  // every other display of these values) actually uses - converting to
  // linear only once per render chunk, right before it reaches sends_.
  float dbToLinearForRamp(float db) { return db > -100.0f ? powf(10.0f, db * 0.05f) : 0.0f; }
  float linearToDbForRamp(float linear) { return linear <= 0.00001f ? -100.0f : 20.0f * log10f(linear); }
}

// The runtime counterpart of LeafTrack (src/model/LeafTrack.h) - mute/solo/
// position/sends/voices_ bookkeeping every leaf track type shares
// (InstrumentTrackState, SampleTrackState), independent of what actually
// produces those voices. Mirrors the model-layer LeafTrack/InstrumentTrack/
// SampleTrack split: a plain TrackState has no notion of "leaf" at all,
// and InstrumentTrackState builds on this with the pool-instrument
// resolution and note-column/chord/pressure bookkeeping only a
// pattern-driven, pitched track needs - SampleTrackState has no use for any
// of that, just this class's plain single-voice-per-trigger machinery.
class LeafTrackState : public TrackState {
public:
  explicit LeafTrackState(const ChannelConfiguration & channel_config, bool solo, bool muted, int track_id, const SphericalPosition & position, const SendLevels & sends)
    : TrackState(channel_config), solo_(solo), muted_(muted), track_id_(track_id), position_(position), sends_(sends) {
    // Every ramp starts already at rest on its own seeded value (stateFor()'s
    // one-time construction-time seed from the model - see this class's own
    // doc comment) - nothing to glide toward yet until a real
    // glideSendMain()/A()/B() call arrives. In dB, not linear - see these
    // ramps' own declaration comment for why.
    send_main_ramp_.snapTo(linearToDbForRamp(sends.main));
    send_a_ramp_.snapTo(linearToDbForRamp(sends.a));
    send_b_ramp_.snapTo(linearToDbForRamp(sends.b));
    azimuth_ramp_.snapTo(position.azimuth);
  }

  void addVoice(int column, std::unique_ptr<VoiceState> voice) {
    voices_[column].push_back(std::move(voice));
  }

  // Combines this track's own currently-sounding voices_ into one buffer -
  // not an override of anything on TrackState (which has no voice-shaped
  // render() any more - voices_ is VoiceState-typed, not TrackState-typed;
  // see plans/trackstate-voicestate-split.md); named distinctly from
  // TrackState::render's 3-arg overload (rather than overloading render()
  // itself) so the two can't be mistaken for one hiding the other. Both
  // InstrumentTrackState::render() and SampleTrackState::render() call this
  // by unqualified name once per pending-event sub-chunk, and
  // ArpeggiatorState overrides it (see its own doc comment) to interleave
  // its stepper's timing with that same chunking, purely via ordinary
  // virtual dispatch.
  virtual AudioBuffer renderVoices(int frames) {
    // Advance this chunk's own share of any in-flight Send Main/A/B/
    // azimuth glide first - a voice rendered below with renderVoices()
    // already reflects wherever the glide has reached by this chunk, same
    // as a live press already did before it moved server-side.
    advanceSendRamps(frames);
    advanceAzimuthRamp(frames);

    // Render every active voice first (still calling render() even when
    // muted, so envelopes/LFOs keep advancing - only mixing is skipped),
    // then decide this track's own accumulator shape from what actually
    // came back rather than a separate non-rendering prediction.
    std::vector<AudioBuffer> rendered;
    bool is_active = false;

    for (auto & [ column, voices ] : voices_) {
      for (auto & voice : voices) {
	if (voice->isActive()) {
	  auto s = voice->render(frames);
	  is_active = true;
	  if (!isMuted()) rendered.push_back(std::move(s));
	}
      }
    }

    bool has_main = false, has_aux_a = false, has_aux_b = false;
    for (auto & s : rendered) {
      has_main = has_main || s.hasChannel(Channel::Main);
      has_aux_a = has_aux_a || s.hasChannel(Channel::AuxA);
      has_aux_b = has_aux_b || s.hasChannel(Channel::AuxB);
    }
    AudioBuffer data(has_main ? getChannelConfiguration().numberOfChannels() : 0, has_aux_a, has_aux_b, frames, isSolo());
    data.zero();

    // Every voice now spatially encodes itself directly, using its own
    // position, to its own real (never reduced) ChannelConfiguration - see
    // InstrumentVoice::encodePosition() - so a voice's rendered output
    // always already matches this accumulator's shape exactly; no
    // per-voice dispatch is needed, just a plain mix.
    for (auto & s : rendered) data.mixNamed(s);

    // meter_value_ (-1.0f, "no data") is left at TrackInfo's own default
    // here - InstrumentTrackState::render()/SampleTrackState::render() both
    // overwrite this call's own TrackInfo with their own, RMS included,
    // once their outer chunked loop finishes (see their own render()).
    // sends_/position_.azimuth are already this chunk's own post-advance
    // value (the advanceSendRamps()/advanceAzimuthRamp() calls above), so
    // it's current even if some future subclass never gets around to
    // overwriting this with its own call.
    setTrackInfo(TrackInfo( is_active, data.isClipping(), -1.0f, sends_.main, sends_.a, sends_.b, position_.azimuth, true ));

    return data;
  }

  void stopVoices(int column) {
    auto it = voices_.find(column);
    if (it != voices_.end()) {
      for (auto & voice : it->second) if (voice->isActive()) voice->stopNote();
    }
  }

  // stopVoices()'s own natural release (voice->stopNote(), full authored
  // release tail - never fastRelease()'s short ~10ms envelope, that's for
  // inaudibly reclaiming a voice under a fresh attack, not for a musical
  // stop), just generalized to every column at once rather than one at a
  // time - used when a track's pattern/clip is pulled out from under it
  // (Launchpad Session view's "stop this track") and there's no single
  // column to target, unlike an ordinary per-column stop.
  virtual void stopAllVoices() {
    for (auto & [ column, voices ] : voices_) {
      for (auto & voice : voices) if (voice->isActive()) voice->stopNote();
    }
  }

  void clear() override {
    TrackState::clear();
    voices_.clear();
  }

  bool isActive() const override {
    for (auto & [ column, voices ] : voices_) {
      for (auto & voice : voices) {
	if (voice->isActive()) return true;
      }
    }
    return false;
  }

  int getVoiceCount() const override {
    int n = TrackState::getVoiceCount();
    for (auto & [ column, voices ] : voices_) {
      for (auto & voice : voices) {
	n += voice->getVoiceCount();
      }
    }
    return n;
  }

  int getAllocatedVoiceCount() const override {
    int n = TrackState::getAllocatedVoiceCount();
    for (auto & [ column, voices ] : voices_) {
      for (auto & voice : voices) {
	n += voice->getAllocatedVoiceCount();
      }
    }
    return n;
  }

  void getAllActiveVoices(std::unordered_map<int, std::vector<ActiveVoiceInfo> > & out) const override {
    std::vector<ActiveVoiceInfo> own;
    for (auto & [ column, voices ] : voices_) {
      for (auto & voice : voices) {
	if (voice->isActive()) own.push_back({ voice->getNoteValue(), voice->getLoudness() });
      }
    }
    if (!own.empty()) out[track_id_] = std::move(own);
    TrackState::getAllActiveVoices(out);
  }

  // Live control changes, pushed from the UI thread via PlaybackControlEvent
  // (SET_TRACK_MUTED/SOLO/SEND_A/SEND_B/SEND_MAIN - see Player::handleEvent) and
  // applied directly to this already-running state, unlike the constructor
  // argument above which only seeds the initial value at song load.
  bool isMuted() const { return muted_; }
  void setMuted(bool m) { muted_ = m; }

  bool isSolo() const { return solo_; }
  void setSolo(bool s) { solo_ = s; }

  // Send Main/A/B all push into every already-active voice too, not just
  // future notes/triggers (same reasoning as setAzimuth()/adjustAzimuth()
  // below: sends_ isn't read fresh from anywhere but this voice's own
  // construction otherwise). Reuses the same
  // VoiceState::adjust*() virtual-recursion mechanism adjustAzimuth() does
  // (so a multi-region SoundFontInstrument group's real leaf voices are all
  // reached too), just carrying an absolute value instead of a per-tick
  // delta - there's no tick-scheduled slide command for sends the way
  // there is for azimuth, these are live knobs (Launchpad/UI Send rows),
  // not a pattern effect. See VoiceState::adjustSendMain()/adjustSendA()/
  // adjustSendB().
  void setSendMain(float s) {
    send_main_ramp_.snapTo(linearToDbForRamp(s)); // an instant set always wins outright over any glide in flight
    applySendMain(s);
  }
  void setSendA(float s) {
    send_a_ramp_.snapTo(linearToDbForRamp(s));
    applySendA(s);
  }
  void setSendB(float s) {
    send_b_ramp_.snapTo(linearToDbForRamp(s));
    applySendB(s);
  }

  // The server-side counterpart of what used to be a Launchpad fader
  // press's own client-side glide (now instant/one-shot on that side):
  // starts this track's own Send Main/A/B moving from wherever it
  // actually is right now toward `target_db`, reaching it after `frames`
  // frames. `target_db`, not linear gain - the ramp itself interpolates
  // in dB (see its own declaration comment for why), so this takes the
  // same unit the row it's headed toward is defined in, not the unit
  // sends_/SendLevels happen to store. Advanced by advanceSendRamps()
  // below, once per render chunk - never ticked from the UI thread. See
  // glideAzimuth() below for Pan's own equivalent.
  void glideSendMain(float target_db, int frames) { send_main_ramp_.glideTo(target_db, frames); }
  void glideSendA(float target_db, int frames) { send_a_ramp_.glideTo(target_db, frames); }
  void glideSendB(float target_db, int frames) { send_b_ramp_.glideTo(target_db, frames); }

  // Called once per render chunk from renderVoices() below (the same hook
  // YLxx/YRxx's own per-tick azimuth slide already advances through, so a
  // glide crossing a chunk boundary mid-block still lands correctly) -
  // converts each active ramp's newly-advanced dB value back to linear
  // gain right here (dbToLinearForRamp()), the one point this ever
  // crosses from the ramp's own dB space into sends_/SendLevels' linear
  // one, then pushes it to applySendMain()/A()/B() (never back through
  // setSendMain()/A()/B() above, which would immediately snapTo() and
  // kill the very ramp this is in the middle of advancing). A ramp
  // that isn't active is skipped outright - once settled, there's
  // nothing left to push every single block forever.
  void advanceSendRamps(int frames) {
    if (send_main_ramp_.isActive()) applySendMain(dbToLinearForRamp(send_main_ramp_.advance(frames)));
    if (send_a_ramp_.isActive()) applySendA(dbToLinearForRamp(send_a_ramp_.advance(frames)));
    if (send_b_ramp_.isActive()) applySendB(dbToLinearForRamp(send_b_ramp_.advance(frames)));
  }

  // The live-knob path (Launchpad/UI Pan row, via Controller::
  // setTrackAzimuth()) - reaches every already-active voice too, not just
  // future triggers, by reusing adjustAzimuth() below with the absolute-to-
  // delta conversion done here. An instant set always wins outright over
  // any glide in flight, same as setSendMain()/A()/B() above.
  void setAzimuth(float a) {
    azimuth_ramp_.snapTo(a);
    adjustAzimuth(a - position_.azimuth);
  }
  float getAzimuth() const { return position_.azimuth; }

  // Pan's own counterpart of glideSendMain()/A()/B() above - a Launchpad
  // Pan press reaches every already-sounding voice the same instant a
  // plain setAzimuth() does (adjustAzimuth() below), so an abrupt press
  // needs the same velocity-scaled glide treatment a Volume/Send step
  // does, not just an instant snap. Circular, unlike the three above: the
  // delta toward `target_degrees` is wrapped into (-180,180] first, so the
  // glide always turns whichever way is actually shorter around the
  // circle (e.g. 170 degrees to -170 degrees moves 20 degrees through
  // +-180, not 340 degrees back through 0) rather than always increasing.
  //
  // Also folds position_.azimuth (and the ramp's own current_/target_)
  // back into a bounded range first, whenever the ramp happens to be at
  // rest right now (a fresh press - a press retargeting a still-in-flight
  // glide skips this, see the guard below): purely a change of reference
  // point, congruent mod 360, so nothing audible moves (a track/voice's
  // own azimuth only ever matters through periodic trig functions, never
  // as a raw number). Without this, a long run of presses landing exactly
  // 180 degrees apart - two Pan rows directly opposite each other, a
  // perfectly ordinary thing to press back and forth - walked
  // position_.azimuth off by 180 every single time (a real, observed
  // bug: the exact-degrees-apart tie always broke the same way, so it
  // never averaged out, only accumulated) rather than settling into a
  // stable oscillation. `adjustAzimuth()`'s own unbounded accumulation
  // (YLxx/YRxx's own relative ticks, which deliberately need to cross
  // +-180 to reach "behind") is untouched - this only ever rebases the
  // *resting* baseline a fresh glide measures its own delta from.
  void glideAzimuth(float target_degrees, int frames) {
    // Only while at rest - snapTo() would otherwise overwrite an
    // in-flight glide's own target_ too, cutting it short instead of
    // retargeting it smoothly from wherever it actually is. position_.
    // azimuth and the ramp's own current_/target_ are guaranteed equal
    // right now (nothing else touches either between calls), which is
    // what makes rebasing both by the same offset safe.
    if (!azimuth_ramp_.isActive()) {
      float rebased = fmodf(position_.azimuth, 360.0f);
      if (rebased > 180.0f) rebased -= 360.0f;
      else if (rebased <= -180.0f) rebased += 360.0f;
      if (rebased != position_.azimuth) {
        position_.azimuth = rebased;
        azimuth_ramp_.snapTo(rebased);
      }
    }
    float delta = fmodf(target_degrees - position_.azimuth, 360.0f);
    if (delta > 180.0f) delta -= 360.0f;
    else if (delta <= -180.0f) delta += 360.0f;
    azimuth_ramp_.glideTo(position_.azimuth + delta, frames);
  }

  // advanceSendRamps()'s own azimuth sibling, called alongside it from
  // renderVoices() below - converts the ramp's newly-advanced absolute
  // degrees into the delta adjustAzimuth() actually wants (never back
  // through setAzimuth() above, which would immediately snapTo() and kill
  // the very ramp this is in the middle of advancing).
  void advanceAzimuthRamp(int frames) {
    if (azimuth_ramp_.isActive()) adjustAzimuth(azimuth_ramp_.advance(frames) - position_.azimuth);
  }

  // Shared by the live Pan-row knob above and the YLxx/YRxx azimuth slide
  // (Command::isAzimuthSlide(), scheduled per-tick by SongState::
  // scheduleAzimuthSlide(), consumed by InstrumentTrackState::render()'s own
  // chunked loop) - both are just different sources of a delta that should
  // audibly move whatever is currently sounding, not only future notes.
  // VoiceState::adjustAzimuth()'s default recursion (overridden by
  // InstrumentVoice - see its own comment) makes this correct even for a
  // multi-region SoundFontInstrument group. No longer an override of
  // anything on TrackState (azimuth is a VoiceState-only concept).
  void adjustAzimuth(float delta) {
    position_.azimuth += delta;
    for (auto & [ column, voices ] : voices_) {
      for (auto & voice : voices) if (voice->isActive()) voice->adjustAzimuth(delta);
    }
  }

protected:
  // Read access to this track's own position/sends/id for a subclass that
  // needs to construct its own voices directly (e.g. ArpeggiatorState
  // triggering a step, SampleTrackState::triggerClip()) rather than through
  // the normal pending-events path, which already has position_/sends_ in
  // scope. Mirrors getChannelConfiguration()'s existing public accessor for
  // the same otherwise-private-to-this-class piece of construction state.
  const SphericalPosition & getPosition() const { return position_; }
  const SendLevels & getSends() const { return sends_; }
  int getTrackId() const { return track_id_; }

  static inline bool is_not_playing(const std::unique_ptr<VoiceState> & voice) { return !voice->isActive(); }

  void clearFinishedVoices() {
    for (auto & [ id, voices ] : voices_) {
      voices.erase(std::remove_if(voices.begin(), voices.end(), is_not_playing), voices.end());
    }
  }

  // Every voice this track currently owns, keyed by column - a single note
  // column for a plain InstrumentTrackState note-on, always column 0 for a
  // SampleTrackState clip (a SampleTrack only ever plays one clip at a
  // time). Protected, not private: InstrumentTrackState's own note-column/
  // chord/pressure/exclusive-class bookkeeping (retriggerVoices(),
  // chokeExclusiveClasses(), pushChannelPressureToVoices()) iterates it
  // directly.
  std::unordered_map<int, std::vector<std::unique_ptr<VoiceState> > > voices_;

private:
  // setSendMain()/A()/B()'s own "store it, push it to every active voice"
  // half, factored out so advanceSendRamps() (already mid-advance() on its
  // own ramp) can reach it directly without routing back through a public
  // setter that would snapTo() and cut its own glide short.
  void applySendMain(float s) {
    sends_.main = s;
    for (auto & [ column, voices ] : voices_) {
      for (auto & voice : voices) if (voice->isActive()) voice->adjustSendMain(s);
    }
  }
  void applySendA(float s) {
    sends_.a = s;
    for (auto & [ column, voices ] : voices_) {
      for (auto & voice : voices) if (voice->isActive()) voice->adjustSendA(s);
    }
  }
  void applySendB(float s) {
    sends_.b = s;
    for (auto & [ column, voices ] : voices_) {
      for (auto & voice : voices) if (voice->isActive()) voice->adjustSendB(s);
    }
  }

  bool solo_, muted_;
  int track_id_;
  SphericalPosition position_;
  SendLevels sends_;
  // In dB, not the linear gain sends_/SendLevels actually store - the
  // Launchpad row layout these glides are driven from (sendRowToDb()) is
  // itself linear in dB, so ramping in dB is what makes a glide's own
  // row-position move at a constant rate over time; ramping the
  // underlying linear gain directly (an earlier revision of this class)
  // instead moved through rows very unevenly - fast near the bottom
  // (where a tiny linear step is a huge dB jump) and crawling near the
  // top (where a linear step near unity is a tiny dB one) - since a
  // uniform linear-per-frame step is very much not a uniform dB-per-frame
  // one. dbToLinearForRamp()/linearToDbForRamp() convert at the only two
  // places that ever need to: seeding/instant-setting the ramp
  // (linearToDbForRamp(), since sends_ itself is always linear) and
  // reading it back out in advanceSendRamps() (dbToLinearForRamp()).
  dsp::ValueRamp send_main_ramp_, send_a_ramp_, send_b_ramp_;
  // Plain degrees, unlike the three above - azimuth has no equivalent of
  // dB's "uneven row spacing" problem, so it ramps directly in the same
  // unit rowToAzimuth()/position_.azimuth already use. See glideAzimuth()
  // for how its own target is chosen (shortest way around the circle).
  dsp::ValueRamp azimuth_ramp_;
};

#endif
