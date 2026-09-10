#include "LaunchpadManager.h"

#include "LaunchpadIO.h"
#include "LaunchpadLayout.h"
#include "LaunchpadProtocol.h"
#include "LaunchpadPadEvent.h"
#include "LaunchpadChannelPressureEvent.h"
#include "../state/PlaybackInfo.h"
#include "../playback/PlaybackControlEvent.h"
#include "../model/Song.h"
#include "../model/LeafTrack.h"
#include "../model/InstrumentTrack.h"
#include "../model/PercussionTrack.h"
#include "../model/SongStructure.h"
#include "../model/ArrangementOps.h"
#include "../Controller.h"
#include "../util/constants.h"
#include "../model/Color.h"

#include <algorithm>
#include <chrono>

using namespace std;

namespace {
  // How long CC97 (DRAW mode toggle, handleDrawToggleButton()) or CC98
  // (drum config, handleDrumConfigButton()) must be held before release
  // means "clear" instead of "toggle". Long enough that a normal
  // deliberate tap never accidentally clears instead.
  constexpr auto kDrawClearHoldThreshold = std::chrono::milliseconds(600);

  // How long a DRAW-mode grid pad must be held before release means "just
  // adjust brightness" instead of "cycle to the next hue" - see
  // releaseDrawPad(). Same value as kDrawClearHoldThreshold above (both are
  // "long press" thresholds for a DRAW-mode gesture) but named/declared
  // separately since they're conceptually independent controls that could
  // reasonably be tuned apart later.
  constexpr auto kDrawPadLongPressThreshold = std::chrono::milliseconds(600);

  struct Rgb { uint8_t r, g, b; };

  // DRAW mode's coloring-toy palette - a plain rainbow, cycling back to
  // off. Order/values are not meaningful the way the consonance-hierarchy
  // colors elsewhere in this file are (no music-theory landmark to
  // preserve here), just distinct and bright
  // enough to be satisfying to press through. No white entry here - white
  // is no longer a selectable hue, it's what a hue turns into at maximum
  // press/aftertouch intensity (see colorForDrawPad() below), so it isn't
  // also a separate, independently-cyclable palette slot.
  constexpr Rgb DRAW_PALETTE[] = {
    {0,   0,   0},   // off
    {127, 0,   0},   // red
    {127, 60,  0},   // orange
    {127, 127, 0},   // yellow
    {0,   127, 0},   // green
    {0,   127, 127}, // cyan
    {0,   0,   127}, // blue
    {90,  0,   127}, // purple
  };
  constexpr int DRAW_PALETTE_SIZE = sizeof(DRAW_PALETTE) / sizeof(DRAW_PALETTE[0]);

  // DRAW mode's brightness ramp for a single pad: black (intensity 0) up
  // to the pad's selected hue at full saturation (intensity
  // kWhiteBlendStart), then a second segment blending that hue further up
  // to pure white as intensity keeps climbing toward the true numeric
  // ceiling (127 - the largest value a MIDI press velocity or aftertouch
  // pressure can ever report) - a "hot"-style colormap, not a flat
  // brightness scale, so hitting the actual maximum reads as a
  // qualitatively distinct "maxed out" white, not just "the brightest
  // version of whatever hue is selected". kWhiteBlendStart=100 leaves a
  // deliberately wide top zone (100-127) for the white blend to be
  // visible as its own distinct region, not a single-value cliff.
  constexpr int kWhiteBlendStart = 100;

  Rgb colorForDrawPad(const Rgb & hue, int intensity) {
    // "Off" (the black palette entry) has no hue to modulate - without this,
    // a pad that was pressed hard before being cycled/cleared back to off
    // would still blend toward white at the top of the intensity range
    // (intensity > kWhiteBlendStart doesn't care what hue.r/g/b are), so it
    // would visibly fail to go fully dark.
    if (hue.r == 0 && hue.g == 0 && hue.b == 0) return {0, 0, 0};
    if (intensity <= 0) return {0, 0, 0};
    if (intensity >= 127) return {127, 127, 127};
    if (intensity <= kWhiteBlendStart) {
      float t = static_cast<float>(intensity) / static_cast<float>(kWhiteBlendStart);
      return {
        static_cast<uint8_t>(static_cast<float>(hue.r) * t),
        static_cast<uint8_t>(static_cast<float>(hue.g) * t),
        static_cast<uint8_t>(static_cast<float>(hue.b) * t),
      };
    }
    float t = static_cast<float>(intensity - kWhiteBlendStart) / static_cast<float>(127 - kWhiteBlendStart);
    return {
      static_cast<uint8_t>(static_cast<float>(hue.r) + static_cast<float>(127 - hue.r) * t),
      static_cast<uint8_t>(static_cast<float>(hue.g) + static_cast<float>(127 - hue.g) * t),
      static_cast<uint8_t>(static_cast<float>(hue.b) + static_cast<float>(127 - hue.b) * t),
    };
  }

  // LaunchpadLayout::noteForPad/classifyPad treat pad (0,0) as the tonic
  // (base_note) - a pure, logical coordinate system with no notion of a
  // "physical middle." Anchoring that logical origin at the bottom-left
  // *physical* pad (the original behavior - passing x,y straight through)
  // pushes the diatonic scale out along the grid's edges and leaves DIESIS
  // (31/53-EDO's dense, quarter-tone-ish in-between notes - there are far
  // more of them than diatonic degrees) dominating the middle, since the
  // middle ends up farthest from the one tonic-anchored corner.
  //
  // (1,3), chosen with 31-EDO as the priority target (the tuning actually
  // in use), is the result of exhaustively scoring every candidate origin's
  // (diatonic - diesis) count in the middle 4x4 pads, with a second pass
  // fixing x so the two visible tonic pads' span is horizontally centered
  // on the grid: the octave-equivalent step in this T/S coordinate system
  // is (Δx,Δy) = (5,2), landing a second tonic 5 columns to the right of
  // the first - x=1 (span [1,6], centered on the grid's own midpoint 3.5)
  // beats x=2's off-center span [2,7] for that reason, even though x=2
  // alone scores marginally higher on the raw diatonic-vs-diesis count
  // (+3 vs +2) - centering the tonic pair visually won out over that small
  // difference. Also clearly beats the corner anchor (original behavior,
  // x=y=0) for every other supported EDO too (12-EDO already has no DIESIS
  // at all; 53-EDO's sheer note density - 53 pitches/octave into 64 pads -
  // keeps some diesis in the middle regardless of anchor, but noticeably
  // less than the corner anchor left there).
  constexpr int GRID_ORIGIN_X = 1, GRID_ORIGIN_Y = 3;

  struct Hsl { float h, s, l; }; // h in [0,360), s/l in [0,1]

  Hsl rgbToHsl(Rgb c) {
    float r = c.r / 127.0f, g = c.g / 127.0f, b = c.b / 127.0f;
    float maxc = max({r, g, b}), minc = min({r, g, b});
    float l = (maxc + minc) / 2.0f;
    if (maxc == minc) return {0.0f, 0.0f, l}; // achromatic (includes black)

    float d = maxc - minc;
    float s = l > 0.5f ? d / (2.0f - maxc - minc) : d / (maxc + minc);
    float h;
    if (maxc == r) h = (g - b) / d + (g < b ? 6.0f : 0.0f);
    else if (maxc == g) h = (b - r) / d + 2.0f;
    else h = (r - g) / d + 4.0f;
    return {h * 60.0f, s, l};
  }

  // Delegates to Color's own HSL->RGB math (see Color::fromHSL(),
  // src/model/Color.h) rather than keeping a second copy of the same
  // conversion - a Launchpad pad color is just that same math rescaled
  // from Color's 0-255 range down to the hardware's own 0-127
  // velocity-scaled range.
  Rgb hslToRgb(Hsl hsl) {
    auto c = Color::fromHSL(hsl.h, hsl.s, hsl.l);
    auto to127 = [](int v) { return static_cast<uint8_t>(v * 127 / 255); };
    return {to127(c.getRed()), to127(c.getGreen()), to127(c.getBlue())};
  }

  // Idle luminosity vs. the luminosity of a pad whose voice is at full
  // loudness - hue/saturation come from the base consonance/percussion
  // color and are otherwise untouched, only lightness ramps between the
  // two as that voice's loudness (its own gain, decaying with any
  // envelope it has) moves from 0 to 1.
  constexpr float LAUNCHPAD_IDLE_LUMINOSITY = 0.35f;
  constexpr float LAUNCHPAD_ACTIVE_LUMINOSITY = 1.0f;

  // The drum picker's own idle/assigned lightness levels - deliberately
  // its own pair, not a reuse of
  // LAUNCHPAD_IDLE_LUMINOSITY/LAUNCHPAD_ACTIVE_LUMINOSITY above. Those two
  // are tuned for a *sounding note's* idle->loud ramp, where 1.0 at full
  // loudness is fine; the picker instead shows a static picked/unpicked
  // distinction, and HSL lightness of 1.0 renders as white regardless of
  // hue - on real hardware an "assigned" pad using LAUNCHPAD_ACTIVE_LUMINOSITY
  // read as washed-out white rather than its family color.
  // Both levels stay well under 1.0 so the family hue stays visible at
  // both states, and idle is dimmer than the general-purpose idle level
  // too, since the picker's unpicked pads should read as clearly
  // secondary to the assigned ones. Not yet confirmed against real
  // hardware for exact tuning - like percussionFamilyColor()'s own hues,
  // expect these to shift after testing.
  constexpr float LAUNCHPAD_PICKER_IDLE_LUMINOSITY = 0.15f;
  constexpr float LAUNCHPAD_PICKER_ASSIGNED_LUMINOSITY = 0.65f;

  // The track-picker overlay's own row (see DeviceState::
  // track_picker_active's own comment) - the bottom grid row (y=0 - see
  // LaunchpadProtocol::padToNoteNumber()'s own doc comment for the
  // y-flip), one pad per selectable track, matching session_.track_ids/
  // Session view's own column order. Every other row is left exactly as
  // Session view's own rendering already drew it - the overlay is
  // Session-view-only (GridMode's own comment), so there's no other
  // GridMode content underneath to distinguish it from, and Session view
  // stays fully interactive there (isTrackPickerRow()).
  constexpr int LAUNCHPAD_TRACK_PICKER_ROW = 0;

  // One hue per DeviceState::TrackPickerPurpose, shared by both the opener
  // button's own LED (extra-button section below) and the picker row
  // itself (refreshLeds()' track-picker post-process pass) - one color
  // naming "which action this is" wherever it shows up, rather than two
  // independently-tuned copies. Brightness within a purpose's hue is what
  // tells columns apart in the picker row (dim/bright meaning is
  // purpose-specific - see LAUNCHPAD_TRACK_PICKER_ROW's own comment).
  constexpr Rgb LAUNCHPAD_TRACK_PICKER_STOP_CLIP_BRIGHT { 127, 0, 0 };
  constexpr Rgb LAUNCHPAD_TRACK_PICKER_STOP_CLIP_DIM    { 20, 0, 0 };
  constexpr Rgb LAUNCHPAD_TRACK_PICKER_SOLO_BRIGHT      { 0, 0, 127 };
  constexpr Rgb LAUNCHPAD_TRACK_PICKER_SOLO_DIM         { 0, 0, 20 };
  constexpr Rgb LAUNCHPAD_TRACK_PICKER_MUTE_BRIGHT      { 127, 127, 0 };
  constexpr Rgb LAUNCHPAD_TRACK_PICKER_MUTE_DIM         { 20, 20, 0 };

  // The mixer radio group's remaining four members - Volume/Pan/Send A/
  // Send B, which stay a real GridMode swap rather than an overlay (see
  // GridMode's own comment) - get the same bright-when-active/dim-
  // otherwise treatment as the three track-picker purposes above, each
  // keeping the hue its own static color already established (magenta/
  // cyan/orange/yellow) rather than adopting red/blue/yellow like the
  // picker trio, since these four don't share the picker's single-overlay
  // identity.
  constexpr Rgb LAUNCHPAD_MIXER_SEND_B_BRIGHT { 127, 0, 127 };
  constexpr Rgb LAUNCHPAD_MIXER_SEND_B_DIM    { 40, 0, 40 };
  constexpr Rgb LAUNCHPAD_MIXER_SEND_A_BRIGHT { 0, 127, 127 };
  constexpr Rgb LAUNCHPAD_MIXER_SEND_A_DIM    { 0, 40, 40 };
  constexpr Rgb LAUNCHPAD_MIXER_PAN_BRIGHT    { 127, 64, 0 };
  constexpr Rgb LAUNCHPAD_MIXER_PAN_DIM       { 40, 20, 0 };
  constexpr Rgb LAUNCHPAD_MIXER_VOLUME_BRIGHT { 127, 127, 0 };
  constexpr Rgb LAUNCHPAD_MIXER_VOLUME_DIM    { 40, 40, 0 };

  // Session's mixer-submode radio group (see DeviceState::
  // session_mixer_mode's own comment), all seven buttons, while that
  // submode is off - a plain scene-launch trigger has no state of its own
  // worth showing, so all seven go uniformly dim white rather than any of
  // their mixer-mode hues (which would otherwise misleadingly suggest a
  // fader/picker is one press away) - same dim-white convention the
  // static move-row-up/down utility buttons already use.
  constexpr Rgb LAUNCHPAD_SCENE_LAUNCH_BUTTON_COLOR { 30, 30, 30 };

  Rgb padColor(Rgb base, const unordered_map<int, float> & active_note_loudness, int note_value) {
    if (base.r == 0 && base.g == 0 && base.b == 0) return base; // stays off (e.g. unused/reserved pads)

    float loudness = 0.0f;
    if (note_value >= 0) {
      auto it = active_note_loudness.find(note_value);
      if (it != active_note_loudness.end()) loudness = clamp(it->second, 0.0f, 1.0f);
    }

    auto hsl = rgbToHsl(base);
    hsl.l = LAUNCHPAD_IDLE_LUMINOSITY + (LAUNCHPAD_ACTIVE_LUMINOSITY - LAUNCHPAD_IDLE_LUMINOSITY) * loudness;
    return hslToRgb(hsl);
  }

  // Since padColor() above unconditionally overrides lightness (it's the
  // channel that carries loudness instead), hue and saturation are the
  // only two channels a consonance-tier color actually gets to keep once
  // rendered - so this builds an Hsl directly from a PadClassification
  // rather than going through an intermediate Rgb constant the way the
  // old fixed per-category palette did. Lightness here is a placeholder
  // (any well-formed value works, since padColor() replaces it).
  constexpr float kConsonanceTonicSaturation = 1.0f;
  // FOURTH and FIFTH deliberately share one saturation too (see
  // LaunchpadLayout's kFourthFifthHue comment), slightly desaturated
  // relative to tonic (a touch less vivid, still clearly prominent).
  constexpr float kConsonanceFourthFifthSaturation = 0.8f;
  // Depth 3 (RECURSIVE tier) is "somewhat prominent still", at higher
  // saturation; depth 4+ shares one more muted saturation rather than
  // continuing to differentiate by shade - every pitch class still gets
  // its own real family hue at every depth (LaunchpadLayout's hue drift
  // never stops, see its own kDepth3HueOffset/kDepth4HueOffset comment),
  // it's only saturation that stops distinguishing shades past depth 4.
  constexpr float kConsonanceDepth3Saturation = 0.85f;
  constexpr float kConsonanceDepth4PlusSaturation = 0.5f;

  // ENHARMONIC pads (LaunchpadLayout's enharmonic_blend > 0) are
  // achromatic (or nearly so) rather than a family hue, so they read as a
  // single, unmistakable "not cleanly major or minor" category rather
  // than a shade that has to compete for attention with the RECURSIVE
  // hue drift's own blue/violet/magenta band - but split into two shades
  // by enharmonic_depth (see LaunchpadLayout's own comment on that
  // field), not one flat grey for every strength of collision. Depth <=
  // kConsonanceEnharmonicShallowMaxDepth - a structurally fundamental
  // collision, e.g. a third's own immediate split (this is "D"'s own
  // level in every EDO that has any collision at all) - gets a
  // deliberately muted, barely-there green instead of plain grey, so
  // that set is still visually distinguishable from a pitch class only
  // implicated via many/deeper ratio pileups (plain grey). Saturation is
  // kept low specifically because green is not: real Launchpad X hardware
  // reads green LEDs as much more prominent than blue/violet/grey ones
  // regardless of the saturation value sent (see this file's other
  // green-avoidance comments) - not yet confirmed how low this specific
  // saturation needs to go to actually read as "muted" rather than a
  // fully separate hue category in its own right.
  constexpr float kConsonanceEnharmonicShallowHue = 120.0f; // green
  constexpr float kConsonanceEnharmonicShallowSaturation = 0.25f;
  constexpr int kConsonanceEnharmonicShallowMaxDepth = 3;

  Rgb consonanceColor(const LaunchpadLayout::PadClassification & classification) {
    if (classification.enharmonic_blend > 0.0f) {
      if (classification.enharmonic_depth <= kConsonanceEnharmonicShallowMaxDepth) {
        return hslToRgb({kConsonanceEnharmonicShallowHue, kConsonanceEnharmonicShallowSaturation, 0.5f});
      }
      return hslToRgb({0.0f, 0.0f, 0.5f}); // grey; padColor() overrides lightness regardless
    }
    Hsl hsl;
    if (classification.tier == LaunchpadLayout::PadTier::TONIC) {
      hsl = {classification.hue, kConsonanceTonicSaturation, 0.5f};
    } else if (classification.tier == LaunchpadLayout::PadTier::FOURTH || classification.tier == LaunchpadLayout::PadTier::FIFTH) {
      hsl = {classification.hue, kConsonanceFourthFifthSaturation, 0.5f};
    } else { // RECURSIVE.
      auto saturation = classification.depth <= 3 ? kConsonanceDepth3Saturation : kConsonanceDepth4PlusSaturation;
      hsl = {classification.hue, saturation, 0.5f};
    }
    return hslToRgb(hsl);
  }

  // The free-drumming layout's per-family base color - shared by the
  // ordinary percussion note-grid rendering and the drum picker, which
  // reuses the exact same layout as its own
  // picking surface, so the two never drift into two independently-
  // maintained copies of the same palette. Hues are an initial pass, not
  // yet confirmed against real hardware (see LaunchpadLayout.cpp's own
  // note on RECURSIVE-tier hue tuning for how that confirmation loop has
  // gone elsewhere in this file) - expect these to shift after real
  // testing.
  Rgb percussionFamilyColor(LaunchpadLayout::PercussionFamily family) {
    switch (family) {
    case LaunchpadLayout::PercussionFamily::CORE:            return {127, 0,   0};   // red
    case LaunchpadLayout::PercussionFamily::HI_HAT:          return {127, 127, 0};   // yellow
    case LaunchpadLayout::PercussionFamily::TOMS:            return {127, 50,  0};   // orange
    case LaunchpadLayout::PercussionFamily::CYMBALS:         return {0,   127, 127}; // cyan
    case LaunchpadLayout::PercussionFamily::KIT_ACCESSORIES: return {127, 32,  80};  // pink
    case LaunchpadLayout::PercussionFamily::LATIN_DRUMS:     return {0,   127, 0};   // green
    case LaunchpadLayout::PercussionFamily::LATIN_METAL:     return {127, 0,   127}; // magenta
    case LaunchpadLayout::PercussionFamily::SHAKERS:         return {0,   0,   127}; // blue
    case LaunchpadLayout::PercussionFamily::WOODS:           return {80,  0,   127}; // purple
    case LaunchpadLayout::PercussionFamily::CUICA_WHISTLE:   return {127, 127, 127}; // white
    case LaunchpadLayout::PercussionFamily::ELECTRONIC:      return {40,  40,  40};
    default:                                                  return {0,   0,   0};  // UNUSED
    }
  }

  // Pan mode maps a track's azimuth to one of 8 compass points spaced 45
  // degrees apart around the *full* circle (not clamped to a stereo-like
  // +-90 range) - this is a full 3D ambisonic engine, not a stereo panner,
  // so "behind" positions are just as reachable as "in front" ones. Row 4
  // (dead center of the 8) lands exactly on 0 degrees (front) for a
  // memorable, symmetric mapping.
  constexpr float PAN_ROW_DEGREES = 45.0f;

  // Send A/B/Main fader curve: row 1 is this many dB below unity (row 7);
  // 6dB/step is a standard, easily-perceived mixing-console increment. Row
  // 0 is a hard off (see sendRowToDb()), not a further 6dB step, since a
  // real fader's bottom position is true silence, not just very quiet.
  constexpr float SEND_ROW_FLOOR_DB = -36.0f;

  // Self-contained (not TreeNode::gainToDecibels(), only reachable from
  // TreeNode<Derived> subclasses - VoiceState/TrackState, neither of which
  // LaunchpadManager is) - the same "each file keeps its own small dB
  // helper" convention model/LeafTrack.cpp's own linearToDb() and
  // Controller.cpp's own dbToLinear() already use, including the same
  // -100dB "off" floor.
  float linearToDb(float linear) { return linear <= 0.00001f ? -100.0f : 20.0f * log10f(linear); }

  // Automatic per-model octave starting point, applied once (in refresh())
  // the first time a device is seen - not a wire-protocol fact (unlike
  // LaunchpadProtocol::ModelInfo's fields), just a UX default: with two
  // Launchpads connected side by side, the physically smaller one starts
  // higher, so they don't collide in the same register the way two
  // identical default octaves would. Ordered by each model's actual
  // physical footprint (Mini MK3 the most compact, Pro MK3 the largest,
  // with its extra left column and control rows) - a device that later
  // gets its own octave-up/octave-down press (see LaunchpadProtocol::
  // commandForButton's own comment on that being currently unreachable)
  // moves independently from this starting point, same as any other
  // manually-adjusted device.
  int defaultOctaveOffsetForModel(LaunchpadProtocol::Model model) {
    switch (model) {
    case LaunchpadProtocol::Model::MINI_MK3: return 1;
    case LaunchpadProtocol::Model::X:        return 0;
    case LaunchpadProtocol::Model::PRO_MK3:  return -1;
    }
    return 0;
  }

  // Fires one step's worth of `pattern`'s own notes (at row step % length)
  // as one-shot PLAY_NOTE audition events for `track_id` - the clip
  // equivalent of triggerAuditionStep()'s own per-PercussionTrack firing,
  // generalized to any track/note rather than a lane hit specifically
  // (column = the note's own position within that row, matching how a
  // pattern-driven note is scheduled normally, not PercussionTrack's own
  // by-value column convention). `length` is the clip's own length
  // (Clip::getLength(), already clamped to at least 1 by the caller) -
  // `pattern` is just the leaf Pattern's notes, not a length source of its
  // own (Pattern::getLength() is unrelated to a clip's length - see
  // Clip.h). A plain rest (an undefined cell) fires no explicit STOP_NOTE,
  // same reasoning as triggerAuditionStep(): relies on the instrument's
  // own envelope/choke machinery past that - but an explicit off row does
  // push one (see its own comment below), since that duration was
  // actually recorded, not merely implied by a note's own natural decay.
  // Shared by triggerClipStep()'s own per-tick loop and
  // handleSessionPadEvent()'s "nothing was playing yet, launch
  // immediately" case.
  void fireClipStep(const Song & song, Controller & controller, int track_id, const Pattern & pattern, int length, int step) {
    auto track = song.getMasterTrack().getChildByInternalId(track_id);
    if (!track) return;
    auto tuning = song.getTuningForTrack(*track);
    auto & notes = pattern.getNotes(pattern.getEffectiveRow(step, length));
    auto & event_queue = controller.getPlaybackEventQueue();
    for (size_t col = 0; col < notes.size(); col++) {
      auto & note = notes[col];
      if (note.isOff()) {
        // An explicit off (Session View's own recording writes one at the
        // release row - see LaunchpadManager::handlePadEvent()'s RELEASE
        // branch) has to actually stop the column here - unlike a plain
        // rest (an undefined cell, still skipped below), silently
        // skipping it would leave whatever's still sounding on this
        // column ringing on its own envelope forever, ignoring a duration
        // the performer explicitly recorded.
        event_queue.push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::STOP_NOTE, controller.getActiveBufferName(), track_id, static_cast<int>(col)));
        continue;
      }
      if (!note.isDefined() || note.isAftertouch()) continue;
      event_queue.push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::PLAY_NOTE, controller.getActiveBufferName(), track_id, static_cast<int>(col), note.getValue(), note.getVelocity()));
    }
    // tuning is resolved (getTuningForTrack) purely so a future caller
    // that needs it (e.g. a diagnostic) doesn't have to re-derive it -
    // Player.cpp's own PLAY_NOTE handler already resolves tuning/frequency
    // itself from the raw midi_note value this pushes.
    (void)tuning;
  }

  // The length of track_id's own currently-focused clip (Controller::
  // getFocusedClip()), or -1 if it doesn't resolve to a real clip on this
  // track (nothing focused, or a stale/dangling id) - callers already
  // gate on Controller::getFocusedClipTrackId() == track_id before
  // calling this. Used by the drum-machine step grid's own paging
  // (LaunchpadManager::handleCommand()/refresh()) to know how many
  // 8-step pages a focused clip actually spans, since a clip's own length
  // can be longer than the grid's fixed 8 columns.
  int focusedDrumClipLength(const Song & song, int track_id, const string & focused_clip_id) {
    for (auto & clip : song.getClips(track_id)) {
      if (clip.getId() == focused_clip_id) return std::max(1, clip.getLength());
    }
    return -1;
  }

  // fireClipStep()'s own dispatch, generalized over track type - a
  // SampleTrack's own clip is raw audio, not a Pattern of notes, so it's
  // fired once per loop iteration (or once, for a one-shot) via
  // PlaybackControlEvent::PLAY_SAMPLE_CLIP instead of per-row PLAY_NOTE
  // events; `step % length == 0` catches both the very first launch
  // (`step` == 0) and every later loop repeat in one condition. Every
  // other track type is unaffected - same fireClipStep() call as before.
  void fireOrTriggerClipStep(const Song & song, Controller & controller, int track_id, int clip_index, const Clip & clip, int length, int step) {
    auto track = song.getMasterTrack().getChildByInternalId(track_id);
    if (!track) return;

    if (track->getType() == TrackType::SAMPLE) {
      if (step % length == 0) {
        controller.getPlaybackEventQueue().push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::PLAY_SAMPLE_CLIP, controller.getActiveBufferName(), track_id, clip_index));
      }
      return;
    }

    fireClipStep(song, controller, track_id, clip.getLeafPattern(), length, step);
  }
}

int
LaunchpadManager::azimuthToRow(float azimuth) {
  float normalized = fmodf(azimuth + 180.0f, 360.0f);
  if (normalized < 0.0f) normalized += 360.0f;
  return static_cast<int>(lround(normalized / PAN_ROW_DEGREES)) % 8;
}

float
LaunchpadManager::rowToAzimuth(int row) {
  return static_cast<float>(row) * PAN_ROW_DEGREES - 180.0f;
}

float
LaunchpadManager::sendRowToDb(int row) {
  if (row <= 0) return -100.0f;
  return SEND_ROW_FLOOR_DB + (-SEND_ROW_FLOOR_DB) * static_cast<float>(row - 1) / 6.0f;
}

int
LaunchpadManager::sendLinearToRow(float linear) {
  float db = linearToDb(linear);
  // Nearer to off than to the lowest real (row-1) step - round down to the
  // hard-off row rather than the same half-step rounding the real steps
  // below use, so a value that's genuinely off (or migrated from one that
  // was) always redraws as row 0, not a barely-lit row 1.
  if (db <= SEND_ROW_FLOOR_DB - (-SEND_ROW_FLOOR_DB) / 6.0f / 2.0f) return 0;
  float row = 1.0f + (db - SEND_ROW_FLOOR_DB) * 6.0f / (-SEND_ROW_FLOOR_DB);
  return std::clamp(static_cast<int>(lround(row)), 0, 7);
}

LaunchpadManager::DeviceState &
LaunchpadManager::deviceState(int device_id) {
  return devices_[device_id];
}

const LaunchpadManager::DeviceState *
LaunchpadManager::findDeviceState(int device_id) const {
  auto it = devices_.find(device_id);
  return it == devices_.end() ? nullptr : &it->second;
}

LaunchpadManager::ActiveNote *
LaunchpadManager::findActiveNote(int device_id, int x, int y) {
  auto it = devices_.find(device_id);
  if (it == devices_.end()) return nullptr;
  auto note_it = it->second.active_notes.find({x, y});
  if (note_it == it->second.active_notes.end()) return nullptr;
  return &note_it->second;
}

void
LaunchpadManager::recordActiveNote(int device_id, int x, int y, ActiveNote note) {
  deviceState(device_id).active_notes[{x, y}] = note;
}

void
LaunchpadManager::clearActiveNote(int device_id, int x, int y) {
  auto it = devices_.find(device_id);
  if (it == devices_.end()) return;
  it->second.active_notes.erase({x, y});
}

bool
LaunchpadManager::hasAnyActiveNotes(int device_id) const {
  auto * state = findDeviceState(device_id);
  return state && !state->active_notes.empty();
}

int
LaunchpadManager::octave(int device_id) const {
  auto * state = findDeviceState(device_id);
  auto offset = state ? state->octave_offset : 0;
  return LaunchpadLayout::clampOctave(cached_global_octave_, offset);
}

void
LaunchpadManager::octaveUp(int device_id) {
  auto & state = deviceState(device_id);
  state.octave_offset = LaunchpadLayout::clampOctaveOffset(state.octave_offset, 1);
}

void
LaunchpadManager::octaveDown(int device_id) {
  auto & state = deviceState(device_id);
  state.octave_offset = LaunchpadLayout::clampOctaveOffset(state.octave_offset, -1);
}

int
LaunchpadManager::resolveTrackId(int device_id, const vector<int> & track_ids, int fallback_track_index) const {
  (void)device_id; // no more per-device track assignment - every device follows fallback_track_index
  if (track_ids.empty()) return -1;
  auto track_index = fallback_track_index;
  if (track_index < 0 || track_index >= static_cast<int>(track_ids.size())) return -1;
  return track_ids[static_cast<size_t>(track_index)];
}

LaunchpadManager::GridMode
LaunchpadManager::gridMode(int device_id) const {
  auto * state = findDeviceState(device_id);
  return state ? state->grid_mode : GridMode::NOTES;
}

void
LaunchpadManager::toggleGridMode(int device_id, GridMode mode) {
  auto & state = deviceState(device_id);
  // Session mixer-submode radio group member (see GridMode's own comment)
  // - a no-op unless already somewhere in that family (inSessionMixerFamily()),
  // so pressing a fader button from NOTES/CUSTOM/DRAW does nothing, but
  // pressing one while another family member (a different fader, or the
  // track-picker overlay) is already active switches straight to it.
  // Closing (a repeat press of the one already active) always lands back
  // on SESSION, not NOTES, since that's the only place these are ever
  // entered from any more.
  if (!inSessionMixerFamily(state)) return;
  bool already_active = state.grid_mode == mode;
  state.track_picker_active = false; // switching to (or off of) a fader always leaves the picker
  state.grid_mode = already_active ? GridMode::SESSION : mode;
}

bool
LaunchpadManager::inSessionMixerFamily(const DeviceState & state) const {
  return state.grid_mode == GridMode::SESSION || state.grid_mode == GridMode::SEND_MAIN ||
    state.grid_mode == GridMode::PAN || state.grid_mode == GridMode::SEND_A ||
    state.grid_mode == GridMode::SEND_B || state.track_picker_active;
}

void
LaunchpadManager::forceNotesModeOnAllDevices() {
  for (auto & [device_id, state] : devices_) {
    state.grid_mode = GridMode::NOTES;
    state.track_picker_active = false; // Session-view-only - see DeviceState::track_picker_active's own comment
  }
}

void
LaunchpadManager::forceSessionModeOnAllDevices() {
  for (auto & [device_id, state] : devices_) state.grid_mode = GridMode::SESSION;
}

void
LaunchpadManager::resetDrumEditPaging() {
  if (!launchpad_io_) return;
  auto ready_ids = launchpad_io_->readySessionIds();
  for (size_t i = 0; i < ready_ids.size(); i++) deviceState(ready_ids[i]).drum_edit_page = static_cast<int>(i);
}

void
LaunchpadManager::silenceOtherTriggeredClips(Controller & controller) {
  for (auto & [ track_id, unused ] : triggered_pattern_by_track_) {
    controller.getPlaybackEventQueue().push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::STOP_ALL_NOTES, controller.getActiveBufferName(), track_id));
  }
  triggered_pattern_by_track_.clear();
  queued_pattern_by_track_.clear();
  // Once nothing anywhere is triggered or pending, "beat 1" no longer
  // means anything - see triggerClipStep()'s own identical reasoning for
  // clearing this the same way once both maps go empty on their own.
  session_origin_set_ = false;
}

bool
LaunchpadManager::handleRawButton(int cc_number, int device_id, Controller & controller, int track_id) {
  // 69/79/89 confirmed against a real Launchpad X: Send A/Pan/Volume, 10
  // apart in that order (row 5/6/7 of the right column, CC = 19 + 10*row) -
  // not the arbitrary contiguous-slot guess this originally shipped with.
  // 59 (row 4, one further down the same sequence) is unconfirmed but a
  // strong inference: it matches the standard Launchpad right-column
  // "Track" control row order documented across DAW controller scripts
  // for this hardware (Volume, Pan, Send A, Send B, Stop, Mute, Solo,
  // Record Arm - Volume/Pan/SendA already lined up exactly with that
  // order at 89/79/69). 89/Volume is repurposed as the Send Main fader
  // mode - the same bargraph shape as Send A/Send B, just controlling how
  // much of each track's own voices reach the main mix (LeafTrack::
  // getSendMain()) rather than the shared send bus. 98 (DRAW mode toggle)
  // is handled separately, in handleDrawToggleButton() - unlike these four,
  // it needs to see both press and release to distinguish a quick tap from
  // a long hold, so UI::handleLaunchpadButtonEvent routes it there directly
  // rather than through this press-only entry point. The Programmer-mode
  // protocol also maps a CC (99) to the grid position one past the 91-98
  // top row, but on real Launchpad X hardware that top-right corner isn't
  // an actual pressable button (confirmed against a real unit - it lacks
  // the tactile structure every other cell has; the CC mapping there is
  // presumably just kept for symmetry with the Launchpad Pro, which does
  // have a real corner button) - so it's deliberately left unhandled here,
  // not wired to anything.
  // 19 (right column, continuing the "Track" control row order one
  // further past Send B - see this method's own doc comment) is the real
  // Launchpad X's own dedicated "Record Arm" button. What it actually does
  // depends on the currently selected track's own type (SampleTrack's own
  // threshold-armed cycle vs. every other type's plain capture-armed
  // toggle) and on whatever's already armed/recording (a press always
  // means "stop that" first, regardless of the current track) - entirely
  // "toggle-record-arm"'s own concern (Controller.cpp), not this file's,
  // since the identical decision has to be reachable without a Launchpad
  // connected at all (a keybinding, M-x). Moved here from CC98 ("Capture
  // MIDI", now DRAW mode's own home) - see DeviceState::capture_enabled's
  // own comment for why. CC98 used to be wired to toggle-playing via the
  // named-command pipeline before that; toggle-playing stays reachable
  // via Space either way. Deliberately untouched by Session's own mixer
  // submode below - Record Arm means the same thing regardless.
  if (cc_number == 19) {
    controller.sendCommand("toggle-record-arm");
    return true;
  }
  // 89/79/69/59/49/39/29 (Volume/Pan/SendA/SendB/Stop Clip/Mute/Solo, and
  // Pro MK3 left-column twins 30/20 for Mute/Solo) share one dispatch -
  // see this method's own doc comment (LaunchpadManager.h) for the full
  // reasoning. Off (the default), each launches a whole scene instead -
  // the classic Launchpad right-column convention - reusing this same
  // button's own row position (row = (cc_number - 19) / 10, the identical
  // right-column arithmetic this method's own top comment establishes).
  // On, they're the mixer radio group: Volume/Pan/SendA/SendB enter a
  // fader GridMode (toggleGridMode()), Stop Clip/Mute/Solo open/retarget
  // the track-picker overlay (toggleTrackPicker()) - both already handle
  // their own "only one of the seven active" logic via
  // inSessionMixerFamily(), so this dispatch only needs to pick which of
  // the two mechanisms a given CC number means.
  if (cc_number == 89 || cc_number == 79 || cc_number == 69 || cc_number == 59 ||
      cc_number == 49 || cc_number == 39 || cc_number == 30 || cc_number == 29 || cc_number == 20) {
    if (!deviceState(device_id).session_mixer_mode) {
      triggerSceneRow(controller, (cc_number - 19) / 10);
      return true;
    }
    switch (cc_number) {
    case 89: toggleGridMode(device_id, GridMode::SEND_MAIN); break;
    case 79: toggleGridMode(device_id, GridMode::PAN); break;
    case 69: toggleGridMode(device_id, GridMode::SEND_A); break;
    case 59: toggleGridMode(device_id, GridMode::SEND_B); break;
    case 49: toggleTrackPicker(device_id, DeviceState::TrackPickerPurpose::STOP_CLIP); break;
    case 39: case 30: toggleTrackPicker(device_id, DeviceState::TrackPickerPurpose::MUTE); break;
    case 29: case 20: toggleTrackPicker(device_id, DeviceState::TrackPickerPurpose::SOLO); break;
    }
    return true;
  }
  // 95 ("Session"), 96 ("Note") and 97 ("Custom" - see GridMode::CUSTOM's
  // own comment) are three of a four-member exclusive group with DRAW
  // (CC98, routed directly to handleDrawToggleButton() instead - see its
  // own comment - since it needs both press and release): each press
  // *selects* that mode unconditionally, even if it's already the current
  // one - the only way to ever leave a mode is to select a *different*
  // one of the four. Purely per-device state, like every other toggle
  // here, not tied to whether the overview widget has terminal UI focus at
  // all: one connected Launchpad can sit in Session view while another
  // stays on ordinary note entry. 95 also doubles as the mixer-submode
  // toggle (DeviceState::session_mixer_mode) - a repeat press while
  // already at the plain Session grid with nothing from the mixer radio
  // group active flips it; either way this unconditionally lands on (or
  // stays on) that plain grid, closing any active fader/picker first if
  // there was one - one press to back out of a radio-group selection,
  // a second to then flip the submode itself.
  if (cc_number == 95) {
    auto & state = deviceState(device_id);
    bool at_plain_session_grid = state.grid_mode == GridMode::SESSION && !state.track_picker_active;
    if (at_plain_session_grid) state.session_mixer_mode = !state.session_mixer_mode;
    state.grid_mode = GridMode::SESSION;
    state.track_picker_active = false;
    return true;
  }
  if (cc_number == 96) {
    auto & state = deviceState(device_id);
    state.grid_mode = GridMode::NOTES;
    state.track_picker_active = false; // Session-view-only - see DeviceState::track_picker_active's own comment
    return true;
  }
  if (cc_number == 97) {
    auto & state = deviceState(device_id);
    state.grid_mode = GridMode::CUSTOM;
    state.track_picker_active = false; // Session-view-only - see DeviceState::track_picker_active's own comment
    return true;
  }
  return false;
}

bool
LaunchpadManager::isColumnLiveHeld(int track_id, int note_column) const {
  for (auto & [ device_id, state ] : devices_) {
    for (auto & [ pos, note ] : state.active_notes) {
      if (note.track_id == track_id && note.note_column == note_column) return true;
    }
  }
  return false;
}

void
LaunchpadManager::onRowAdvanced(Controller & controller) {
  if (!auto_started_playback_) return;

  auto & info = controller.getPlaybackInfo();
  auto track_ids = getActiveNoteTrackIds();
  controller.sweepAutoRecordRows(auto_record_cleared_rows_, last_cleared_row_, last_cleared_pattern_idx_, info.getPatternIndex(), info.getRowIndex(), track_ids);
}

vector<int>
LaunchpadManager::getActiveNoteTrackIds() const {
  // Not just the caller's own device - two different Launchpads could be
  // assigned to different tracks and both mid-hold at once.
  vector<int> track_ids;
  for (auto & [ device_id, state ] : devices_) {
    for (auto & [ pos, note ] : state.active_notes) {
      if (find(track_ids.begin(), track_ids.end(), note.track_id) == track_ids.end()) {
	track_ids.push_back(note.track_id);
      }
    }
  }
  return track_ids;
}

bool
LaunchpadManager::handleDrawToggleButton(int device_id, bool is_press) {
  auto & state = deviceState(device_id);
  if (is_press) {
    state.draw_toggle_pressed = true;
    state.draw_toggle_press_time = std::chrono::steady_clock::now();
    // Whether DRAW mode was active *before* this press decides what
    // release does below - captured now since grid_mode is about to
    // change (or not) on this very line.
    state.draw_toggle_was_already_active = (state.grid_mode == GridMode::DRAW);
    // Entering DRAW mode happens immediately on press, matching CC95's
    // own instant Session switch - only clearing the canvas (a long hold,
    // released while already there) waits for release, since a long hold
    // can't be told apart from a fresh entry until then.
    state.grid_mode = GridMode::DRAW;
    state.track_picker_active = false; // Session-view-only - see DeviceState::track_picker_active's own comment
    return true;
  }
  if (!state.draw_toggle_pressed) return true; // stray/duplicate release
  state.draw_toggle_pressed = false;
  if (!state.draw_toggle_was_already_active) return true; // this press is what entered DRAW mode - nothing further to do
  auto held = std::chrono::steady_clock::now() - state.draw_toggle_press_time;
  if (held >= kDrawClearHoldThreshold) {
    // Long hold, released while already in DRAW mode before this press:
    // blank the canvas (DRAW_PALETTE[0] is "off" - see its own definition
    // above).
    state.draw_color_index.fill(0);
  }
  // A quick tap while already in DRAW mode before this press does nothing
  // further - DRAW is part of the same Session/Note/Custom/Draw exclusive
  // group CC95/96/97 are (handleRawButton()'s own comment): the only way
  // to leave it is selecting a different one of the four, never a repeat
  // press of the one already selected.
  return true;
}

bool
LaunchpadManager::isTrackPickerRow(int device_id, int y) const {
  auto * state = findDeviceState(device_id);
  return state && state->track_picker_active && y == LAUNCHPAD_TRACK_PICKER_ROW;
}

void
LaunchpadManager::toggleTrackPicker(int device_id, DeviceState::TrackPickerPurpose purpose) {
  auto & state = deviceState(device_id);
  // Session mixer-submode radio group member (see GridMode's own comment)
  // - a no-op unless already somewhere in that family
  // (inSessionMixerFamily()), so opening from NOTES/CUSTOM/DRAW does
  // nothing, but switching from a fader mode straight into the picker (or
  // between two picker purposes) always works.
  if (!inSessionMixerFamily(state)) return;
  bool already_active = state.track_picker_active && state.track_picker_purpose == purpose;
  state.grid_mode = GridMode::SESSION; // leaving a fader mode for the picker always lands on the plain grid underneath
  state.track_picker_active = !already_active;
  state.track_picker_purpose = purpose; // harmless to set even when closing - only read while track_picker_active
}

void
LaunchpadManager::handleTrackPickerPadEvent(const LaunchpadPadEvent & ev, Controller & controller) {
  if (ev.getKind() != LaunchpadPadEvent::PRESS) return;
  auto & state = deviceState(ev.getDeviceIndex());
  if (ev.getY() != LAUNCHPAD_TRACK_PICKER_ROW) return; // defensive only - the caller (isTrackPickerRow()) never routes any other row here

  auto track_index = ev.getX();
  if (track_index < 0 || track_index >= static_cast<int>(session_.track_ids.size())) return; // no track behind this column
  auto track_id = session_.track_ids[static_cast<size_t>(track_index)];

  switch (state.track_picker_purpose) {
  case DeviceState::TrackPickerPurpose::STOP_CLIP:
    stopSessionTrack(controller, track_id);
    break;
  case DeviceState::TrackPickerPurpose::MUTE:
    controller.toggleTrackMuted(track_id);
    break;
  case DeviceState::TrackPickerPurpose::SOLO:
    controller.toggleTrackSolo(track_id);
    break;
  }
  // Deliberately leaves the overlay open - see this method's own doc
  // comment (LaunchpadManager.h) for why picking stays a repeatable
  // action rather than a one-shot dialog.
}

void
LaunchpadManager::pressDrawPad(int device_id, int x, int y, int velocity) {
  if (x < 0 || x > 7 || y < 0 || y > 7) return;
  auto & state = deviceState(device_id);
  if (state.grid_mode != GridMode::DRAW) return;
  size_t i = static_cast<size_t>(y * 8 + x);
  if (state.draw_pad_held[i]) {
    // A press arriving while this pad is already marked held is a hold
    // continuation, not a fresh touch-down - some Launchpad units resend
    // Note On instead of real Polyphonic Key Pressure while a pad stays
    // down. Route it through the same raise-only rule as real aftertouch,
    // and leave the original press's start time alone so releaseDrawPad()
    // still measures the whole hold, not just the time since this resend.
    updateDrawIntensity(device_id, x, y, velocity);
    return;
  }
  state.draw_pad_held[i] = true;
  state.draw_pad_press_time[i] = std::chrono::steady_clock::now();
  state.draw_intensity[i] = velocity;
  // Hue is deliberately untouched here - see releaseDrawPad(), which makes
  // that decision once it knows how long the pad was held.
}

void
LaunchpadManager::updateDrawIntensity(int device_id, int x, int y, int velocity) {
  if (x < 0 || x > 7 || y < 0 || y > 7) return;
  auto & state = deviceState(device_id);
  if (state.grid_mode != GridMode::DRAW) return;
  auto & intensity = state.draw_intensity[static_cast<size_t>(y * 8 + x)];
  if (velocity > intensity) intensity = velocity;
}

void
LaunchpadManager::releaseDrawPad(int device_id, int x, int y) {
  if (x < 0 || x > 7 || y < 0 || y > 7) return;
  auto & state = deviceState(device_id);
  size_t i = static_cast<size_t>(y * 8 + x);
  if (!state.draw_pad_held[i]) return; // stray/duplicate release
  state.draw_pad_held[i] = false;
  if (state.grid_mode != GridMode::DRAW) return;
  auto held = std::chrono::steady_clock::now() - state.draw_pad_press_time[i];
  if (state.draw_color_index[i] == 0) {
    // Off -> on: lands on the default hue either way, short click or long
    // press - there's nothing lit yet to just brighten, so unlike the
    // already-lit case below there's no short/long branch here.
    state.draw_color_index[i] = 1;
  } else if (held < kDrawPadLongPressThreshold) {
    // Already lit, released quickly: cycle to the next palette hue.
    state.draw_color_index[i] = (state.draw_color_index[i] + 1) % DRAW_PALETTE_SIZE;
  }
  // Already lit, held past the threshold: hue is left exactly as it was -
  // the hold was for adjusting brightness (already live via pressDrawPad/
  // updateDrawIntensity above), not choosing a new color.
}

bool
LaunchpadManager::handleCommand(string_view name, int device_id, int fallback_track_index, int num_tracks, Controller & controller) {
  if (name == "octave-up") {
    octaveUp(device_id);
    return true;
  }
  if (name == "octave-down") {
    octaveDown(device_id);
    return true;
  }
  if (name == "move-row-up" || name == "move-row-down") {
    // Only meaningful in GridMode::SESSION - moves the overview's own
    // section cursor via session_move_section_callback_ (see that member's own
    // comment for why this doesn't scroll a local row window the way the
    // old plain-navigation overview did: Session view's rows are a
    // track's own clips, not sections). Outside SESSION,
    // "move-row-up"/"move-row-down" isn't this class's command at all
    // (PatternEditor's own row navigation owns it, reached via
    // UI::executeCommand()'s fallback, not through here) - declining lets
    // that happen normally.
    if (gridMode(device_id) != GridMode::SESSION) return false;
    if (session_move_section_callback_) session_move_section_callback_(name == "move-row-down" ? 1 : -1);
    return true;
  }
  if (name == "next-track" || name == "prev-track") {
    // Reserved while in Session view (per-device - see handleRawButton()'s
    // own CC95/96 comment): the cursor keys no longer switch this device
    // back to note-entry view, and no longer enter/exit Session view
    // either - CC95/96 are the only way there now, on whichever device
    // that's actually pressed on, independent of every other connected
    // Launchpad.
    if (gridMode(device_id) == GridMode::SESSION) return true;
    if (num_tracks <= 0) return true;

    // Also reserved - repurposed, not just declined - while this device
    // is actually showing a Session-View-focused drum clip's own step
    // grid (Controller's "toggle-record-arm" drum-machine repurposing):
    // paginates through the clip's own steps instead of switching tracks,
    // so the shared cursor stays put and Record Arm alone is the way out
    // of that editing session.
    // Checked directly off getFocusedClipTrackId() itself, not resolved
    // through fallback_track_index first (handlePadEvent()'s own identical
    // pin has the full reasoning) - the shared cursor may well have
    // wandered off to a different track by now, but editing (and so
    // paging through it) has to stay reachable regardless until Record
    // Arm actually closes it.
    if (gridMode(device_id) == GridMode::NOTES && controller.getFocusedClipTrackId() >= 0) {
      auto & song = controller.getSong();
      auto track_id = controller.getFocusedClipTrackId();
      auto length = focusedDrumClipLength(song, track_id, controller.getFocusedClip());
      auto page_count = length > 0 ? (length + 7) / 8 : 1;
      auto & state = deviceState(device_id);
      state.drum_edit_page = std::clamp(state.drum_edit_page + (name == "next-track" ? 1 : -1), 0, page_count - 1);
      return true;
    }

    // Moves the one shared cursor (fallback_track_index), not a
    // per-device assignment of this device's own - see
    // track_move_callback_'s own comment for why every connected
    // Launchpad, not just this one, follows the result.
    if (track_move_callback_) {
      track_move_callback_(LaunchpadLayout::advanceTrackIndex(fallback_track_index, name == "next-track" ? 1 : -1, num_tracks));
    }
    return true;
  }
  return false;
}

int
LaunchpadManager::resolveNote(const Song & song, int device_id, int track_id, int x, int y) const {
  auto track = song.getMasterTrack().getChildByInternalId(track_id);
  auto tuning = track && track->getType() == TrackType::PERCUSSION_CONTROL ? Tuning::PERCUSSION : song.getTuning();

  if (tuning == Tuning::PERCUSSION) {
    return LaunchpadLayout::percussionNoteForPad(x, y);
  }

  auto edo_steps = LaunchpadLayout::edoSteps(tuning);
  if (edo_steps <= 0) return -1; // defensive - every non-percussion Tuning is currently pitched

  auto basis = LaunchpadLayout::computeBasis(edo_steps);
  auto key = song.getKey();
  // song.getKey() is a full note number with its own baked-in octave
  // (Note::stringToKey() defaults to octave 4 whenever the key text omits
  // one, e.g. "C" -> 60) - only its pitch class matters here, since the
  // octave register below is what actually picks the octave. Adding the
  // raw absolute value double-counted the octave (song key "C4" plus the
  // default register 4 landed at base_note ~120, i.e. C9, not the
  // intended ~C4/C5).
  auto tonic = key >= 0 ? ((key % edo_steps) + edo_steps) % edo_steps : 0;
  // Deliberately not "(octave - 4) * edo_steps" (which anchors pad (0,0) at
  // the raw tonic, an inaudibly low register for most instruments): the
  // computer-keyboard tables (InputEvent.h) each bake in their own
  // several-octaves-up baseline for their lowest key (TET12's 'z' is
  // base+48, i.e. exactly 4 octaves; TET31/TET53 use 5 octaves) -
  // multiplying by the octave directly, instead of recentering around 4,
  // reproduces that same baseline; +1 further octave on top of that since
  // the "octave*N" register alone was still too low to be comfortably
  // useful on the Launchpad specifically.
  auto base_note = tonic + (octave(device_id) + 1) * edo_steps;
  return LaunchpadLayout::noteForPad(basis, x - GRID_ORIGIN_X, y - GRID_ORIGIN_Y, base_note);
}

void
LaunchpadManager::handlePadEvent(LaunchpadPadEvent & ev, Controller & controller, int fallback_track_index, int edit_step_size) {
  auto & song = controller.getSong();
  auto & info = controller.getPlaybackInfo();

  auto track_ids = song.getPlayableTrackIds();

  auto device_id = ev.getDeviceIndex();

  // Send A/Send B/Send Main/Pan mode: the whole grid means something else
  // entirely while active (see LaunchpadManager::GridMode) - column x is
  // track_ids[x] (the first 8 playable tracks, not this device's assigned
  // track), row y sets that track's send level or azimuth. Only a PRESS
  // does anything; RELEASE/AFTERTOUCH are swallowed too, never falling
  // through to note-entry below. CUSTOM is excluded here (unlike SESSION/
  // DRAW, which never reach this function at all - see UI::
  // handleLaunchpadPadEvent) since it addresses "this device's assigned
  // track" the same way NOTES does, not a fixed column-per-track layout.
  auto grid_mode = gridMode(device_id);
  if (grid_mode != GridMode::NOTES && grid_mode != GridMode::CUSTOM) {
    if (ev.getKind() == LaunchpadPadEvent::PRESS && ev.getX() < 8) {
      // The first 8 columns must always be usable, even in a song that
      // doesn't have that many tracks yet - a Launchpad's physical layout
      // doesn't know or care how many tracks currently exist, so auto-create
      // plain InstrumentTracks (the same default 't' key/add-track uses) up
      // to the pressed column rather than silently doing nothing.
      while (static_cast<int>(track_ids.size()) <= ev.getX()) {
        song.addTrack(make_unique<InstrumentTrack>(0));
        track_ids = song.getPlayableTrackIds();
      }
      // No track-type check needed - track_ids is already
      // getPlayableTrackIds()'s own "every LeafTrack" list (its own doc
      // comment), and Controller::setTrackSendA()/setTrackSendB()/
      // setTrackSendMain()/setTrackAzimuth() are themselves generic over
      // LeafTrack (asLeafTrack()), not restricted to InstrumentTrack - a
      // stale, narrower whitelist here once silently excluded SampleTrack
      // from all four.
      auto track_id = track_ids[static_cast<size_t>(ev.getX())];
      if (grid_mode == GridMode::SEND_A) {
        controller.setTrackSendA(track_id, sendRowToDb(ev.getY()));
      } else if (grid_mode == GridMode::SEND_B) {
        controller.setTrackSendB(track_id, sendRowToDb(ev.getY()));
      } else if (grid_mode == GridMode::SEND_MAIN) {
        controller.setTrackSendMain(track_id, sendRowToDb(ev.getY()));
      } else { // PAN
        controller.setTrackAzimuth(track_id, rowToAzimuth(ev.getY()));
      }
    }
    return;
  }

  // Mirrors the Send/Pan-mode auto-create above: the shared cursor
  // (fallback_track_index - every device follows it, there's no more
  // per-device assignment of its own) may point past however many tracks
  // currently exist in a brand-new/emptied song, so grow the song up to
  // that index rather than silently doing nothing.
  auto track_index = fallback_track_index;
  while (static_cast<int>(track_ids.size()) <= track_index) {
    song.addTrack(make_unique<InstrumentTrack>(0));
    track_ids = song.getPlayableTrackIds();
  }
  int track_id = track_ids[static_cast<size_t>(track_index)];
  // A drum clip open for editing (Controller::getFocusedClipTrackId(),
  // Record Arm's own repurposing) pins every connected NOTES-grid device
  // to it, regardless of wherever the shared cursor itself has since
  // wandered off to elsewhere in the terminal (Session view's own column,
  // PatternEditor, ...) - editing stays open until Record Arm explicitly
  // closes it, never merely by looking at a different track meanwhile.
  if (controller.getFocusedClipTrackId() >= 0) track_id = controller.getFocusedClipTrackId();

  // CUSTOM: "customize the assigned track" - today, only meaningful for a
  // PercussionTrack (the lane picker, lane-count-agnostic - this is how a
  // lane-less track gains its first lane); no-op for anything else (see
  // GridMode::CUSTOM's own comment for what this could grow into).
  if (grid_mode == GridMode::CUSTOM) {
    auto assigned_track = song.getMasterTrack().getChildByInternalId(track_id);
    if (assigned_track && assigned_track->getType() == TrackType::PERCUSSION_CONTROL) {
      handleDrumPickerPadEvent(ev, controller, static_cast<PercussionTrack &>(*assigned_track));
    }
    return;
  }

  // Step grid: a step-sequenced PercussionTrack's grid means something else
  // entirely from ordinary chord entry, the same way Send/Pan mode
  // already short-circuits above. A lane-less PercussionTrack falls
  // through to ordinary note entry below instead.
  {
    auto assigned_track = song.getMasterTrack().getChildByInternalId(track_id);
    auto percussion_track = assigned_track && assigned_track->getType() == TrackType::PERCUSSION_CONTROL
      ? &static_cast<PercussionTrack &>(*assigned_track) : nullptr;
    if (percussion_track && percussion_track->isStepSequenced()) {
      handleStepGridPadEvent(ev, controller, *percussion_track, track_id);
      return;
    }
  }

  auto note_value = resolveNote(song, device_id, track_id, ev.getX(), ev.getY());
  if (note_value < 0) return; // unused percussion pad (row 7), or an unpitched/degenerate tuning

  // Pad-press note entry writes - see Song::getOrCreateSection()'s own
  // comment (Song.h) on why that's the one to use here, not plain
  // getSection(): the edit position can legitimately be past the last real
  // Section (PatternEditor's own row navigation already tolerates that), and
  // getSection() would silently write into a shared, process-wide sentinel
  // instead of real song content in that case.
  auto & section = song.getOrCreateSection(info.getPatternIndex());
  auto current_delay = info.getCurrentDelay();
  auto & event_queue = controller.getPlaybackEventQueue();

  if (ev.getKind() == LaunchpadPadEvent::PRESS) {
    // A Session View take targeting this exact track writes into that
    // take's own clip directly, indexed by the free-running audition
    // clock (this device's NOTE grid is the only way a Session View take
    // ever receives notes at all), never the global transport position - a
    // Session View take runs with the transport stopped by design.
    // ensureSessionRecordingClip() itself owns turning an absolute clock
    // step into a row relative to this take's own row 0 (established from
    // whichever step happens to be this take's *first* one - see its own
    // comment), so row 0 always means "the start of the bar this take
    // began in", not the instant of this specific press.
    bool session_recording_here = controller.isSessionRecording() && controller.getSessionRecordingTrackId() == track_id;
    // The new take hasn't actually started yet - the old clip still
    // playing on this exact track is what's still audible, right up to
    // session_recording_quantize_until_step_'s own shared boundary (see
    // its own comment) - so this press is dropped outright rather than
    // recorded early or previewed live alongside the old clip's own audio.
    if (session_recording_here && session_recording_quantize_until_step_ >= 0 &&
        audition_clock_.currentStep() < session_recording_quantize_until_step_) return;
    auto row = info.getRowIndex();
    if (session_recording_here) {
      // Quantizes a human performer's own real-time press to whichever step
      // it's actually closer to, rather than always flooring to the one
      // that just started (audition_clock_.currentStep()'s own contract) -
      // a press landing just after a step boundary is far more likely a
      // slightly-early attempt at the *next* beat than a slightly-late one
      // for the step that just began. Row-only, deliberately: a raw sub-row
      // offset (the same idea Note's own delay column captures for the
      // transport-driven path, via info.getCurrentDelay() - meaningless
      // here, since the transport itself never advances during a Session
      // View take) would just re-encode the human's own imprecise timing
      // instead of cleaning it up - snapping fully to the row grid is the
      // whole point of quantizing a live take at all.
      auto absolute_step = audition_clock_.currentStep();
      auto tempo = song.getTempo();
      float row_duration = tempo > 0 ? 60.0f / 4.0f / static_cast<float>(tempo) : 0.0f;
      if (row_duration > 0.0f && audition_clock_.phase() / row_duration >= 0.5f) absolute_step++;
      // Aligns this take's own origin (established on the first call, see
      // ensureSessionRecordingClip()'s own comment) to whatever else is
      // already looping in the session, if anything is - session_origin_step_
      // is the exact same shared bar-boundary reference triggerClipStep()
      // itself already measures every other track's own Session View clip
      // against.
      row = controller.ensureSessionRecordingClip(absolute_step, session_origin_set_ ? session_origin_step_ : -1);
    }
    auto & state = deviceState(device_id);

    Pattern * session_pattern = nullptr;
    int session_row = 0;
    if (session_recording_here) {
      if (row < 0) return; // nothing actually armed for this track (shouldn't normally happen)
      auto & clips = song.getClips(track_id);
      auto clip_index = controller.getSessionRecordingClipIndex();
      if (clip_index >= 0 && clip_index < static_cast<int>(clips.size())) {
        auto & clip = clips[static_cast<size_t>(clip_index)];
        session_pattern = &clip.getLeafPattern();
        session_row = row % std::max(1, clip.getLength());
      }
    }
    // Writes into whatever's actually active at this row - an active clip
    // instance's own (live-linked) Pattern, or this track's own
    // background Pattern otherwise (ArrangementOps.h's own
    // resolveEditTarget()), at that Pattern's own resolved row; `row`
    // itself stays raw for recordActiveNote()'s own held-note bookkeeping
    // (a same-row-or-not comparison against a later release, unaffected
    // by any remap).
    auto edit_target = session_pattern ? EditTarget{ session_pattern, session_row }
      : resolveEditTarget(song, section, track_id, row, controller.getFocusedClip());
    if (session_recording_here && !session_pattern) return; // nothing valid to write into (shouldn't normally happen)

    // The transport itself is already running by the time any press can
    // reach here - Record Arm starts it immediately on arming
    // (refresh()'s own note-capture-armed rising-edge handling) - except
    // for a Session View take, which never starts it at all.

    // A live take writes into a real, individually-manageable Clip
    // instance, not directly into the section's own background Pattern - a
    // no-op once that clip already exists (or if a clip is focused, which
    // already resolves correctly without this). Re-resolves edit_target
    // immediately after: it was computed before this take could have just
    // placed a brand new instance here, so it would otherwise still point
    // at the (now superseded) background - the free-slot search and this
    // press's own write below both need the fresh one.
    if (!session_recording_here && state.capture_enabled && info.isPlaying()) {
      controller.ensureNoteRecordingClip(auto_record_clip_ids_, track_id, info.getPatternIndex(), row);
      edit_target = resolveEditTarget(song, section, track_id, row, controller.getFocusedClip());
    }

    // Whole-row replace semantics for a live take: idempotent (see its
    // own comment), so calling it defensively is safe - only actually
    // does anything the first time (row, track_id) is touched this
    // session. Cleared *before* the free-slot search just below, not
    // after - otherwise a column still holding an old, about-to-be-
    // erased note reads as "taken" and gets skipped past, when the old
    // note is actually gone (or about to be, from this same call) and
    // the new one should be free to land in the very first column.
    if (state.capture_enabled && auto_started_playback_) controller.ensureRowCleared(auto_record_cleared_rows_, info.getPatternIndex(), row, track_id);

    // Free-slot search (mirrors Section::pushNote), deliberately not
    // "map size" the way active_midi_notes assigns columns - that has a
    // latent collision bug on non-LIFO release order, which is the common
    // case for a chordally-played grid controller (see the plan's design
    // decision 3). Computed unconditionally (even with Capture off,
    // nothing gets written to it) - simpler than a second code path, and
    // it's still needed to key the live-audition voice below. A column is
    // "taken" if the pattern already has a real note there *or* some
    // other currently-held press already claimed it live
    // (isColumnLiveHeld) - the latter is essential with Capture off: a
    // held note is never written to the pattern at all then, so the
    // pattern-only check alone would hand out the very same "free"
    // column to every simultaneously-held note, each PLAY_NOTE silently
    // stealing the previous one's voice (Player.cpp's
    // stopVoices(column)) and killing polyphony entirely.
    auto & notes = edit_target.pattern->getNotes(edit_target.effective_row);
    int note_column = 0;
    while ((note_column < static_cast<int>(notes.size()) && notes[static_cast<size_t>(note_column)].isDefined()) ||
	   isColumnLiveHeld(track_id, note_column)) {
      note_column++;
    }

    auto velocity = LaunchpadProtocol::getModelInfo(ev.getModel()).velocity_sensitive ?
      static_cast<short>(ev.getVelocity()) : static_cast<short>(0x28); // same default as keyboard entry

    recordActiveNote(device_id, ev.getX(), ev.getY(), {note_column, row, track_id});

    if (state.capture_enabled) {
      // 0, not current_delay, for a Session View take - current_delay
      // reads the global transport's own delay tracking, meaningless
      // while it never advances during one; the row-rounding above already
      // is this take's own quantization, so its notes always land exactly
      // on a row with no further sub-row offset to record.
      Note note(note_value, velocity, session_recording_here ? 0 : current_delay);
      edit_target.pattern->setNote(edit_target.effective_row, note_column, note);
      song.incVersion();
    }

    event_queue.push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::PLAY_NOTE, controller.getActiveBufferName(), track_id, note_column, note_value, velocity));

    // Deliberately NOT auto-advancing here (unlike single-note keyboard
    // entry): a chord is multiple near-simultaneous presses that must all
    // land on the *same* row - advancing per-press would spread a chord
    // across rows the moment any two presses straddle the (asynchronous)
    // MOVE_POSITION round-trip. Advance is deferred to RELEASE, once every
    // currently-held pad has been let go (see below) - treating
    // simultaneously-pressed MIDI notes as one gesture, not N independent
    // steps. (With Capture armed and
    // the transport now running via the auto-play push above, rows in
    // fact advance continuously in real time for the whole hold, same as
    // real playback - this per-press deferral only still matters for the
    // Capture-off/pure-audition case, which never engages auto-play.)
  } else if (ev.getKind() == LaunchpadPadEvent::RELEASE) {
    auto held_ptr = findActiveNote(device_id, ev.getX(), ev.getY());
    if (!held_ptr) return;
    auto held = *held_ptr;
    clearActiveNote(device_id, ev.getX(), ev.getY());
    auto & state = deviceState(device_id);

    // Always silence the live-audition voice.
    event_queue.push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::STOP_NOTE, controller.getActiveBufferName(), held.track_id, held.note_column));

    // A Session View take never reaches info.isPlaying() below - it runs
    // with the transport stopped by design - so it needs its own release-
    // off write, keyed off the same audition-clock step (rounded the same
    // way the PRESS branch above rounds it) rather than the transport's
    // own row.
    bool session_recording_here = controller.isSessionRecording() && controller.getSessionRecordingTrackId() == held.track_id;
    if (session_recording_here) {
      if (state.capture_enabled) {
        auto absolute_step = audition_clock_.currentStep();
        auto tempo = song.getTempo();
        float row_duration = tempo > 0 ? 60.0f / 4.0f / static_cast<float>(tempo) : 0.0f;
        if (row_duration > 0.0f && audition_clock_.phase() / row_duration >= 0.5f) absolute_step++;
        // grid_origin_step only actually matters on this take's own first
        // ever call (see ensureSessionRecordingClip()'s own comment) -
        // already established by the corresponding PRESS by the time any
        // RELEASE reaches here, but passed the same way regardless for
        // consistency.
        auto release_row = controller.ensureSessionRecordingClip(absolute_step, session_origin_set_ ? session_origin_step_ : -1);
        auto & clips = song.getClips(held.track_id);
        auto clip_index = controller.getSessionRecordingClipIndex();
        // Same "not the row the note itself is on" rule as the ordinary
        // performance-recording branch below - a single Pattern row can't
        // hold both a note and its own off.
        if (release_row >= 0 && release_row != held.row &&
            clip_index >= 0 && clip_index < static_cast<int>(clips.size())) {
          auto & clip = clips[static_cast<size_t>(clip_index)];
          clip.getLeafPattern().setNote(release_row % std::max(1, clip.getLength()), held.note_column, Note(0, 0, 0));
          song.incVersion();
        }
      }
    } else if (state.capture_enabled) {
      if (info.isPlaying()) {
	// Live performance recording: write an explicit OFF at the row the
	// transport has since reached, mirroring handleMidiEvent's NOTE_OFF -
	// UNLESS that's still the same row the note itself is on. In this
	// tracker's own pattern model (a single line can't hold both a note
	// and its own note-off), a release fast enough to land before the
	// row has advanced must not be recorded as an off, or it would
	// instantly erase the note it belongs to.
	auto release_row = info.getRowIndex();
	if (release_row != held.row) {
	  controller.writeReleaseOff(auto_record_cleared_rows_, auto_started_playback_, info.getPatternIndex(), release_row, held.track_id, held.note_column, current_delay);
	}
      } else if (!hasAnyActiveNotes(device_id)) {
	// Step entry: advance once the whole chord gesture has been
	// released on *this* device (not per pad - see the PRESS branch;
	// and scoped to this device, not every connected Launchpad, so one
	// device's chord release doesn't prematurely advance while another
	// device is still mid-chord), so the next tap/chord lands on a
	// fresh row instead of piling onto this one. Only reachable at all
	// with Capture off (real playback, engaged by the PRESS branch's
	// auto-play push, is the norm whenever Capture is on) - a stopped,
	// pure-audition release must not touch the cursor either.
	controller.moveEditPosition(edit_step_size);
      }
    }

    // The transport itself is no longer stopped here - releasing the
    // last held note used to end the whole recording session, which made
    // recording a real phrase (anything with a rest in it) impossible:
    // the transport froze the instant nothing was held, so the next note
    // played after a gap landed right next to the previous one instead of
    // where the gap actually put it. Now only CC19 (disarming Record Arm,
    // see handleRawButton()'s own comment) stops it - a held note's own
    // release still silences that one note (above) and, while stopped,
    // still advances step entry (above), just never touches the
    // transport itself any more.
  } else if (ev.getKind() == LaunchpadPadEvent::AFTERTOUCH) {
    // Mini MK3 never emits this (no pressure sensing); defensive check
    // anyway in case a future model reports itself incorrectly.
    if (!LaunchpadProtocol::getModelInfo(ev.getModel()).poly_aftertouch) return;

    auto held_ptr = findActiveNote(device_id, ev.getX(), ev.getY());
    if (!held_ptr) return; // no held note to modulate
    auto & held = *held_ptr;

    // Live modulation always happens, regardless of Capture/write-
    // throttle below - mirrors handleMidiEvent's NOTE_PRESSURE handling
    // exactly.
    event_queue.push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::NOTE_PRESSURE, controller.getActiveBufferName(), held.track_id, held.note_column, note_value, ev.getVelocity()));

    if (!deviceState(device_id).capture_enabled) return;

    // Rate-limit the persisted pattern write: Pattern::setNote already
    // overwrites in place (so "one aftertouch object per column per row" is
    // free), this threshold purely avoids redundant work/redraw churn for
    // a dense pressure stream, not a correctness requirement.
    const int aftertouch_threshold = 4;
    auto delta = ev.getVelocity() - held.last_aftertouch_value;
    if (delta < 0) delta = -delta;
    if (delta < aftertouch_threshold) return;
    held.last_aftertouch_value = ev.getVelocity();

    // While playing (the norm whenever Capture is on - see the PRESS
    // branch's auto-play push), modulate the currently-sounding row
    // (transport has moved on, matching handleMidiEvent); while stopped
    // (only reachable with Capture on if the auto-play push hasn't been
    // processed by the Player thread yet), modulate the row the note
    // actually landed on.
    auto target_row = info.isPlaying() ? info.getRowIndex() : held.row;
    // Clear before reading, not just before writing - otherwise the
    // isDefined() check below could pick up stale pre-existing data from
    // before this row was cleared for the live take.
    if (auto_started_playback_) controller.ensureRowCleared(auto_record_cleared_rows_, info.getPatternIndex(), target_row, held.track_id);
    controller.applyNotePressure(info.getPatternIndex(), target_row, held.track_id, held.note_column, static_cast<short>(ev.getVelocity()), current_delay);
    song.incVersion();
  }
}

void
LaunchpadManager::handleSessionPadEvent(const LaunchpadPadEvent & ev, Controller & controller) {
  if (ev.getKind() != LaunchpadPadEvent::PRESS) return;

  // No column scroll yet (see SessionWindow's own comment).
  auto track_index = ev.getX();
  if (track_index < 0 || track_index >= static_cast<int>(session_.track_ids.size())) return;
  auto track_id = session_.track_ids[static_cast<size_t>(track_index)];

  // Same y-flip as refresh()'s own session_colors computation - y=0 is
  // the bottom-left pad, so y=7 is that track's first clip.
  triggerSessionClip(controller, track_id, 7 - ev.getY());
}

void
LaunchpadManager::stopSessionTrack(Controller & controller, int track_id) {
  // Genuinely two different mechanisms depending on Record Arm: while
  // recording, a stop has to become real song data (placeRecordingStop(),
  // the same thing an empty-row press in triggerSessionClip()'s own assign
  // branch does) since real playback never reads this class's own
  // triggered_pattern_by_track_/queued_pattern_by_track_ bookkeeping in the
  // first place - that's audition-only state.
  if (controller.isNoteCaptureArmed()) {
    placeRecordingStop(controller, track_id);
    return;
  }
  // Auditioning: same quantized stop every other stop path here uses if
  // something's actually triggered; a not-yet-started pending join is
  // simply cancelled outright instead (same "nothing playing yet to
  // release" reasoning placeRecordingStop() has for the recording case); a
  // total no-op if the track isn't doing anything at all.
  if (triggered_pattern_by_track_.find(track_id) != triggered_pattern_by_track_.end()) {
    queued_pattern_by_track_[track_id] = -1;
  } else {
    queued_pattern_by_track_.erase(track_id);
  }
}

void
LaunchpadManager::triggerSessionClip(Controller & controller, int track_id, int clip_index) {
  auto & song = controller.getSong();
  auto & clips = song.getClips(track_id);
  bool has_pattern_here = clip_index >= 0 && clip_index < static_cast<int>(clips.size());

  if (!controller.isNoteCaptureArmed()) {
    // Auditioning (Record Arm off) - touches no song state, only this
    // class's own triggered_pattern_by_track_/queued_pattern_by_track_.
    auto triggered_it = triggered_pattern_by_track_.find(track_id);
    if (!has_pattern_here) {
      // An unassigned row cancels/stops whatever this track is doing: a
      // not-yet-started pending join is simply erased outright (nothing
      // is playing yet to release), while something already triggered
      // gets a queued stop instead - quantized the same as everything
      // else here (see triggerClipStep()'s own comment for
      // exactly when it takes effect). A no-op if the track isn't doing
      // anything at all.
      if (triggered_it != triggered_pattern_by_track_.end()) queued_pattern_by_track_[track_id] = -1;
      else queued_pattern_by_track_.erase(track_id);
      return;
    }
    if (triggered_it != triggered_pattern_by_track_.end() && triggered_it->second.clip_index == clip_index) {
      // Pressing the already-triggered pattern again queues a stop - the
      // exact same quantized handling as pressing an empty row above, not
      // an immediate cut - this is also the only way to stop a track
      // whose clip list fills every row (no empty one to press).
      queued_pattern_by_track_[track_id] = -1;
      return;
    }
    // Either nothing is triggered on this track yet, or something else
    // is (a swap) - both are the same "pending join" case now, with one
    // exception: the very first pattern to play anywhere in an otherwise
    // silent session launches immediately rather than queuing, since
    // there's nothing yet to quantize against - and that exact moment
    // becomes the shared origin (session_origin_step_) every later
    // launch/swap/stop, on any track, is measured against (see
    // triggerClipStep()'s own comment). Once anything anywhere
    // is active, every further join/swap queues instead, uniformly,
    // regardless of whether this specific track already had something
    // playing.
    if (triggered_pattern_by_track_.empty() && queued_pattern_by_track_.empty()) {
      // launch_step pins the clock's current step as this instance's own
      // zero point, so it always starts at its own row 0 (relative step
      // 0) rather than wherever the shared clock's own phase happens to
      // be right now. While the clock isn't currently running (e.g. the
      // transport is playing even though Record Arm is off), currentStep()
      // can be a stale leftover from a previous run rather than 0 -
      // stop() never resets it, only start() does (see StepClock's own
      // contract) - so pin 0 directly instead, anticipating the step
      // start() itself will actually (re)fire from whenever this track's
      // pattern next ticks.
      auto launch_step = audition_clock_.isRunning() ? audition_clock_.currentStep() : 0;
      triggered_pattern_by_track_[track_id] = {clip_index, launch_step};
      session_origin_step_ = launch_step;
      session_origin_set_ = true;
      if (audition_clock_.isRunning()) {
        auto & launched_clip = clips[static_cast<size_t>(clip_index)];
        auto launched_length = launched_clip.getLength() > 0 ? launched_clip.getLength() : 1;
        fireOrTriggerClipStep(song, controller, track_id, clip_index, launched_clip, launched_length, 0);
      }
    } else {
      queued_pattern_by_track_[track_id] = clip_index;
    }
    return;
  }

  // Assigning (Record Arm on): places a real instance event - the
  // arrangement layer's own start-only, tracker-idiom placement
  // (ArrangementOps.h's placeClipInstance(), the same one real playback
  // resolves), not a live reference back to the clip. Deliberately stays
  // in Session view rather than switching focus away - a player assigning
  // several patterns in a row needs to keep pressing pads, not get
  // bounced out after the first one. An empty row means "stop this
  // track" instead, same as auditioning's own empty-row press - but has
  // to write it (placeRecordingStop()), not just adjust bookkeeping, the
  // same reasoning CC49-while-recording above has.
  if (!has_pattern_here) {
    placeRecordingStop(controller, track_id);
    return;
  }
  auto & playback_info = controller.getPlaybackInfo();
  // Recording an arrangement means the playhead actually has to advance -
  // a clip assigned into an otherwise-stopped section would just sit at row
  // 0 forever, never becoming "a whole section" the way triggering further
  // clips as playback continues is supposed to build up. Starts the
  // transport on this first assign press (not at Record Arm time - unlike
  // ordinary note capture, an assign has no "clip creation" step to defer
  // separately, so there's nothing to gain from starting any earlier),
  // using the same auto_started_playback_ bookkeeping, so
  // Controller::extendRecordingSectionIfNeeded() (gated on isAutoRecording())
  // also keeps growing the section as the performance continues - but
  // Controller::startAutoRecordPlayback(), not startAutoRecordSession():
  // the latter also mutes the song's own pattern-driven scheduling, correct
  // for a held note (heard through its own separate live PLAY_NOTE stream
  // while old content stays silent) but wrong here - a triggered clip has
  // no such separate path, it's heard entirely through that same
  // scheduling the instant placeClipInstance() places it below, so muting
  // it would silence the very clip being recorded. Calls togglePlaying()
  // synchronously, so playback_info (bound by reference above) already
  // reflects isPlaying()==true by the time section_idx/row are computed
  // just below.
  if (!playback_info.isPlaying()) {
    controller.startAutoRecordPlayback(auto_started_playback_);
  }
  // Targets whichever section is actually *playing* right now, not
  // necessarily the column the cursor happens to be pointing at - true
  // live-recording, matching a real note-on's own timing, and (per
  // Controller::extendRecordingSectionIfNeeded()) that section keeps growing
  // to fit as the performance continues rather than being confined to a
  // fixed pre-existing length. Falls back to the cursor's own section, row
  // 0 (a whole-section placement, closest to what plain section-navigation
  // used to write here before the instance layer existed) on the off
  // chance playback still isn't running (e.g. a non-positive tempo).
  auto section_idx = playback_info.isPlaying() ? playback_info.getPatternIndex() : session_.cursor_section_idx;
  auto & section = song.getOrCreateSection(section_idx);
  // Bar-aligned (quantizedBarRow()'s own comment has the full reasoning:
  // plans/arrangement-view.md's "Bar alignment" rule, snapped forward not
  // back) - placeClipInstance() below just writes a start event at this
  // (possibly future) row, and ordinary playback naturally begins
  // triggering it once the playhead actually arrives there, no extra
  // queuing needed.
  auto raw_row = playback_info.isPlaying() ? playback_info.getRowIndex() : 0;
  auto row = quantizedBarRow(raw_row, song.getRowsPerBar());
  placeClipInstance(song, section, track_id, row, clip_index);
  song.incVersion();
}

void
LaunchpadManager::triggerSceneRow(Controller & controller, int row) {
  // Same y-flip Session view's own columns use (triggerSessionClip()'s
  // own caller in handleSessionPadEvent()) - row 0 (bottom) is clip index
  // 7, row 7 (top) is clip index 0.
  auto clip_index = 7 - row;
  for (auto track_id : session_.track_ids) triggerSessionClip(controller, track_id, clip_index);
}

void
LaunchpadManager::placeRecordingStop(Controller & controller, int track_id) {
  // A no-op while nothing is actually playing yet - there's no live
  // position to stop against, the same "nothing playing yet to release"
  // reasoning the audition-only path already has for an empty-row/CC49
  // press before anything's been triggered.
  auto & playback_info = controller.getPlaybackInfo();
  if (!playback_info.isPlaying()) return;
  auto & song = controller.getSong();
  auto row = quantizedBarRow(playback_info.getRowIndex(), song.getRowsPerBar());
  auto & section = song.getOrCreateSection(playback_info.getPatternIndex());
  placeStopInstance(section, track_id, row);
  song.incVersion();
}

void
LaunchpadManager::triggerClipStep(const Song & song, Controller & controller, int step) {
  // Every track with either something already triggered or something
  // pending needs evaluating this step - a pending join queued for a
  // track with nothing playing yet (queued_pattern_by_track_ only, no
  // triggered_pattern_by_track_ entry - see handleSessionPadEvent()'s own
  // comment) is exactly as live as a pending swap/stop for one that's
  // already triggered, so this walks their union rather than just
  // triggered_pattern_by_track_ alone. Collected into a plain snapshot
  // first since the loop body below mutates both maps.
  vector<int> track_ids;
  for (auto & [ track_id, unused ] : triggered_pattern_by_track_) track_ids.push_back(track_id);
  for (auto & [ track_id, unused ] : queued_pattern_by_track_) {
    if (find(track_ids.begin(), track_ids.end(), track_id) == track_ids.end()) track_ids.push_back(track_id);
  }

  auto rows_per_bar = song.getRowsPerBar();
  if (rows_per_bar <= 0) rows_per_bar = 1;

  for (auto track_id : track_ids) {
    auto & clips = song.getClips(track_id);
    auto triggered_it = triggered_pattern_by_track_.find(track_id);
    if (triggered_it != triggered_pattern_by_track_.end() &&
        (triggered_it->second.clip_index < 0 || triggered_it->second.clip_index >= static_cast<int>(clips.size()))) {
      // The clip list shrank (or the track's gone) out from under an already-
      // triggered index - drop it rather than read out of bounds; still
      // fall through below to check for a pending queued action.
      triggered_pattern_by_track_.erase(triggered_it);
      triggered_it = triggered_pattern_by_track_.end();
    }

    // Quantized launch/swap/stop: a queued action only takes effect once
    // the shared-grid boundary arrives - (step - session_origin_step_) %
    // rows_per_bar == 0 - the same boundary every track shares, never the
    // currently-playing pattern's own length (that only decides where
    // *it* loops, not when a pending change is allowed to interrupt it)
    // and never immediate. A fresh join for a not-yet-triggered track
    // waits for the exact same boundary, uniformly with a swap/stop - see
    // handleSessionPadEvent()'s own comment for why both are queued
    // identically.
    auto queued_it = queued_pattern_by_track_.find(track_id);
    if (queued_it != queued_pattern_by_track_.end() && session_origin_set_) {
      if ((step - session_origin_step_) % rows_per_bar == 0) {
        auto queued_index = queued_it->second;
        queued_pattern_by_track_.erase(queued_it);
        if (queued_index < 0) {
          // A real stop, not a swap to another pattern - release whatever's
          // still sounding on this track (its natural stopNote() tail, not
          // a hard cut - see InstrumentTrackState::stopAllVoices()) rather
          // than leaving a sustained note ringing with nothing left driving
          // it forward. Guarded on triggered_it still being valid purely
          // defensively (a queued stop is only ever set for an
          // already-triggered track, but the pool-shrink check above could
          // have just erased it out from under this exact step).
          if (triggered_it != triggered_pattern_by_track_.end()) {
            controller.getPlaybackEventQueue().push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::STOP_ALL_NOTES, controller.getActiveBufferName(), track_id));
            triggered_pattern_by_track_.erase(triggered_it);
          }
          triggered_it = triggered_pattern_by_track_.end();
        } else {
          // A fresh join or a swap - either way this instance starts
          // playing right now, at its own row 0 (launch_step = step).
          triggered_it = triggered_pattern_by_track_.insert_or_assign(track_id, TriggeredPattern{queued_index, step}).first;
        }
      }
    }

    if (triggered_it == triggered_pattern_by_track_.end()) continue;
    auto clip_index = triggered_it->second.clip_index;
    if (clip_index < 0 || clip_index >= static_cast<int>(clips.size())) continue;
    auto & clip = clips[static_cast<size_t>(clip_index)];
    auto relative_step = step - triggered_it->second.launch_step;
    auto length = clip.getLength() > 0 ? clip.getLength() : 1;
    if (!clip.isLooping() && relative_step >= length) {
      // A one-shot clip has played through its own length once - release
      // its voices and stop, rather than wrapping back to row 0 (matching
      // a real DAW's own non-looping clip - see Clip::isLooping()'s own
      // comment).
      controller.getPlaybackEventQueue().push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::STOP_ALL_NOTES, controller.getActiveBufferName(), track_id));
      triggered_pattern_by_track_.erase(track_id);
      continue;
    }
    fireOrTriggerClipStep(song, controller, track_id, clip_index, clip, length, relative_step);
  }

  // Once nothing anywhere is triggered or pending, "beat 1" no longer
  // means anything - clear it so the next launch from silence is free to
  // redefine it fresh rather than snapping to a stale reference (see
  // session_origin_step_'s own comment).
  if (triggered_pattern_by_track_.empty() && queued_pattern_by_track_.empty()) session_origin_set_ = false;
}

void
LaunchpadManager::handleStepGridPadEvent(LaunchpadPadEvent & ev, Controller & controller, PercussionTrack & track, int track_id) {
  auto & lane_notes = track.getLaneNotes();
  auto x = ev.getX(), y = ev.getY();
  if (y < 0 || y >= static_cast<int>(lane_notes.size()) || x < 0 || x >= 8) return;
  int note = lane_notes[static_cast<size_t>(y)];

  auto & event_queue = controller.getPlaybackEventQueue();

  if (ev.getKind() == LaunchpadPadEvent::PRESS) {
    // Writes unconditionally, regardless of capture_enabled - "the arm
    // flag gates performance capture, not editing": the step grid writes
    // in both arm states, only free playing is gated. A step is an
    // ordinary Note in this section's own Pattern for this track now (see
    // PercussionTrack.h's own "A step is a Note" comment) - "was_hit" is
    // decided by value, not by a fixed column, so this stays consistent
    // with getHitNotesForRow()'s own by-value identification even if the
    // note isn't sitting at this lane's usual column (e.g. typed directly
    // in PatternEditor, or pasted from elsewhere); a *new* hit still lands
    // at this lane's own column (y), the step grid's own convention for
    // keeping a lane's column stable across rows.
    auto & song = controller.getSong();
    auto & info = controller.getPlaybackInfo();
    auto & section = song.getOrCreateSection(info.getPatternIndex());
    // Writes into whatever's actually active at this row - an active clip
    // instance's own (live-linked) Pattern, or this track's own
    // background Pattern otherwise (ArrangementOps.h's own
    // resolveEditTarget()). x alone addresses rows 0-7 directly (no
    // scrolling window) for ordinary background-pattern editing, same as
    // the ordinary NOTES-mode pad press just above - but a Session-View-
    // focused clip (Controller::getFocusedClipTrackId() == track_id) can
    // be longer than the grid's fixed 8 columns, so this device's own
    // current page (DeviceState::drum_edit_page, paginated via the
    // prev-track/next-track buttons - see handleCommand()'s own comment)
    // picks which 8-row window of it x actually lands in.
    auto row = x;
    if (controller.getFocusedClipTrackId() == track_id) {
      auto length = focusedDrumClipLength(song, track_id, controller.getFocusedClip());
      auto page_count = length > 0 ? (length + 7) / 8 : 1;
      auto page = std::clamp(deviceState(ev.getDeviceIndex()).drum_edit_page, 0, page_count - 1);
      row = page * 8 + x;
    }
    auto edit_target = resolveEditTarget(song, section, track_id, row, controller.getFocusedClip());
    auto & row_notes = edit_target.pattern->getNotes(edit_target.effective_row);
    int existing_column = -1;
    for (size_t i = 0; i < row_notes.size(); i++) {
      auto & n = row_notes[i];
      if (n.isDefined() && !n.isOff() && !n.isAftertouch() && n.getValue() == note) { existing_column = static_cast<int>(i); break; }
    }
    bool was_hit = existing_column >= 0;
    if (was_hit) edit_target.pattern->deleteNote(edit_target.effective_row, existing_column);
    else edit_target.pattern->setNote(edit_target.effective_row, y, Note(note, static_cast<short>(constants::DEFAULT_VELOCITY)));
    song.incVersion();

    // Auditions at a fixed velocity - pad pressure/aftertouch are both
    // ignored on this grid - no per-step velocity here. Clearing a
    // step (was_hit true) always auditions, transport running or not -
    // there's nothing else about to play it. Setting a step (was_hit false) only
    // auditions here when nothing is already going to hit it for real in
    // a moment: while the song is playing or the free-running audition
    // clock is looping, this exact lane/step is about to be triggered on
    // its own, at the actually-correct time - an immediate hit here would
    // land at a musically arbitrary point against that beat, on top of
    // (not instead of) the real one a moment later.
    bool suppress = !was_hit && (controller.getPlaybackInfo().isPlaying() || audition_clock_.isRunning());
    if (!suppress) {
      auto velocity = static_cast<short>(constants::DEFAULT_VELOCITY);
      event_queue.push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::PLAY_NOTE, controller.getActiveBufferName(), track_id, note, note, velocity));
    }
  } else if (ev.getKind() == LaunchpadPadEvent::RELEASE) {
    // Always silence the live-audition voice, mirroring the NOTES-mode
    // RELEASE branch's own "always silence" comment above.
    event_queue.push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::STOP_NOTE, controller.getActiveBufferName(), track_id, note));
  }
  // AFTERTOUCH: aftertouch is unused on this grid - no-op, unlike
  // ordinary NOTES-mode entry.
}

void
LaunchpadManager::handleDrumPickerPadEvent(LaunchpadPadEvent & ev, Controller & controller, PercussionTrack & track) {
  if (ev.getKind() != LaunchpadPadEvent::PRESS) return; // a plain tap - RELEASE/AFTERTOUCH are no-ops

  auto note = LaunchpadLayout::percussionNoteForPad(ev.getX(), ev.getY());
  if (note < 0) return; // unused pad in the free-drumming layout

  // removeLane() deletes every existing step referencing this note across
  // every section, not just the lane itself - see PercussionTrack.h's own
  // comment. Silent, no confirmation, no undo, per the brief's own
  // accepted risk for this gesture. addLane() is itself a silent no-op
  // once the track is already at
  // PercussionTrack::kMaxLanes (the step grid has exactly 8 rows to show
  // them in) - pressing an unlit pad while full just leaves it unlit,
  // same as pressing an already-assigned pad is already a no-op.
  bool was_assigned = track.hasLane(note);
  if (was_assigned) track.removeLane(note, controller.getSong());
  else track.addLane(note);

  controller.getSong().incVersion();

  // Only auditions on the way in, not the way out - unlike the step
  // grid's own PRESS branch (which fires regardless of toggle direction),
  // hearing a drum when you pick it makes sense but hearing it again as
  // its last act before being deleted doesn't. Always fires regardless of
  // playback/audition state - unlike the step grid's
  // own "setting a step" case (see handleStepGridPadEvent), picking a
  // lane isn't something the sequencer is about to hit on its own in a
  // moment (a brand new lane starts all-rest), so there's no "it'll play
  // for real soon anyway" reason to suppress this one. No STOP_NOTE
  // either way, matching this gesture's own "plain tap, not a held
  // gesture" design (RELEASE/AFTERTOUCH stay no-ops above) - same
  // one-shot-note-on convention triggerAuditionStep()/SongState::renderBlock()'s
  // own pattern-driven drum-hit emission already rely on.
  if (!was_assigned) {
    auto velocity = static_cast<short>(constants::DEFAULT_VELOCITY);
    controller.getPlaybackEventQueue().push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::PLAY_NOTE, controller.getActiveBufferName(), track.getInternalId(), note, note, velocity));
  }
}

void
LaunchpadManager::triggerAuditionStep(const Song & song, int track_id, Controller & controller, int step) {
  auto track = song.getMasterTrack().getChildByInternalId(track_id);
  if (!track) return;

  auto & event_queue = controller.getPlaybackEventQueue();
  auto & info = controller.getPlaybackInfo();
  auto & section = song.getSection(info.getPatternIndex()); // read-only audition - never grows the song

  // Idle auditioning only ever previews an explicitly focused clip
  // (Controller::getFocusedClip()), never the background/whatever
  // instance happens to be active there - "nothing selected" means
  // silence, not an uncontrolled loop of whatever was last recorded on
  // this track. resolveReadTarget()'s own is_focused_override tells the
  // two apart; falling through to ordinary resolution (no focus, or a
  // focus that belongs to some other track) means there's nothing to
  // preview here right now.
  auto read_target = resolveReadTarget(song, section, track_id, step, controller.getFocusedClip());
  if (!read_target.is_focused_override) return;

  // Every leaf track type plays the same way here, driven purely by
  // whatever's actually in the clip's own Pattern at this row -
  // column-addressed note-on/note-off/aftertouch, the same content real
  // (transport) playback itself reads (SongState.h's own render loop)
  // and the same PLAY_NOTE/STOP_NOTE/NOTE_PRESSURE events live
  // Kitty-keyboard/Launchpad note entry already use, just driven from
  // the clip's own content instead of a live keypress. No special-casing
  // by track type - a PercussionTrack's own step grid never writes an
  // explicit note-off today, but nothing stops one being placed by hand
  // (muting a cymbal, say), and this plays it exactly like any other
  // track's own note-off if it's there.
  auto & notes = read_target.pattern->getNotes(read_target.effective_row);
  for (size_t j = 0; j < notes.size(); j++) {
    auto & note = notes[j];
    if (!note.isDefined()) continue;
    auto column = static_cast<int>(j);
    if (note.isOff()) {
      event_queue.push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::STOP_NOTE, controller.getActiveBufferName(), track_id, column));
    } else if (note.isAftertouch()) {
      event_queue.push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::NOTE_PRESSURE, controller.getActiveBufferName(), track_id, column, 0, note.getVelocity()));
    } else {
      event_queue.push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::PLAY_NOTE, controller.getActiveBufferName(), track_id, column, note.getValue(), note.getVelocity()));
    }
  }
}

void
LaunchpadManager::handleChannelPressureEvent(LaunchpadChannelPressureEvent & ev, Controller & controller) {
  auto device_id = ev.getDeviceIndex();
  auto * state = findDeviceState(device_id);
  if (!state || state->active_notes.empty()) return;

  auto & event_queue = controller.getPlaybackEventQueue();

  // Dedup by track_id - a chord's notes are typically all on this one
  // device's currently assigned track, but nothing stops different pads
  // from landing on different tracks if the device was reassigned
  // mid-chord, so cover every track this device actually has a held note
  // on rather than assuming just one.
  vector<int> track_ids;
  for (auto & [ pos, note ] : state->active_notes) {
    if (find(track_ids.begin(), track_ids.end(), note.track_id) == track_ids.end()) {
      track_ids.push_back(note.track_id);
    }
  }
  for (auto track_id : track_ids) {
    event_queue.push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::CHANNEL_PRESSURE, controller.getActiveBufferName(), track_id, ev.getVelocity()));
  }
}

void
LaunchpadManager::refreshLeds(int device_id, DeviceState & state) {
  vector<LaunchpadProtocol::PadColor> colors;

  if (state.grid_mode == GridMode::SESSION) {
    // Fully resolved already (identity hue, triggered/queued brightening,
    // off where a track has no clip in that row) - see
    // refresh()'s own session_colors computation and DeviceState::
    // session_colors's own comment. Checked first, ahead of every other
    // branch below: SESSION is a hard override forced on by refresh()
    // itself, not a per-device toggle a user could combine with Send/Pan/
    // Draw/drum-machine display.
    for (int y = 0; y < 8; y++) {
      for (int x = 0; x < 8; x++) {
        auto & c = state.session_colors[static_cast<size_t>(y * 8 + x)];
        colors.push_back({LaunchpadProtocol::padToNoteNumber(x, y),
          static_cast<uint8_t>(c.getRed() / 2), static_cast<uint8_t>(c.getGreen() / 2), static_cast<uint8_t>(c.getBlue() / 2)});
      }
    }
  } else if (state.grid_mode == GridMode::DRAW) {
    // A plain coloring toy - each pad shows its own stored palette hue,
    // brightness-modulated by its own press/aftertouch intensity (see
    // colorForDrawPad()) - completely independent of Song/Track data and of
    // every other pad (see advanceDrawColor/updateDrawIntensity).
    for (int y = 0; y < 8; y++) {
      for (int x = 0; x < 8; x++) {
        size_t i = static_cast<size_t>(y * 8 + x);
        auto & hue = DRAW_PALETTE[static_cast<size_t>(state.draw_color_index[i])];
        auto c = colorForDrawPad(hue, state.draw_intensity[i]);
        colors.push_back({LaunchpadProtocol::padToNoteNumber(x, y), c.r, c.g, c.b});
      }
    }
  } else if (state.grid_mode != GridMode::NOTES && state.grid_mode != GridMode::CUSTOM) {
    // Send/Pan mode: the whole grid means something else entirely - each
    // column is one of the first 8 root tracks. Send A/B/Main fill
    // bottom-up as a bargraph of that track's current send level
    // (sendLinearToRow, its own dB curve's inverse) - a magnitude (Send
    // Main's own zero-config value, 1.0/0dB, so shows fully filled until
    // turned down). Pan lights only the one row
    // matching that track's current azimuth (see azimuthToRow) - a
    // direction, not a magnitude, so a fill wouldn't make sense; "only one
    // button highlighted" per column. No active/inactive feedback needed
    // on the mode buttons themselves (see refreshLeds' extra-button
    // section below) - this repaint *is* the confirmation the mode
    // actually changed.
    bool is_pan = state.grid_mode == GridMode::PAN;
    auto & values = state.grid_mode == GridMode::SEND_A ? state.track_send_a
                  : state.grid_mode == GridMode::SEND_B ? state.track_send_b
                  : state.grid_mode == GridMode::SEND_MAIN ? state.track_send_main
                  : state.track_azimuth;
    Rgb base = state.grid_mode == GridMode::SEND_A ? Rgb{0, 127, 127}
             : state.grid_mode == GridMode::SEND_B ? Rgb{127, 0, 127}
             : state.grid_mode == GridMode::SEND_MAIN ? Rgb{127, 127, 0}
             : Rgb{127, 64, 0};
    for (int x = 0; x < 8; x++) {
      // A column past the real track count has no value to show at all -
      // values[x] is just a stale/default 0.0f there, not "this track's
      // level is 0" - go fully dark rather than painting whatever that
      // default happens to map to (row 0 for Send A/B, dead-center for
      // Pan).
      bool has_track = x < state.grid_track_count;
      int lit_row = is_pan ? azimuthToRow(values[static_cast<size_t>(x)])
                            : sendLinearToRow(values[static_cast<size_t>(x)]);
      for (int y = 0; y < 8; y++) {
        bool lit = has_track && (is_pan ? (y == lit_row) : (y <= lit_row));
        Rgb color = lit ? base : Rgb{0, 0, 0};
        colors.push_back({LaunchpadProtocol::padToNoteNumber(x, y), color.r, color.g, color.b});
      }
    }
  } else if (state.grid_mode == GridMode::CUSTOM) {
    // CUSTOM: today, only a PercussionTrack has anything to customize (the
    // lane picker below); anything else shows a blank grid (see
    // GridMode::CUSTOM's own comment on what this could grow into).
    if (!state.assigned_track_is_percussion) {
      for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) colors.push_back({LaunchpadProtocol::padToNoteNumber(x, y), 0, 0, 0});
      }
    } else {
      // Drum picker: the free-drumming layout doubles as the picker
      // surface, reusing the exact same
      // note/family/color table the ordinary percussion note-grid uses
      // (percussionFamilyColor() above) rather than a second copy. A note
      // currently assigned to a lane renders at LAUNCHPAD_PICKER_ASSIGNED_LUMINOSITY,
      // an available-but-unpicked note at LAUNCHPAD_PICKER_IDLE_LUMINOSITY - both
      // dim (see that constant's own comment for why this isn't just
      // LAUNCHPAD_IDLE_LUMINOSITY/LAUNCHPAD_ACTIVE_LUMINOSITY), so the whole
      // family-colored layout stays visible/navigable throughout, not just
      // the picked subset.
      for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
          auto base = percussionFamilyColor(LaunchpadLayout::percussionFamilyForPad(x, y));
          auto note = LaunchpadLayout::percussionNoteForPad(x, y);
          bool assigned = note >= 0 && find(state.drum_lane_notes.begin(), state.drum_lane_notes.end(), note) != state.drum_lane_notes.end();
          Rgb color = {0, 0, 0};
          if (base.r != 0 || base.g != 0 || base.b != 0) {
            auto hsl = rgbToHsl(base);
            hsl.l = assigned ? LAUNCHPAD_PICKER_ASSIGNED_LUMINOSITY : LAUNCHPAD_PICKER_IDLE_LUMINOSITY;
            color = hslToRgb(hsl);
          }
          colors.push_back({LaunchpadProtocol::padToNoteNumber(x, y), color.r, color.g, color.b});
        }
      }
    }
  } else if (state.assigned_track_is_percussion && !state.drum_lane_notes.empty()) {
    // Step grid - not a GridMode value of
    // its own, displays automatically whenever the assigned track is a
    // step-sequenced PercussionTrack (isStepSequenced()) - a lane-less one
    // falls through to the ordinary percussion pad layout below instead.
    // Rows are lanes (y=0 bottom = drum_lane_notes[0], the
    // lowest-ranked lane), columns are steps (x=0..7). Lit = hit (green);
    // unlit-but-real = a faint dark outline, so a configured lane with a
    // rest step still reads as "a real pad", distinct from the fully black
    // pads past the track's actual lane count. The playhead column gets
    // the same lightness-only brightness boost padColor() already uses
    // for note loudness elsewhere in this file (idle -> active luminosity)
    // - reusing that existing HSL blend, keyed on column instead of note
    // loudness, rather than inventing a second compositing primitive.
    constexpr Rgb kStepLitColor { 0, 110, 20 };
    constexpr Rgb kStepUnlitColor { 12, 12, 12 };
    for (int y = 0; y < 8; y++) {
      for (int x = 0; x < 8; x++) {
        Rgb color {0, 0, 0};
        if (y < static_cast<int>(state.drum_lane_notes.size())) {
          bool lit = (state.drum_lane_steps[static_cast<size_t>(y)] & (1u << x)) != 0;
          color = lit ? kStepLitColor : kStepUnlitColor;
          if (x == state.drum_playhead_step) {
            auto hsl = rgbToHsl(color);
            hsl.l = LAUNCHPAD_ACTIVE_LUMINOSITY;
            color = hslToRgb(hsl);
          }
        }
        colors.push_back({LaunchpadProtocol::padToNoteNumber(x, y), color.r, color.g, color.b});
      }
    }
  } else if (state.tuning == Tuning::PERCUSSION) {
    for (int y = 0; y < 8; y++) {
      for (int x = 0; x < 8; x++) {
        auto base = percussionFamilyColor(LaunchpadLayout::percussionFamilyForPad(x, y));
        auto note = LaunchpadLayout::percussionNoteForPad(x, y);
        auto color = padColor(base, state.active_note_loudness, note);
        colors.push_back({LaunchpadProtocol::padToNoteNumber(x, y), color.r, color.g, color.b});
      }
    }
  } else {
    auto edo_steps = LaunchpadLayout::edoSteps(state.tuning);
    if (edo_steps <= 0) {
      // Defensive - every non-percussion Tuning is currently pitched; blank
      // the grid rather than showing a stale/misleading layout if this
      // ever changes.
      for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
          colors.push_back({LaunchpadProtocol::padToNoteNumber(x, y), 0, 0, 0});
        }
      }
    } else {
      auto basis = LaunchpadLayout::computeBasis(edo_steps);
      // Reduced to a pitch class, matching resolveNote's tonic (see its
      // comment) - purely for consistency, since classifyPad's result is
      // invariant to a uniform octave shift of base_note anyway.
      auto tonic = state.key >= 0 ? ((state.key % edo_steps) + edo_steps) % edo_steps : 0;
      auto base_note = tonic + (octave(device_id) + 1) * edo_steps;

      // Computed once per refresh, not once per pad - computeConsonanceLevels
      // does the actual recursive work (see its own doc comment), classifyPad
      // below is just a table lookup. Skipped entirely (left default-
      // constructed/unused) when degenerate, since classifyPad's own
      // contract requires callers to check that first.
      auto levels = basis.degenerate ? vector<LaunchpadLayout::PadClassification>() : LaunchpadLayout::computeConsonanceLevels(basis, edo_steps);

      for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
          Rgb color;
          if (basis.degenerate) {
            color = {40, 40, 40}; // degraded/fallback visual mode - no meaningful scale structure
          } else {
            auto classification = LaunchpadLayout::classifyPad(levels, basis, edo_steps, x - GRID_ORIGIN_X, y - GRID_ORIGIN_Y, base_note);
            color = consonanceColor(classification);
          }
          auto note = LaunchpadLayout::noteForPad(basis, x - GRID_ORIGIN_X, y - GRID_ORIGIN_Y, base_note);
          color = padColor(color, state.active_note_loudness, note);
          colors.push_back({LaunchpadProtocol::padToNoteNumber(x, y), color.r, color.g, color.b});
        }
      }
    }
  }

  // Track-picker overlay (see DeviceState::track_picker_active's own
  // comment): a post-process pass that only ever overwrites the picker
  // row itself with each selectable track's own bright/dim purpose color
  // (LAUNCHPAD_TRACK_PICKER_*_BRIGHT/DIM above - see
  // LAUNCHPAD_TRACK_PICKER_ROW's own comment for what bright vs. dim means
  // per purpose) - every other row is left exactly as Session view's own
  // rendering above already computed it, since the overlay is Session-
  // view-only now (GridMode's own comment) and Session view stays fully
  // interactive underneath (UI::handleLaunchpadPadEvent only ever routes
  // the picker row itself here - see isTrackPickerRow()). Colors already
  // pushed in row-major (x + y*8) order above, matching this loop's own
  // indexing.
  if (state.track_picker_active) {
    for (int x = 0; x < 8; x++) {
      auto & c = colors[static_cast<size_t>(LAUNCHPAD_TRACK_PICKER_ROW * 8 + x)];
      bool has_track = x < static_cast<int>(session_.track_ids.size());
      Rgb pick_color { 0, 0, 0 };
      if (has_track) {
        switch (state.track_picker_purpose) {
        case DeviceState::TrackPickerPurpose::STOP_CLIP:
          pick_color = state.track_picker_playing[static_cast<size_t>(x)] ? LAUNCHPAD_TRACK_PICKER_STOP_CLIP_BRIGHT : LAUNCHPAD_TRACK_PICKER_STOP_CLIP_DIM;
          break;
        case DeviceState::TrackPickerPurpose::SOLO:
          pick_color = state.track_picker_soloed[static_cast<size_t>(x)] ? LAUNCHPAD_TRACK_PICKER_SOLO_BRIGHT : LAUNCHPAD_TRACK_PICKER_SOLO_DIM;
          break;
        case DeviceState::TrackPickerPurpose::MUTE:
          pick_color = state.track_picker_muted[static_cast<size_t>(x)] ? LAUNCHPAD_TRACK_PICKER_MUTE_DIM : LAUNCHPAD_TRACK_PICKER_MUTE_BRIGHT;
          break;
        }
      }
      c.r = pick_color.r;
      c.g = pick_color.g;
      c.b = pick_color.b;
    }
  }

  // Extra-button LEDs. CC numbers unreachable on X/Mini MK3 (30, 20 -
  // Pro MK3's left column) are harmless to include here: those models
  // simply don't have the physical button, so the colourspec entry has
  // nothing to light.
  colors.push_back({91, 30, 30, 30}); // move-row-up, dim white (static)
  colors.push_back({92, 30, 30, 30}); // move-row-down, dim white (static)
  colors.push_back({93, 0, 0, 60});   // prev-track, dim blue (static)
  colors.push_back({94, 0, 0, 60});   // next-track, dim blue (static)
  // Session (CC95)/Note (CC96)/Custom (CC97)/Draw (CC98, reused from
  // "Capture MIDI" - the record-armed indicator moved to CC19 ("Record
  // Arm"), see DeviceState::record_arm_led_on's own comment) are this
  // device's own GridMode selectors - all four lit when active, same
  // active-state convention Mute/Solo already use, not the static/
  // no-state convention the Send/Pan mode buttons use (those repaint the
  // whole grid as their own confirmation; a mode switch here isn't as
  // visually distinct at a glance, so the button itself carries the state
  // too). Note (CC96) reaching NOTES is still a one-way action, not a
  // toggle (see handleRawButton()'s own comment) - but NOTES is a real,
  // visible mode like the other three, so it gets the same lit-when-active
  // treatment rather than staying static. Custom stays lit-by-mode the
  // same way regardless of whether the assigned track actually has
  // anything to customize (see GridMode::CUSTOM's own comment) - same as
  // Session/Note not caring what track type they land on either. Session
  // itself also carries its own mixer-submode indicator (DeviceState::
  // session_mixer_mode) - green while active and in scene-launch (the
  // default) submode, orange while active and in mixer submode instead,
  // dim green while not active at all - "active" meaning anywhere in the
  // family (inSessionMixerFamily()), not just the plain grid, so a fader/
  // picker still reads as "Session" underneath.
  {
    Rgb session_color = !inSessionMixerFamily(state) ? Rgb{0, 20, 0} : state.session_mixer_mode ? Rgb{127, 64, 0} : Rgb{0, 127, 0};
    colors.push_back({95, session_color.r, session_color.g, session_color.b});
  }
  colors.push_back({96, state.grid_mode == GridMode::NOTES ? uint8_t(90) : uint8_t(20), state.grid_mode == GridMode::NOTES ? uint8_t(90) : uint8_t(20), state.grid_mode == GridMode::NOTES ? uint8_t(90) : uint8_t(20)});
  colors.push_back({97, state.grid_mode == GridMode::CUSTOM ? uint8_t(90) : uint8_t(20), 0, state.grid_mode == GridMode::CUSTOM ? uint8_t(127) : uint8_t(20)});
  colors.push_back({98, state.grid_mode == GridMode::DRAW ? uint8_t(90) : uint8_t(20), 0, state.grid_mode == GridMode::DRAW ? uint8_t(127) : uint8_t(20)});
  // 99 (top-right corner, the grid position the Programmer-mode protocol
  // maps one past the 91-98 top row) isn't actually a pressable button on
  // real Launchpad X hardware - see handleRawButton()'s own comment - so
  // it's left off/reserved rather than wired to reflect any state.
  colors.push_back({99, 0, 0, 0});
  // Volume/Pan/SendA/SendB/Stop Clip/Mute/Solo (89/79/69/59/49/39/29, and
  // Pro MK3 left-column twins 30/20) are Session's own mixer-submode
  // radio group (DeviceState::session_mixer_mode/GridMode's own comment):
  // while that submode is off (the default), all seven are plain
  // scene-launch triggers with no state of their own to show, so they go
  // uniformly dim white - LAUNCHPAD_SCENE_LAUNCH_BUTTON_COLOR - rather
  // than any of their mixer-mode hues, which would otherwise misleadingly
  // suggest a fader/picker is one press away. While it's on, each shows
  // its own hue (LAUNCHPAD_TRACK_PICKER_*_BRIGHT/DIM above for the three
  // picker purposes - the exact same colors the picker row itself shows,
  // since which target track the action lands on isn't decided until a
  // pad there is actually pressed; a parallel BRIGHT/dim pair for the
  // four fader modes below) at full brightness for whichever one of the
  // seven is currently active, dim otherwise - never more than one bright
  // at once, matching the radio group's own "only one active" rule
  // (inSessionMixerFamily()).
  bool mixer_mode = state.session_mixer_mode;
  bool picker_mute = state.track_picker_active && state.track_picker_purpose == DeviceState::TrackPickerPurpose::MUTE;
  bool picker_solo = state.track_picker_active && state.track_picker_purpose == DeviceState::TrackPickerPurpose::SOLO;
  bool picker_stop_clip = state.track_picker_active && state.track_picker_purpose == DeviceState::TrackPickerPurpose::STOP_CLIP;
  Rgb stop_clip_button_color = !mixer_mode ? LAUNCHPAD_SCENE_LAUNCH_BUTTON_COLOR : picker_stop_clip ? LAUNCHPAD_TRACK_PICKER_STOP_CLIP_BRIGHT : LAUNCHPAD_TRACK_PICKER_STOP_CLIP_DIM;
  Rgb solo_button_color = !mixer_mode ? LAUNCHPAD_SCENE_LAUNCH_BUTTON_COLOR : picker_solo ? LAUNCHPAD_TRACK_PICKER_SOLO_BRIGHT : LAUNCHPAD_TRACK_PICKER_SOLO_DIM;
  Rgb mute_button_color = !mixer_mode ? LAUNCHPAD_SCENE_LAUNCH_BUTTON_COLOR : picker_mute ? LAUNCHPAD_TRACK_PICKER_MUTE_BRIGHT : LAUNCHPAD_TRACK_PICKER_MUTE_DIM;
  Rgb send_b_button_color = !mixer_mode ? LAUNCHPAD_SCENE_LAUNCH_BUTTON_COLOR : state.grid_mode == GridMode::SEND_B ? LAUNCHPAD_MIXER_SEND_B_BRIGHT : LAUNCHPAD_MIXER_SEND_B_DIM;
  Rgb send_a_button_color = !mixer_mode ? LAUNCHPAD_SCENE_LAUNCH_BUTTON_COLOR : state.grid_mode == GridMode::SEND_A ? LAUNCHPAD_MIXER_SEND_A_BRIGHT : LAUNCHPAD_MIXER_SEND_A_DIM;
  Rgb pan_button_color = !mixer_mode ? LAUNCHPAD_SCENE_LAUNCH_BUTTON_COLOR : state.grid_mode == GridMode::PAN ? LAUNCHPAD_MIXER_PAN_BRIGHT : LAUNCHPAD_MIXER_PAN_DIM;
  Rgb volume_button_color = !mixer_mode ? LAUNCHPAD_SCENE_LAUNCH_BUTTON_COLOR : state.grid_mode == GridMode::SEND_MAIN ? LAUNCHPAD_MIXER_VOLUME_BRIGHT : LAUNCHPAD_MIXER_VOLUME_DIM;

  colors.push_back({19, state.record_arm_led_on ? uint8_t(127) : uint8_t(20), 0, 0}); // record-arm toggle - untouched by mixer submode
  colors.push_back({29, solo_button_color.r, solo_button_color.g, solo_button_color.b});
  colors.push_back({39, mute_button_color.r, mute_button_color.g, mute_button_color.b});
  colors.push_back({49, stop_clip_button_color.r, stop_clip_button_color.g, stop_clip_button_color.b});
  colors.push_back({59, send_b_button_color.r, send_b_button_color.g, send_b_button_color.b});
  colors.push_back({69, send_a_button_color.r, send_a_button_color.g, send_a_button_color.b});
  colors.push_back({79, pan_button_color.r, pan_button_color.g, pan_button_color.b});
  colors.push_back({89, volume_button_color.r, volume_button_color.g, volume_button_color.b});
  colors.push_back({30, mute_button_color.r, mute_button_color.g, mute_button_color.b}); // Pro MK3 left column pos. 6 - same as CC39
  colors.push_back({20, solo_button_color.r, solo_button_color.g, solo_button_color.b}); // Pro MK3 left column pos. 7 - same as CC29

  // Only actually send when the computed colors changed since the last
  // send - continuous brightness fades mean refreshLeds() is now called
  // every frame while a note decays, not just on discrete state changes.
  bool colors_changed = colors.size() != state.last_sent_colors.size() ||
    !equal(colors.begin(), colors.end(), state.last_sent_colors.begin(),
      [](const LaunchpadProtocol::PadColor & a, const LaunchpadProtocol::PadColor & b) {
        return a.led_index == b.led_index && a.r == b.r && a.g == b.g && a.b == b.b;
      });
  if (!colors_changed) return;

  launchpad_io_->sendLeds(device_id, colors);
  state.last_sent_colors = move(colors);
}

void
LaunchpadManager::refresh(const Song & song, const vector<int> & track_ids, const PlaybackInfo & playback_info, int fallback_track_index, Controller & controller, const SessionWindow & session) {
  if (!launchpad_io_) return;

  // Mirrored once per frame, same as the note-capture-armed edge
  // detection below - see cached_global_octave_'s own comment.
  cached_global_octave_ = controller.getGlobalOctave();
  // Cached for handleSessionPadEvent() - see session_'s own comment.
  session_ = session;

  // Resolves, the instant Arm is pressed, whether this take is taking over
  // an already-playing clip on its own target track. A SampleTrack take
  // arms via isThresholdArmed() rather than note_capture_armed further
  // below, so this can't live inside that block - it needs to run for
  // either kind of take alike.
  bool session_recording = controller.isSessionRecording();
  if (session_recording && !was_session_recording_) {
    auto target_track_id = controller.getSessionRecordingTrackId();
    if (triggered_pattern_by_track_.find(target_track_id) != triggered_pattern_by_track_.end()) {
      // The target track already has a clip playing - queue its stop
      // through the exact same quantized mechanism a live pad press on an
      // already-triggered pad already uses (handleSessionPadEvent()'s own
      // comment), so it keeps playing right up to that shared boundary
      // rather than being cut the instant this take arms.
      // primeSessionRecordingOrigin() fixes the new take's own row 0 to
      // that identical boundary (not previousBarRow()'s own absolute-
      // row-zero grid, which generally disagrees with session_origin_step_'s
      // own grid) - see its own comment - and
      // session_recording_quantize_until_step_ makes handlePadEvent() drop
      // every press before that boundary outright, so the new take can
      // never start any earlier than the moment the old one actually
      // stops.
      auto rows_per_bar = std::max(1, song.getRowsPerBar());
      queued_pattern_by_track_[target_track_id] = -1;
      auto current_step = audition_clock_.isRunning() ? audition_clock_.currentStep() : 0;
      auto offset = ((current_step - session_origin_step_) % rows_per_bar + rows_per_bar) % rows_per_bar;
      // Strictly ahead of current_step, never equal to it: a queued action
      // only resolves against a step triggerClipStep() has yet to process
      // (see its own comment) - this step, even if it already happens to
      // sit on the grid, was already processed before this queue entry
      // existed, so the earliest it can actually take effect is a full
      // bar from here.
      auto boundary = current_step + (offset == 0 ? rows_per_bar : rows_per_bar - offset);
      session_recording_quantize_until_step_ = boundary;
      controller.primeSessionRecordingOrigin(boundary);
    } else {
      session_recording_quantize_until_step_ = -1;
    }
  }
  was_session_recording_ = session_recording;

  // Record Arm's own note-capture side effects live in this file (this
  // class's own Session-view audition bookkeeping and auto-started-
  // transport tracking, neither reachable from Controller directly), but
  // the flag itself can now flip from anywhere - M-x, a keybinding, a
  // Launchpad press alike (see was_note_capture_armed_'s own comment) -
  // so they react here, to the flag's own rising/falling edge each frame,
  // rather than inline in a button handler.
  bool note_capture_armed = controller.isNoteCaptureArmed();
  if (note_capture_armed && !was_note_capture_armed_) {
    // Arming: audition-only live-trigger bookkeeping becomes meaningless
    // from here on for an *ordinary* (non-Session-View) note take -
    // note_capture_armed alone already stops the free-running
    // audition_clock_ (see audition_active's own comment: "the player is
    // presumably about to record something deliberate and doesn't want an
    // uncontrolled loop underneath it"), but that only stops the *clock*,
    // not the data - triggered_pattern_by_track_/queued_pattern_by_track_
    // themselves stay exactly as they were. Left alone, they'd sit there
    // stale through the whole recording session and then resurrect the
    // moment it ends: stopping playback later makes audition_active true
    // again, restarting the clock, which would immediately resume
    // "auditioning" whatever was still marked triggered here - an old
    // clip suddenly playing again, and its own stale LED highlight right
    // along with it, neither of which the performer did anything to cause
    // after actually stopping.
    //
    // A Session View take is the deliberate exception: clearing
    // everything here would silence every other track's own live
    // performance the instant a fresh take arms, directly defeating the
    // point of layering one track's own recording on top of others still
    // playing. The rising-edge block above already handled the *target*
    // track's own already-playing clip on its own terms (kept alive until
    // the new take's own quantized start, not cut immediately, via
    // session_recording_quantize_until_step_) - every other track's own
    // triggered/queued state is left completely untouched either way.
    if (!controller.isSessionRecording()) {
      triggered_pattern_by_track_.clear();
      queued_pattern_by_track_.clear();
      session_origin_set_ = false;
    }
    // Starts the transport immediately, the same as SampleTrack's own
    // Record Arm - not deferred to the first actually-captured note/step
    // - so both branches behave alike; the clip itself still only gets
    // created once a note actually lands
    // (Controller::ensureNoteRecordingClip()'s own lazy-create gate).
    // startAutoRecordSession(), not the plainer startAutoRecordPlayback():
    // it also mutes the song's own pattern-driven scheduling, so the live
    // take is heard through its own separate stream rather than doubled
    // against old content. Never for a Session View take
    // (isSessionRecording()) - that populates a clip slot directly with no
    // arrangement involved at all, so there's nothing for the transport to
    // be running for.
    if (!controller.isSessionRecording() && !playback_info.isPlaying()) {
      controller.startAutoRecordSession(auto_started_playback_, auto_record_cleared_rows_, last_cleared_row_, last_cleared_pattern_idx_, auto_record_clip_ids_);
    }
  } else if (!note_capture_armed && was_note_capture_armed_ && auto_started_playback_) {
    // Disarming while a recording session this class itself auto-started
    // is still running stops the transport too - a live take with Record
    // Arm off has nothing left to record into, so "stop recording" is
    // naturally also "stop playback". stopAutoRecordSession() always
    // issues an explicit unmute regardless of whether this session ever
    // actually muted anything, so it's safe to call unconditionally here.
    controller.stopAutoRecordSession(auto_started_playback_, auto_record_cleared_rows_, playback_info, auto_record_clip_ids_);
    // Guarantees real silence on stop, not just "no more scheduling" -
    // SongState::renderBlock()'s own instance-termination release
    // (stopAllVoices()) only ever runs from within the per-row scheduling
    // loop, which is itself gated on isPlaying() - so whatever's actively
    // sounding at the exact moment playback stops here would otherwise
    // just keep ringing/decaying on its own instead of being released,
    // the same pre-existing "a voice's envelope keeps progressing while
    // playback is stopped" gap docs/known_bugs.md already tracks for a
    // plain manual stop.
    for (auto playable_track_id : controller.getSong().getPlayableTrackIds()) {
      controller.getPlaybackEventQueue().push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::STOP_ALL_NOTES, controller.getActiveBufferName(), playable_track_id));
    }
  }
  was_note_capture_armed_ = note_capture_armed;

  // A note-based Session View take that just finished joins Session View's
  // own live performance immediately, the same as a real pad press would -
  // looping (Controller::trimSessionRecordingClip() already flipped it to
  // that), and, silence permitting, triggered right now rather than left
  // sitting in the clip list for the performer to separately go find and
  // press - hearing your own take loop back instantly is the whole point
  // of a live-performance recorder. Mirrors handleSessionPadEvent()'s own
  // "nothing playing yet" launch vs. "something already is" queue
  // decision exactly, since a completed take joining is otherwise no
  // different from a live press on its own pad.
  // takeCompletedSessionRecording() hands this back at most once per take.
  if (auto completed = controller.takeCompletedSessionRecording()) {
    auto & clips = song.getClips(completed->track_id);
    if (completed->clip_index >= 0 && completed->clip_index < static_cast<int>(clips.size())) {
      if (triggered_pattern_by_track_.empty() && queued_pattern_by_track_.empty()) {
        auto launch_step = audition_clock_.isRunning() ? audition_clock_.currentStep() : 0;
        triggered_pattern_by_track_[completed->track_id] = {completed->clip_index, launch_step};
        session_origin_step_ = launch_step;
        session_origin_set_ = true;
        if (audition_clock_.isRunning()) {
          auto & launched_clip = clips[static_cast<size_t>(completed->clip_index)];
          auto launched_length = launched_clip.getLength() > 0 ? launched_clip.getLength() : 1;
          fireOrTriggerClipStep(song, controller, completed->track_id, completed->clip_index, launched_clip, launched_length, 0);
        }
      } else {
        queued_pattern_by_track_[completed->track_id] = completed->clip_index;
      }
    }
  }

  auto ready_ids = launchpad_io_->readySessionIds();

  // Prune cached state for devices no longer connected - session ids are
  // never reused (see LaunchpadIO's next_session_id_), so a stale entry
  // can never become relevant again.
  for (auto it = devices_.begin(); it != devices_.end(); ) {
    bool still_ready = find(ready_ids.begin(), ready_ids.end(), it->first) != ready_ids.end();
    if (!still_ready) it = devices_.erase(it);
    else ++it;
  }

  auto num_tracks = static_cast<int>(track_ids.size());

  // The track whose clip is currently focused for editing
  // (Controller::getFocusedClipTrackId(), SessionView's own Enter) -
  // triggerAuditionStep() below only fires for this one track, not
  // whichever track a Launchpad device happens to be assigned to: a
  // focus is deliberately hardware-independent (no Launchpad needs to be
  // connected at all to hear the clip you're editing), and deliberately
  // exclusive - a single, currently-selected-for-editing clip, not
  // Session-view-style multi-track simultaneous launching. Silencing the
  // *previous* focus's track on any focus change is Controller's own job
  // (setFocusedClip()/clearFocusedClip()), not this loop's.
  auto focused_track_id = controller.getFocusedClipTrackId();

  // The free-running drum-machine/clip audition clock - computed
  // once here, shared by every connected device below, not per-device.
  // audition_clock_ itself (StepClock, LaunchpadTiming.h) is the pure,
  // unit-tested step-advance logic; everything here is just wall-clock
  // bookkeeping and plugging the real song/track data in. Active exactly
  // while the transport is stopped and Record Arm is off - while playing,
  // the pattern-driven path in SongState::renderBlock() already triggers
  // these same tracks from real song position, and running both at once
  // would double-trigger; while armed, the player is presumably about to
  // record something deliberate and doesn't want an uncontrolled loop
  // underneath it. A Session View take is the one exception - it needs
  // this same clock kept running, not stopped, since it's what drives
  // extendSessionRecordingClipIfNeeded() below in place of the (here,
  // deliberately never-started) transport; safe to keep running through
  // an armed Session View take specifically because arming itself already
  // cleared triggered_pattern_by_track_/queued_pattern_by_track_ above, so
  // there's no stale auditioned content left for it to resume. audition_step
  // is this frame's step for the per-device playhead display below, or -1
  // when the clock isn't running at all.
  int audition_step = -1;
  bool audition_active = !playback_info.isPlaying() && (!note_capture_armed || controller.isSessionRecording());
  if (audition_active) {
    auto now = chrono::steady_clock::now();
    if (!audition_clock_.isRunning()) {
      // (Re)starting: always from step 0, so stopping and restarting (or
      // arming and disarming) never leaves the loop's phase drifted from
      // what a player would expect ("it starts over from the top").
      // start() itself doesn't fire step 0 (see its own comment) - that's
      // this caller's policy: fire it immediately, no dead air waiting
      // for the first row to elapse.
      audition_clock_.start();
      audition_clock_last_refresh_ = now;
      if (focused_track_id >= 0) triggerAuditionStep(song, focused_track_id, controller, audition_clock_.currentStep());
      triggerClipStep(song, controller, audition_clock_.currentStep());
      if (controller.isSessionRecording()) controller.extendSessionRecordingClipIfNeeded(audition_clock_.currentStep());
    } else {
      float dt = chrono::duration<float>(now - audition_clock_last_refresh_).count();
      audition_clock_last_refresh_ = now;
      auto tempo = song.getTempo();
      // Same row-duration formula as ChannelConfiguration::getRowDuration()
      // (a "row" is a 16th note at this tempo) - no ChannelConfiguration
      // needed here since this clock never touches sample counts, only
      // wall-clock seconds. tempo <= 0 -> row_duration <= 0 -> advance()
      // is a no-op (see its own guard), same as elsewhere in this
      // codebase treating a non-positive tempo/loop-length as degenerate.
      float row_duration = tempo > 0 ? 60.0f / 4.0f / static_cast<float>(tempo) : 0.0f;
      for (int step : audition_clock_.advance(dt, row_duration)) {
        if (focused_track_id >= 0) triggerAuditionStep(song, focused_track_id, controller, step);
        triggerClipStep(song, controller, step);
        if (controller.isSessionRecording()) controller.extendSessionRecordingClipIfNeeded(step);
      }
    }
    audition_step = audition_clock_.currentStep();
  } else {
    // A held note triggerAuditionStep() started (via PLAY_NOTE) but never
    // got a matching STOP_NOTE for - the audition clock stopping mid-note
    // (playback starting, or Record Arm arming) rather than the clip's
    // own content actually ending it - would otherwise ring out
    // indefinitely with nothing left driving it forward. Only meaningful
    // if the clock was actually running a moment ago (checked before the
    // stop() below clears that) and something was actually focused;
    // redundant-safe otherwise, matching this codebase's own established
    // "fire the natural release unconditionally, costs nothing extra"
    // precedent (SongState.h's own instance-termination cleanup).
    if (audition_clock_.isRunning() && focused_track_id >= 0) {
      controller.getPlaybackEventQueue().push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::STOP_ALL_NOTES, controller.getActiveBufferName(), focused_track_id));
    }
    audition_clock_.stop();
  }

  // The Send A/Send B/Send Main/Pan grid modes always address the first 8
  // root tracks (not whichever track a device happens to be assigned to) -
  // the same values apply to every connected device, computed once here
  // rather than per-device inside the loop below.
  array<float, 8> track_send_main{}, track_send_a{}, track_send_b{}, track_azimuth{};
  for (int i = 0; i < 8 && i < num_tracks; i++) {
    // No track-type check needed - track_ids is already
    // getPlayableTrackIds()'s own "every LeafTrack" list (its own doc
    // comment), so this cast always succeeds; a stale, narrower type
    // whitelist here once silently left a SampleTrack's own fader/LED
    // feedback at 0 regardless of its actual send/pan values.
    auto track = song.getMasterTrack().getChildByInternalId(track_ids[static_cast<size_t>(i)]);
    if (track) {
      auto & leaf_track = dynamic_cast<const LeafTrack&>(*track);
      track_send_main[static_cast<size_t>(i)] = leaf_track.getSends().main;
      track_send_a[static_cast<size_t>(i)] = leaf_track.getSends().a;
      track_send_b[static_cast<size_t>(i)] = leaf_track.getSends().b;
      track_azimuth[static_cast<size_t>(i)] = leaf_track.getAzimuth();
    }
  }

  // GridMode::SESSION's own shared LED grid - same "computed once here,
  // identical for every connected device" reasoning as track_send_main/
  // etc. above, and same reason this stays a plain Color array rather than
  // a DeviceState-nested computation: refreshLeds() only ever reads
  // DeviceState, never Song/PlaybackInfo directly (see its own branches).
  // Computed unconditionally (not gated on anything overview-focus-
  // related) since which devices, if any, are actually showing Session
  // view is now purely each one's own CC95/96 selection - see
  // handleRawButton()'s own comment. x = column, indexed into
  // session.track_ids - the overview's own filtered column list, not
  // track_ids above (this class's usual root-track-id parameter, which
  // includes non-color-eligible tracks Session view never shows a column
  // for); no column scroll yet (see SessionWindow's own comment). y is
  // flipped the same way the old plain-navigation overview's own rows
  // were: y=0 is the bottom-left pad (see LaunchpadProtocol::
  // padToNoteNumber()'s own doc comment), so y=7 (top) is that track's
  // first clip and y=0 (bottom) its last visible one; no row
  // scroll yet either, so a track with more than 8 clips only
  // shows the first 8 for now.
  array<Color, 64> session_colors;
  // The track-picker overlay's own per-track state (see
  // DeviceState::track_picker_active's own comment and
  // LAUNCHPAD_TRACK_PICKER_ROW's own comment in this file for what each
  // one drives) - computed alongside session_colors below since both walk
  // the same per-track session.track_ids loop.
  array<bool, 8> track_picker_playing {};
  array<bool, 8> track_picker_soloed {};
  array<bool, 8> track_picker_muted {};
  {
    SongStructure structure(song);
    const Color white(255, 255, 255);
    // While actually playing (Session-view recording included - that's
    // exactly the "was playing" case that used to leave these LEDs stuck),
    // triggered_pattern_by_track_/queued_pattern_by_track_ are stale: the
    // audition clock that populates them only ever runs while stopped and
    // unarmed (audition_active, above), and Session-view recording's own
    // assign path (placeClipInstance()/placeRecordingStop()) never touches
    // them either - it writes real song data instead. What's actually
    // sounding while playing is whatever resolveInstanceAt() resolves at
    // the live position, so that's what these LEDs show then; the
    // audition-only bookkeeping only still applies while genuinely
    // stopped, the one state it's ever populated in.
    bool use_real_position = playback_info.isPlaying();
    const Section * current_section = use_real_position ? &song.getSection(playback_info.getPatternIndex()) : nullptr;
    for (int x = 0; x < 8; x++) {
      if (x >= static_cast<int>(session.track_ids.size())) continue;
      auto session_track_id = session.track_ids[static_cast<size_t>(x)];
      // Same hue/near-fully-saturated identity the overview's own
      // terminal glyphs use, but at its own, dimmer lightness: a directly-
      // emitted LED pixel at a given lightness reads brighter than the
      // same value does as terminal glyph text, so the two surfaces are
      // tuned independently here rather than sharing one constant.
      auto identity = Color::fromHSL(structure.getBaselineInfo(session_track_id).getHue(), 0.8f, 0.3f);
      auto & clips = song.getClips(session_track_id);
      int real_active_clip_index = use_real_position ?
        resolveInstanceAt(song, *current_section, session_track_id, playback_info.getRowIndex()).clip_index : -1;
      auto triggered_it = triggered_pattern_by_track_.find(session_track_id);
      auto queued_it = queued_pattern_by_track_.find(session_track_id);
      // STOP_CLIP's own picker-row state: a real clip instance is active
      // right now (playing), the same sentinel-checked source
      // session_colors' own is_triggered below reads per-row - here it
      // just needs to know whether *any* row is (kStopInstance/
      // kNoInstance are both negative, so a real clip_index is >= 0).
      track_picker_playing[static_cast<size_t>(x)] = use_real_position ?
        real_active_clip_index >= 0 : triggered_it != triggered_pattern_by_track_.end();
      auto track = song.getMasterTrack().getChildByInternalId(session_track_id);
      if (track) {
        auto & leaf_track = dynamic_cast<const LeafTrack &>(*track);
        track_picker_soloed[static_cast<size_t>(x)] = leaf_track.isSolo();
        track_picker_muted[static_cast<size_t>(x)] = leaf_track.isMuted();
      }
      for (int y = 0; y < 8; y++) {
        auto clip_index = 7 - y;
        if (clip_index >= static_cast<int>(clips.size())) continue;
        bool is_triggered, is_queued;
        if (use_real_position) {
          is_triggered = real_active_clip_index == clip_index;
          is_queued = false; // no "about to launch" concept while genuinely playing - a press takes effect as a real, bar-quantized write, not an audition-only queued swap
        } else {
          is_triggered = triggered_it != triggered_pattern_by_track_.end() && triggered_it->second.clip_index == clip_index;
          is_queued = queued_it != queued_pattern_by_track_.end() && queued_it->second == clip_index;
        }
        Color c = identity;
        if (is_triggered) c = identity.blend(0.5f, white); // currently playing
        else if (is_queued) c = identity.blend(0.25f, white); // about to launch at the next loop boundary
        session_colors[static_cast<size_t>(y * 8 + x)] = c;
      }
    }
  }

  for (auto device_id : ready_ids) {
    // A device not already in devices_ is being seen for the first time
    // this session (freshly connected, or reconnected after having been
    // pruned above on an earlier disconnect) - deviceState() below is
    // about to default-construct its DeviceState, octave_offset included
    // (defaulting to 0, i.e. "exactly the global octave"), so this is the
    // one moment to apply defaultOctaveOffsetForModel()'s per-model nudge
    // instead of leaving every device at the same relative register.
    bool is_new_device = devices_.find(device_id) == devices_.end();
    auto & state = deviceState(device_id);
    if (is_new_device) {
      if (auto model = launchpad_io_->modelForSession(device_id)) {
        state.octave_offset = LaunchpadLayout::clampOctaveOffset(state.octave_offset, defaultOctaveOffsetForModel(*model));
      }
    }

    // Every device follows the one shared cursor now - no more per-device
    // assignment of its own (see track_move_callback_'s own comment). Out
    // of range (e.g. -1, no track selected while the overview has focus)
    // is handled below by simply skipping the per-track lookups.
    auto track_index = fallback_track_index;

    Tuning tuning = Tuning::TET12;
    int key_val = -1;
    unordered_map<int, float> active_note_loudness;
    bool is_percussion = false;
    vector<int> drum_lane_notes;
    array<uint8_t, 8> drum_lane_steps {};
    int drum_playhead_step = -1;
    // A drum clip open for editing pins this device's own display to it
    // while actually in NOTES or CUSTOM mode (where the step grid/lane
    // picker themselves live) - see handlePadEvent()'s own identical
    // override for why (this is its LED-rendering counterpart); a device
    // that's since switched to a different GridMode (Send A, say, opened
    // temporarily on top) is unaffected and keeps following the shared
    // cursor for whatever that mode shows instead.
    int pinned_track_id = (state.grid_mode == GridMode::NOTES || state.grid_mode == GridMode::CUSTOM) ? controller.getFocusedClipTrackId() : -1;
    if (pinned_track_id >= 0 || (track_index >= 0 && track_index < num_tracks)) {
      auto track_id = pinned_track_id >= 0 ? pinned_track_id : track_ids[static_cast<size_t>(track_index)];
      auto track = song.getMasterTrack().getChildByInternalId(track_id);
      tuning = track ? song.getTuningForTrack(*track) : song.getTuning();
      key_val = song.getKey();
      is_percussion = track && track->getType() == TrackType::PERCUSSION_CONTROL;
      if (is_percussion) {
        auto & drum_track = static_cast<const PercussionTrack &>(*track);
        drum_lane_notes = drum_track.getLaneNotes();
        // A Session-View-focused clip can be longer than the grid's fixed
        // 8 columns - this device's own current page (DeviceState::
        // drum_edit_page, see handleCommand()'s own comment) picks which
        // 8-row window of it is actually shown; editing the background
        // pattern instead (nothing focused for this track) always shows
        // rows 0-7 exactly as before, no paging.
        bool drum_clip_editing = controller.getFocusedClipTrackId() == track_id;
        auto drum_clip_length = drum_clip_editing ? focusedDrumClipLength(song, track_id, controller.getFocusedClip()) : -1;
        auto drum_page_count = drum_clip_length > 0 ? (drum_clip_length + 7) / 8 : 1;
        auto drum_page = drum_clip_editing ? std::clamp(state.drum_edit_page, 0, drum_page_count - 1) : 0;
        // A lane's own hit state, this section - by value (getHitNotesAtRow(),
        // matching handleStepGridPadEvent()'s own by-value "was_hit" check),
        // not by assuming it's sitting at this lane's usual column. Per
        // step, whatever's actually active at that row (ArrangementOps.h's
        // own resolveReadTarget()) - a placed clip instance's own content,
        // or this track's own background Pattern otherwise - rather than
        // one background-only lookup reused across all 8 steps.
        auto & drum_section = song.getSection(playback_info.getPatternIndex());
        for (int step = 0; step < 8; step++) {
          auto read_target = resolveReadTarget(song, drum_section, track_id, drum_page * 8 + step, controller.getFocusedClip());
          for (int hit_note : drum_track.getHitNotesAtRow(*read_target.pattern, read_target.effective_row)) {
            auto lane_it = find(drum_lane_notes.begin(), drum_lane_notes.end(), hit_note);
            if (lane_it == drum_lane_notes.end()) continue;
            auto lane_index = static_cast<size_t>(lane_it - drum_lane_notes.begin());
            drum_lane_steps[lane_index] = static_cast<uint8_t>(drum_lane_steps[lane_index] | (1u << step));
          }
        }
        // While playing, the real song position; while stopped, the
        // free-running audition clock's own shared step - or no playhead
        // at all if that clock isn't currently running either (Record Arm
        // is on). Only shown at all if it actually falls within this
        // device's own currently-shown page - a focused clip playing back
        // a page this device isn't showing right now has no visible
        // playhead here (paging elsewhere on the same device brings it
        // back into view instead).
        if (playback_info.isPlaying()) {
          drum_playhead_step = playback_info.getRowIndex() % 8;
        } else if (audition_step >= 0) {
          auto clip_row = drum_clip_length > 0 ? audition_step % drum_clip_length : audition_step % 8;
          auto step_page = drum_clip_length > 0 ? clip_row / 8 : 0;
          drum_playhead_step = (step_page == drum_page) ? clip_row % 8 : -1;
        }
      }
      // Max-of when multiple columns happen to sound the same note_value
      // (e.g. unison), so the pad shows the loudest of them.
      for (auto & voice : playback_info.getActiveVoices(track_id)) {
        if (voice.note_value < 0) continue;
        auto & loudness = active_note_loudness[voice.note_value];
        loudness = max(loudness, voice.loudness);
      }
    }

    state.connected = true;
    state.capture_enabled = note_capture_armed; // mirrors the one song-wide flag - see its own comment
    state.record_arm_led_on = note_capture_armed || controller.isThresholdArmed() || controller.isRecording();
    state.tuning = tuning;
    state.key = key_val;
    state.active_note_loudness = move(active_note_loudness);
    state.session_colors = session_colors;
    state.track_picker_playing = track_picker_playing;
    state.track_picker_soloed = track_picker_soloed;
    state.track_picker_muted = track_picker_muted;
    state.track_send_main = track_send_main;
    state.track_send_a = track_send_a;
    state.track_send_b = track_send_b;
    state.track_azimuth = track_azimuth;
    state.grid_track_count = min(8, num_tracks);
    state.assigned_track_is_percussion = is_percussion;
    state.drum_lane_notes = move(drum_lane_notes);
    state.drum_lane_steps = drum_lane_steps;
    state.drum_playhead_step = drum_playhead_step;

    refreshLeds(device_id, state);
  }
}
