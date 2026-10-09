#include "LaunchpadManager.h"
#include "../playback/EventHandler.h"
#include "../playback/LogEvent.h"

#include "LaunchpadIO.h"
#include "LaunchpadLayout.h"
#include "LaunchpadProtocol.h"
#include "LaunchpadPadEvent.h"
#include "LaunchpadChannelPressureEvent.h"
#include "../state/PlaybackInfo.h"
#include "../playback/PlaybackControlEvent.h"
#include "../model/Song.h"
#include "../model/Command.h"
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
  // How long CC98 (handleRecordButton()) must be held before release means
  // Capture MIDI instead of a quick tap's own Session Record. Long enough
  // that a normal deliberate tap never accidentally reads as a hold.
  constexpr auto kCaptureHoldThreshold = std::chrono::milliseconds(600);

  // How long a DRAW-mode grid pad must be held before release means "just
  // adjust brightness" instead of "cycle to the next hue" - see
  // releaseDrawPad(). Same value as kCaptureHoldThreshold above but declared
  // separately since they're conceptually independent controls that could
  // reasonably be tuned apart later.
  constexpr auto kDrawPadLongPressThreshold = std::chrono::milliseconds(600);

  // How long one of the mixer radio group's eight members must be held
  // before release reverts the display back to whatever was showing
  // before this press, instead of leaving the switch that already
  // happened on press standing - LaunchpadManager::
  // handleMixerFunctionRelease()'s own momentary-hold-to-preview gesture.
  // Same value as kDrawClearHoldThreshold/kDrawPadLongPressThreshold above
  // (all three are "long press" thresholds for an otherwise-instant
  // Launchpad gesture) but declared separately, same reasoning as those
  // two share it.
  constexpr auto kMixerHoldPreviewThreshold = std::chrono::milliseconds(600);

  // The step grid's own scroll step, both axes - move-row-up/down and
  // pad-prev-track/pad-next-track each advance this many rows/steps per press,
  // rather than jumping a whole 8-wide window at once, so consecutive
  // windows overlap and a performer can actually follow where a press
  // landed relative to before (half the fixed 8-row/8-column grid).
  constexpr int kStepGridScrollStep = 4;
  constexpr int kStepWindow = LaunchpadLayout::kStepsPerView;

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
  // y-flip), one pad per selectable track, matching live_.track_ids/
  // Live View's own column order. Every other row is left exactly as
  // Live View's own rendering already drew it - the overlay is
  // Live-View-only (GridMode's own comment), so there's no other
  // GridMode content underneath to distinguish it from, and Live View
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
  // Reuses Stop Clip's own red rather than picking a fourth distinct hue -
  // the two purposes never show at once (only one picker purpose is ever
  // active), and red is already CC19's own long-established "Record Arm"
  // color (see DeviceState::record_arm_led_on's own comment) as well as
  // the future per-pad armed-track indicator's own color, so keeping
  // Record Arm red throughout stays consistent rather than introducing a
  // fifth hue nothing else on the device uses.
  constexpr Rgb LAUNCHPAD_TRACK_PICKER_RECORD_ARM_BRIGHT { 127, 0, 0 };
  constexpr Rgb LAUNCHPAD_TRACK_PICKER_RECORD_ARM_DIM    { 20, 0, 0 };

  // The mixer radio group's remaining four members - Volume/Pan/Send A/
  // Send B, which stay a real GridMode swap rather than an overlay (see
  // GridMode's own comment) - get the same bright-when-active/dim-
  // otherwise treatment as the three track-picker purposes above, each
  // keeping the hue Novation's own Launchpad X Session-mode documentation
  // gives that fader's own bargraph (green/purple/blue for Volume/Send A/
  // Send B; Pan has none - see track_colors' own comment, its fader grid
  // uses each track's own identity color instead of one fixed hue, so
  // this opener button's own hue is this codebase's own pick, not a
  // documented one) rather than adopting red/blue/yellow like the picker
  // trio, since these four don't share the picker's single-overlay
  // identity.
  constexpr Rgb LAUNCHPAD_MIXER_SEND_B_BRIGHT { 0, 0, 127 };
  constexpr Rgb LAUNCHPAD_MIXER_SEND_B_DIM    { 0, 0, 40 };
  constexpr Rgb LAUNCHPAD_MIXER_SEND_A_BRIGHT { 90, 0, 127 };
  constexpr Rgb LAUNCHPAD_MIXER_SEND_A_DIM    { 30, 0, 40 };
  constexpr Rgb LAUNCHPAD_MIXER_PAN_BRIGHT    { 127, 64, 0 };
  constexpr Rgb LAUNCHPAD_MIXER_PAN_DIM       { 40, 20, 0 };
  constexpr Rgb LAUNCHPAD_MIXER_VOLUME_BRIGHT { 0, 127, 0 };
  constexpr Rgb LAUNCHPAD_MIXER_VOLUME_DIM    { 0, 40, 0 };

  // Live's mixer-submode radio group (see DeviceState::
  // mixer_mode's own comment), all seven buttons, while that
  // submode is off - a plain scene-launch trigger has no state of its own
  // worth showing, so all seven go uniformly dim white rather than any of
  // their mixer-mode hues (which would otherwise misleadingly suggest a
  // fader/picker is one press away) - same dim-white convention the
  // static move-row-up/down utility buttons already use.
  constexpr Rgb LAUNCHPAD_SCENE_LAUNCH_BUTTON_COLOR { 30, 30, 30 };

  // Volume/Pan/Send A/Send B's own fader micro-values (FaderState::
  // micro_step) show as a lightness scale on just the fader's own top pad,
  // once one is actually active (micro_step > 0) - this is the floor at
  // micro_step 1 (the dimmest real micro-value), scaling up to 1.0
  // (unscaled base color) at micro_step 2. A plain, non-repeated press
  // (micro_step 0) skips this scale entirely and shows unscaled base
  // color too, same as every other lit bargraph row.
  constexpr float LAUNCHPAD_FADER_MICRO_MIN_SCALE = 0.35f;

  // Live View's own triggered/queued pad overlay (DeviceState::
  // clip_highlight) - a real hardware flash/pulse animation, driven by
  // the device's own internal clock rather than anything this file redraws
  // frame by frame, so it needs palette indices, not the arbitrary RGB
  // every other Live View pad color in this file uses. Values taken directly
  // from Novation's own worked example lighting a pad "flashing green
  // (between dim and bright green)", not an approximation - the palette is
  // a fixed 128-entry table, so there is no way to ask for "this track's
  // own hue, just flashing" instead.
  constexpr uint8_t LAUNCHPAD_CLIP_GREEN_PALETTE_BRIGHT = 21;
  constexpr uint8_t LAUNCHPAD_CLIP_GREEN_PALETTE_DIM = 23;
  // A launched clip while the transport is paused: the same green, static.
  constexpr Rgb LAUNCHPAD_CLIP_PAUSED = { 0, 127, 0 };

  // An armed track's own red equivalent of the two above (ClipHighlight::
  // RECORD_QUEUED/RECORDING/RECORD_STOPPING) - unlike the green pair, not
  // taken from a Novation worked example (none published one for red);
  // best-effort standard-palette picks, not yet confirmed against real
  // hardware - expect these to shift after testing, same as
  // percussionFamilyColor()'s own hues.
  constexpr uint8_t LAUNCHPAD_CLIP_RED_PALETTE_BRIGHT = 5;
  constexpr uint8_t LAUNCHPAD_CLIP_RED_PALETTE_DIM = 7;

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

  // How long one of the Pan row's chord-eligible pads (columns 0/7 and
  // 3/4) waits for its partner before applying as a single press - short
  // enough to feel immediate, long enough to catch a two-finger press.
  constexpr auto kPanChordWindow = std::chrono::milliseconds(50);
  // A Pan bar's pads show the track's own color at these fractions of its
  // lightness: the tip (outermost lit pad) full, the rest of the bar
  // dimmer, and the centred marker (azimuth exactly 0) dimmest.
  constexpr float kPanBarScale = 0.55f;
  constexpr float kPanCenterScale = 0.25f;

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

  // sendLinearToRow()'s own db->row half, factored out so
  // resolveSendFaderTarget() (whose FaderState already tracks dB, the
  // same unit sendRowToDb()/Controller::setTrackSendA()/B()/Main() all
  // use - never linear gain) can reach it directly rather than
  // double-converting through linearToDb() a second time via
  // sendLinearToRow() itself.
  int sendDbToRow(float db) {
    // Nearer to off than to the lowest real (row-1) step - round down to
    // the hard-off row rather than the same half-step rounding the real
    // steps below use, so a value that's genuinely off (or migrated from
    // one that was) always redraws as row 0, not a barely-lit row 1.
    if (db <= SEND_ROW_FLOOR_DB - (-SEND_ROW_FLOOR_DB) / 6.0f / 2.0f) return 0;
    float row = 1.0f + (db - SEND_ROW_FLOOR_DB) * 6.0f / (-SEND_ROW_FLOOR_DB);
    return std::clamp(static_cast<int>(lround(row)), 0, 7);
  }

  // A fader press's own glide duration (faderGlideDurationSeconds()) scales
  // between these by press velocity: a hard press moves a full range in
  // kMinFaderRampSeconds (near-instant), the softest one takes
  // kMaxFaderRampSeconds - long enough for a fade in or out. The scale is
  // exponential, so both ends keep a useful spread of speeds, and a
  // partial-range move takes proportionally less time at any given
  // velocity than crossing the whole range would. Not calibrated against
  // documented hardware behavior - tuned so a press at the velocity every
  // existing e2e fixture sends (100) finishes well inside the ~1s those
  // scripts wait before reading the result back.
  constexpr float kMinFaderRampSeconds = 0.03f;
  constexpr float kMaxFaderRampSeconds = 8.0f;
  // The reference "whole range" a glide's distance is measured as a
  // fraction of, in each parameter's own unit - Send's is the full
  // sentinel-off-to-unity span (linearToDb's own -100dB floor to 0dB),
  // Pan's is the farthest apart two positions can be around a circle
  // (180 degrees - a wraparound delta, computed at the press site, never
  // exceeds this).
  constexpr float kSendFullRangeDb = 100.0f;
  constexpr float kAzimuthFullRangeDegrees = 180.0f;

  // Real velocity-sensitive hardware doesn't reliably saturate near the
  // MIDI velocity ceiling: a deliberate max-effort press on a Launchpad
  // X's own Send-fader pad landed mostly in the 65-92 range (raw velocity
  // bytes captured from the device), while a Mini MK3's non-velocity-
  // sensitive pads report a flat 127 unconditionally. So the ramp is
  // already at its fastest from kFaderFullSpeedVelocity up, and only the
  // range below it spreads out toward the slow end.
  constexpr int kFaderFullSpeedVelocity = 80;

  // resolveSendFaderTarget()'s (Send Main/A/B) and
  // resolveAzimuthFaderTarget()'s (Pan) own shared glide-duration formula -
  // identical shape, just fed a different distance_fraction for each's own
  // value range (kSendFullRangeDb/kAzimuthFullRangeDegrees), so it's a
  // single source of truth for the velocity curve rather than two copies
  // that could drift apart.
  float faderGlideDurationSeconds(int velocity, float distance_fraction) {
    float speed = std::clamp(static_cast<float>(velocity - 1) / static_cast<float>(kFaderFullSpeedVelocity - 1), 0.0f, 1.0f);
    float base_duration = kMinFaderRampSeconds * powf(kMaxFaderRampSeconds / kMinFaderRampSeconds, 1.0f - speed);
    return std::max(kMinFaderRampSeconds, base_duration * distance_fraction);
  }

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

}

float
LaunchpadManager::sendRowToDb(int row) {
  if (row <= 0) return -100.0f;
  return SEND_ROW_FLOOR_DB + (-SEND_ROW_FLOOR_DB) * static_cast<float>(row - 1) / 6.0f;
}

int
LaunchpadManager::sendLinearToRow(float linear) {
  return sendDbToRow(linearToDb(linear));
}

float
LaunchpadManager::resolveSendFaderTarget(FaderState & fader, float current_value, int pressed_row, int velocity,
    bool & is_micro_tap, float & out_duration_seconds) {
  auto now = std::chrono::steady_clock::now();
  // A server-side glide fired by a still-recent press hasn't necessarily
  // finished yet - checked lazily right here (there's nothing ticking
  // fader.ramping down any more) rather than via a running client-side
  // ramp, same reasoning as the same_row_repeat check just below (not
  // conflating "the value already happens to sit near this row" with
  // "you actually pressed it there").
  bool still_gliding = fader.ramping && std::chrono::duration<float>(now - fader.start_time).count() < fader.duration_seconds;
  is_micro_tap = fader.touched && pressed_row == fader.last_pressed_row && !still_gliding;
  if (is_micro_tap) {
    // 2 micro-values - 8 rows * 2 = 16 distinct positions, matching the
    // 4-bit target resolution a recorded `YMxy`/`YAxy`/`YBxy` command can
    // actually address (see docs/commands.md's own "Recorded fader-glide
    // commands" section) - a finer live micro-step than that would
    // silently round away to the same recorded value anyway.
    fader.micro_step = fader.micro_step >= 2 ? 1 : fader.micro_step + 1;
    int next_row = std::min(7, pressed_row + 1); // Send's row 7 is a true ceiling - never wraps
    float base_value = sendRowToDb(pressed_row);
    float target = base_value + (sendRowToDb(next_row) - base_value) * static_cast<float>(fader.micro_step) / 2.0f;
    fader.ramping = false; // instant - nothing left gliding after a micro-tap
    return target;
  }

  fader.touched = true;
  fader.last_pressed_row = pressed_row;
  fader.micro_step = 0;
  float target = sendRowToDb(pressed_row);
  float distance_fraction = std::clamp(std::fabs(target - current_value) / kSendFullRangeDb, 0.0f, 1.0f);
  out_duration_seconds = faderGlideDurationSeconds(velocity, distance_fraction);
  fader.ramping = true;
  fader.start_time = now;
  fader.duration_seconds = out_duration_seconds;
  return target;
}

float
LaunchpadManager::resolveAzimuthFaderTarget(FaderState & fader, float current_value, float target_degrees, int velocity,
    float & out_duration_seconds) {
  auto now = std::chrono::steady_clock::now();
  fader.touched = true;
  // Circular distance (the shorter way around - same reasoning
  // LeafTrackState::glideAzimuth() uses to actually pick a travel
  // direction), so a press near the wrap point isn't penalized with an
  // implausibly slow near-360-degree "distance".
  float delta = fmodf(target_degrees - current_value, 360.0f);
  if (delta > 180.0f) delta -= 360.0f;
  else if (delta <= -180.0f) delta += 360.0f;
  float distance_fraction = std::clamp(std::fabs(delta) / kAzimuthFullRangeDegrees, 0.0f, 1.0f);
  out_duration_seconds = faderGlideDurationSeconds(velocity, distance_fraction);
  fader.ramping = true;
  fader.start_time = now;
  fader.duration_seconds = out_duration_seconds;
  return target_degrees;
}

void
LaunchpadManager::applyPanTarget(Controller & controller, int track_id, float target_degrees, int velocity) {
  auto track = controller.getSong().getMasterTrack().getChildByInternalId(track_id);
  auto leaf_track = track ? dynamic_cast<LeafTrack *>(track) : nullptr;
  if (!leaf_track) return;
  auto & fader = fader_state_azimuth_[track_id];
  float duration_seconds = 0.0f;
  resolveAzimuthFaderTarget(fader, leaf_track->getAzimuth(), target_degrees, velocity, duration_seconds);
  controller.glideTrackAzimuth(track_id, target_degrees, duration_seconds);
  recordFaderAutomationIfArmed(controller, fader, track_id, Command::azimuthGlide(target_degrees, duration_seconds));
}

void
LaunchpadManager::handlePanPress(Controller & controller, int device_id, int track_id, int column, int velocity) {
  auto & chord = pan_chords_[{device_id, track_id}];
  // A held-back press has to land before anything that follows it in this
  // row, or the later one would be overwritten by the earlier.
  auto flushPending = [&]() {
    if (chord.pending_column < 0) return;
    applyPanTarget(controller, track_id, LaunchpadLayout::panPadToAzimuth(chord.pending_column), chord.pending_velocity);
    chord.pending_column = -1;
  };
  int partner = column == 0 ? 7 : column == 7 ? 0 : column == 3 ? 4 : column == 4 ? 3 : -1;
  bool partner_held = partner >= 0 && (chord.held_columns & (1u << partner)) != 0;
  chord.held_columns |= 1u << column;
  if (partner_held && chord.pending_column == partner) {
    chord.pending_column = -1;
    applyPanTarget(controller, track_id, column == 0 || column == 7 ? LaunchpadLayout::kPanRearDegrees : 0.0f, velocity);
    return;
  }
  flushPending();
  if (partner >= 0) {
    chord.pending_column = column;
    chord.pending_velocity = velocity;
    chord.pending_time = std::chrono::steady_clock::now();
    return;
  }
  applyPanTarget(controller, track_id, LaunchpadLayout::panPadToAzimuth(column), velocity);
}

void
LaunchpadManager::handlePanRelease(Controller & controller, int device_id, int track_id, int column) {
  auto it = pan_chords_.find({device_id, track_id});
  if (it == pan_chords_.end()) return;
  auto & chord = it->second;
  chord.held_columns &= ~(1u << column);
  // A tap shorter than the chord window still applies.
  if (chord.pending_column == column) {
    applyPanTarget(controller, track_id, LaunchpadLayout::panPadToAzimuth(column), chord.pending_velocity);
    chord.pending_column = -1;
  }
}

void
LaunchpadManager::flushPendingPanPresses(Controller & controller) {
  auto now = std::chrono::steady_clock::now();
  for (auto & [ key, chord ] : pan_chords_) {
    if (chord.pending_column < 0 || now - chord.pending_time < kPanChordWindow) continue;
    applyPanTarget(controller, key.second, LaunchpadLayout::panPadToAzimuth(chord.pending_column), chord.pending_velocity);
    chord.pending_column = -1;
  }
}

void
LaunchpadManager::recordFaderAutomationIfArmed(Controller & controller, FaderState & fader, int track_id, Command command) {
  auto & song = controller.getSong();
  Pattern * pattern = nullptr;
  int row = 0;
  if (controller.isClipRecording(track_id)) {
    // Into the take's clip, at the row a note pressed now lands on.
    auto step = controller.getClipPlayer().quantizedStep();
    auto take_row = controller.ensureClipRecordingClip(track_id, step.step, step.bar_start);
    auto clip_index = controller.getClipRecordingClipIndex(track_id);
    auto & clips = song.getClips(track_id);
    if (take_row < 0 || clip_index < 0 || clip_index >= static_cast<int>(clips.size())) return;
    auto & clip = clips[static_cast<size_t>(clip_index)];
    pattern = &clip.getLeafPattern();
    row = take_row % std::max(1, clip.getLength());
  } else {
    // The same "you're recording a take right now" condition arrangement
    // note entry gates on - armed and genuinely playing, not just
    // armed-while-stopped.
    auto & playback_info = controller.getPlaybackInfo();
    if (!controller.isNoteCaptureArmed() || !playback_info.isPlaying()) return;
    pattern = &song.getArrangement().getPatternsByTrack()[track_id];
    row = playback_info.getAbsolutePosition();
  }
  Song::Edit edit(song, "record fader automation", Song::Edit::Kind::CONTENT);
  if (fader.automation_pattern == pattern && fader.automation_row == row && fader.automation_column >= 0) {
    pattern->setCommand(row, fader.automation_column, command);
  } else {
    fader.automation_column = pattern->pushCommand(row, command);
    fader.automation_pattern = pattern;
    fader.automation_row = row;
  }
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

std::vector<LaunchpadManager::ActiveNote> *
LaunchpadManager::findActiveNotes(int device_id, int x, int y) {
  auto it = devices_.find(device_id);
  if (it == devices_.end()) return nullptr;
  auto note_it = it->second.active_notes.find({x, y});
  if (note_it == it->second.active_notes.end()) return nullptr;
  return &note_it->second;
}

void
LaunchpadManager::recordActiveNote(int device_id, int x, int y, ActiveNote note) {
  deviceState(device_id).active_notes[{x, y}].push_back(note);
}

void
LaunchpadManager::clearActiveNotes(int device_id, int x, int y) {
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
  // Live mixer-submode radio group member (see GridMode's own comment)
  // - a no-op unless already somewhere in that family (inMixerFamily()),
  // so pressing a fader button from NOTES/CUSTOM/DRAW does nothing, but
  // pressing one while another family member (a different fader, or the
  // track-picker overlay) is already active switches straight to it.
  // Closing (a repeat press of the one already active) always lands back
  // on LIVE, not NOTES, since that's the only place these are ever
  // entered from any more.
  if (!inMixerFamily(state)) return;
  bool already_active = state.grid_mode == mode;
  armMixerHoldPreview(state, already_active);
  state.track_picker_active = false; // switching to (or off of) a fader always leaves the picker
  state.grid_mode = already_active ? GridMode::LIVE : mode;
}

bool
LaunchpadManager::inMixerFamily(const DeviceState & state) const {
  return state.grid_mode == GridMode::LIVE || state.grid_mode == GridMode::SEND_MAIN ||
    state.grid_mode == GridMode::PAN || state.grid_mode == GridMode::SEND_A ||
    state.grid_mode == GridMode::SEND_B || state.track_picker_active;
}

void
LaunchpadManager::openStepView() {
  // The preview clock only ever stops while playing or armed, so it's
  // almost always still running here, wherever it happened to already be -
  // restarting it is what makes "opening a clip" actually mean "hear it
  // from its own row 0".
  preview_clock_.start();
  preview_clock_last_refresh_ = chrono::steady_clock::now();
  if (!launchpad_io_) return;
  auto ready_ids = launchpad_io_->readySessionIds();
  // Only a press on a device opens it, so that device is the one last used.
  if (std::find(ready_ids.begin(), ready_ids.end(), last_active_device_) == ready_ids.end()) return;
  step_view_device_ = last_active_device_;
  for (auto device_id : ready_ids) {
    auto & state = deviceState(device_id);
    state.drum_edit_step_offset = 0;
    state.selected_step_note = -1;
    state.octave_offset = 0;
  }
  auto & state = deviceState(step_view_device_);
  state.grid_mode = GridMode::NOTES;
  state.track_picker_active = false; // Live-View-only - see DeviceState::track_picker_active's own comment
}

void
LaunchpadManager::closeStepView() {
  auto it = devices_.find(step_view_device_);
  if (it != devices_.end() && it->second.grid_mode == GridMode::NOTES) it->second.grid_mode = GridMode::LIVE;
  step_view_device_ = -1;
}

bool
LaunchpadManager::handleRawButton(int cc_number, int device_id, Controller & controller) {
  last_active_device_ = device_id;
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
  // getSendMain()) rather than the shared send bus. 98 (Session Record)
  // is handled separately, in handleRecordButton() - unlike these four,
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
  // 19/89/79/69/59/49/39/29 (Record Arm/Volume/Pan/SendA/SendB/Stop Clip/
  // Mute/Solo, and Pro MK3 left-column twins 30/20 for Mute/Solo) are the
  // real Launchpad X's own right-column "Track control" group, all eight
  // sharing one dispatch (row = (cc_number - 19) / 10, CC19 itself being
  // row 0 - see this method's own doc comment) and the exact same
  // scene-launch/mixer-radio-group split (isMixerFunctionButton()'s own
  // per-CC list) - see LaunchpadManager.h's own doc comment for the full
  // reasoning. Off (the default), each launches a whole scene instead -
  // the classic Launchpad right-column convention, all eight included.
  // On, they're the mixer radio group: Volume/Pan/SendA/SendB enter a
  // fader GridMode (toggleGridMode()), Stop Clip/Mute/Solo/Record Arm
  // open/retarget the track-picker overlay (toggleTrackPicker()) - all
  // already handle their own "only one of the eight active" logic via
  // inMixerFamily(), so this dispatch only needs to pick which of
  // the two mechanisms a given CC number means. "toggle-record-arm"'s own
  // per-current-track arm/disarm (Controller.cpp) isn't part of this group
  // at all: on a Launchpad it's shift + CC98 (handleRecordButton()).
  // A no-op outside the Live family entirely (inMixerFamily()) -
  // none of these eight have any meaning to a grid that isn't showing
  // Live content or one of its own fader/picker overlays, the same
  // guard toggleGridMode()/toggleTrackPicker() already apply to the
  // mixer-submode-on half of this dispatch; scene-launch needs the exact
  // same guard for the mixer-submode-off half, since Live's own take
  // on "what plays" isn't what a NOTES/CUSTOM/DRAW press should be able to
  // reach either.
  if (isMixerFunctionButton(cc_number)) {
    auto & state = deviceState(device_id);
    // Tempo/Swing views: Send B and Stop Clip switch to (or leave) their view
    // without shift; the rest of the column does nothing.
    if (inNumberView(state)) {
      if (cc_number == 59) toggleNumberView(state, GridMode::TEMPO);
      else if (cc_number == 49) toggleNumberView(state, GridMode::SWING);
      return true;
    }
    // Shift (CC91 held) makes these the labelled alternate functions
    // instead, in every grid mode: Volume duplicates a clip for as long as
    // it stays held, Pan deletes likewise, Solo toggles the metronome click;
    // Record Arm (undo) and Mute (redo) are reserved and only say so.
    if (state.row_up_shift_held) {
      state.row_up_shift_combined = true;
      if (cc_number == 89) {
        state.duplicate_held = true;
      } else if (cc_number == 59) {
        toggleNumberView(state, GridMode::TEMPO);
      } else if (cc_number == 49) {
        toggleNumberView(state, GridMode::SWING);
      } else if (cc_number == 79) {
        state.delete_held = true;
      } else if (cc_number == 69) {
        state.quantize_held = true;
        state.quantize_used = false;
      } else if (cc_number == 29 || cc_number == 20) {
        controller.sendCommand("toggle-metronome");
      } else if (cc_number == 19) {
        controller.getUIEventQueue().push(std::make_unique<LogEvent>("Undo: not implemented yet"));
      } else if (cc_number == 39 || cc_number == 30) {
        controller.getUIEventQueue().push(std::make_unique<LogEvent>("Redo: not implemented yet"));
      }
      return true;
    }
    // Note entry: Record Arm starts and stops capturing what's played. A
    // sample take or threshold arm is ended through the shared command,
    // which doesn't depend on terminal focus; note capture is toggled
    // directly, since the command would arm the clip grid's track instead
    // while that has focus.
    if (cc_number == 19 && state.grid_mode == GridMode::NOTES && !state.show_step_grid) {
      if (controller.isRecording() || controller.isThresholdArmed()) {
        controller.sendCommand("toggle-record-arm");
      } else if (controller.isNoteCaptureArmed()) {
        controller.disarmNoteCapture();
      } else {
        controller.armNoteCapture();
      }
      return true;
    }
    if (!inMixerFamily(state)) return true;
    if (hasStopSoloMuteCycle(device_id)) {
      if (cc_number == 19)
        cycleStopSoloMute(device_id);
      else
        triggerSceneRow(controller, (cc_number - 19) / 10);
      return true;
    }
    if (!state.mixer_mode) {
      triggerSceneRow(controller, (cc_number - 19) / 10);
      return true;
    }
    switch (cc_number) {
    case 19: toggleTrackPicker(device_id, DeviceState::TrackPickerPurpose::RECORD_ARM); break;
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
  // (reached through shift + 97 instead): each press
  // *selects* that mode unconditionally, even if it's already the current
  // one - the only way to ever leave a mode is to select a *different*
  // one of the four. Purely per-device state, like every other toggle
  // here, not tied to whether the overview widget has terminal UI focus at
  // all: one connected Launchpad can sit in Live View while another
  // stays on ordinary note entry. 95 also doubles as the mixer-submode
  // toggle (DeviceState::mixer_mode) - a repeat press while
  // already at the plain Live grid with nothing from the mixer radio
  // group active flips it; either way this unconditionally lands on (or
  // stays on) that plain grid, closing any active fader/picker first if
  // there was one - one press to back out of a radio-group selection,
  // a second to then flip the submode itself.
  if (cc_number == 95) {
    auto & state = deviceState(device_id);
    // Computed before closeDrumClipFocus() below, which can itself flip
    // grid_mode to LIVE on the step-view device (via closeStepView())
    // as a side effect - reading it after would misread "just closed the
    // sequencer" as "already at the plain grid", spuriously flipping
    // mixer_mode on a press that was actually meant to leave the
    // sequencer, not toggle the mixer submode.
    bool at_plain_live_grid = state.grid_mode == GridMode::LIVE && !state.track_picker_active;
    // Live's own button also means "leave the sequencer entirely" when
    // a clip is currently open for step-grid editing - the same closing
    // effect a second press of whatever opened it already has
    // (Controller::toggleDrumClipFocus()'s own close branch) - so every
    // connected device returns to the plain Live grid, not just this
    // one, and none of them are left showing the step grid or any button
    // still highlighted for it. A no-op when nothing's focused.
    controller.closeDrumClipFocus();
    if (at_plain_live_grid && !hasStopSoloMuteCycle(device_id)) state.mixer_mode = !state.mixer_mode;
    state.grid_mode = GridMode::LIVE;
    state.track_picker_active = false;
    return true;
  }
  if (cc_number == 96) {
    auto & state = deviceState(device_id);
    // Shift + Note opens (or closes) the selected clip for step editing.
    if (state.row_up_shift_held) {
      state.row_up_shift_combined = true;
      auto & ui_events = controller.getUIEventQueue();
      if (selected_track_id_ < 0) {
        ui_events.push(std::make_unique<LogEvent>("Step edit: select a clip first (shift + pad)"));
      } else if (!controller.toggleDrumClipFocus(selected_track_id_, selected_clip_index_)) {
        ui_events.push(std::make_unique<LogEvent>("Step edit: that track has no step grid"));
      }
      return true;
    }
    state.grid_mode = GridMode::NOTES;
    state.track_picker_active = false; // Live-View-only - see DeviceState::track_picker_active's own comment
    return true;
  }
  if (cc_number == 97) {
    auto & state = deviceState(device_id);
    // Shift + Custom is Draw (or blanks its canvas if already there).
    if (state.row_up_shift_held) {
      state.row_up_shift_combined = true;
      if (state.grid_mode == GridMode::DRAW) {
        state.draw_color_index.fill(0);
      } else {
        state.grid_mode = GridMode::DRAW;
        state.track_picker_active = false;
      }
      return true;
    }
    state.grid_mode = GridMode::CUSTOM;
    state.track_picker_active = false; // Live-View-only - see DeviceState::track_picker_active's own comment
    return true;
  }
  return false;
}

bool
LaunchpadManager::isColumnLiveHeld(int track_id, int note_column) const {
  for (auto & [ device_id, state ] : devices_) {
    for (auto & [ pos, notes ] : state.active_notes) {
      for (auto & note : notes) {
        if (note.track_id == track_id && note.note_column == note_column) return true;
      }
    }
  }
  return false;
}

void
LaunchpadManager::onRowAdvanced(Controller & controller) {
  if (!auto_started_playback_) return;

  auto & info = controller.getPlaybackInfo();
  auto track_ids = getActiveNoteTrackIds();
  controller.sweepAutoRecordRows(auto_record_cleared_rows_, last_cleared_row_, info.getAbsolutePosition(), track_ids);
}

vector<int>
LaunchpadManager::getActiveNoteTrackIds() const {
  // Not just the caller's own device - two different Launchpads could be
  // assigned to different tracks and both mid-hold at once.
  vector<int> track_ids;
  for (auto & [ device_id, state ] : devices_) {
    for (auto & [ pos, notes ] : state.active_notes) {
      for (auto & note : notes) {
        if (find(track_ids.begin(), track_ids.end(), note.track_id) == track_ids.end()) {
          track_ids.push_back(note.track_id);
        }
      }
    }
  }
  return track_ids;
}

bool
LaunchpadManager::handleRecordButton(int device_id, Controller & controller, bool is_press) {
  auto & state = deviceState(device_id);
  if (is_press) {
    // Nothing fires yet - a tap and a hold mean two unrelated things, so
    // there's nothing safe to commit to until release settles which this was.
    state.record_button_pressed = true;
    state.record_button_press_time = std::chrono::steady_clock::now();
    // Shift + this button is the arrangement's own Record Arm instead;
    // the shift hold is spent on it, so its own tap doesn't also fire.
    state.record_button_shifted = state.row_up_shift_held;
    if (state.record_button_shifted) state.row_up_shift_combined = true;
    return true;
  }
  if (!state.record_button_pressed) return true; // stray/duplicate release
  state.record_button_pressed = false;
  if (state.record_button_shifted) {
    state.record_button_shifted = false;
    controller.sendCommand("toggle-record-arm");
    return true;
  }
  auto held = std::chrono::steady_clock::now() - state.record_button_press_time;
  auto & ui_events = controller.getUIEventQueue();
  if (held >= kCaptureHoldThreshold) {
    ui_events.push(std::make_unique<LogEvent>("Capture MIDI: not implemented yet"));
    return true;
  }
  auto track_ids = controller.getSong().getPlayableTrackIds();
  auto followed = cursor_track_index_ >= 0 && cursor_track_index_ < static_cast<int>(track_ids.size()) ?
    track_ids[static_cast<size_t>(cursor_track_index_)] : -1;
  if (!controller.getClipPlayer().toggleOverdub(followed)) {
    ui_events.push(std::make_unique<LogEvent>("Session Record: no playing clip to overdub"));
  }
  return true;
}

void
LaunchpadManager::endQuantize(int device_id, Controller & controller) {
  auto & state = deviceState(device_id);
  if (!state.quantize_held) return;
  state.quantize_held = false;
  if (!state.quantize_used) controller.sendCommand("toggle-record-quantize");
  state.quantize_used = false;
}

bool
LaunchpadManager::handleShiftButton(int device_id, bool is_press, Controller & controller) {
  last_active_device_ = device_id;
  auto & state = deviceState(device_id);
  // In a Tempo/Swing view CC91 is only the up arrow, never shift: it steps
  // on press and a hold repeats.
  if (inNumberView(state)) {
    state.row_up_shift_held = false;
    state.row_up_shift_combined = false;
    if (is_press) {
      stepNumberView(state, +1, controller);
      state.arrow_held_cc = 91;
      state.arrow_repeating = false;
      state.arrow_press_time = chrono::steady_clock::now();
    } else if (state.arrow_held_cc == 91) {
      state.arrow_held_cc = 0;
      state.arrow_repeating = false;
    }
    return false;
  }
  if (is_press) {
    // A fresh hold - nothing combined with it yet. handleLivePadEvent()
    // flips row_up_shift_combined the moment a pad press actually uses
    // this held state to open a clip instead of triggering it.
    state.row_up_shift_held = true;
    state.row_up_shift_combined = false;
    return false; // never fires "move-row-up" on press - see this method's own comment
  }
  state.row_up_shift_held = false;
  auto fire = !state.row_up_shift_combined;
  state.row_up_shift_combined = false;
  return fire;
}

bool
LaunchpadManager::inNumberView(const DeviceState & state) {
  return state.grid_mode == GridMode::TEMPO || state.grid_mode == GridMode::SWING;
}

void
LaunchpadManager::toggleNumberView(DeviceState & state, GridMode view) {
  state.track_picker_active = false;
  if (state.grid_mode == view) {
    state.grid_mode = state.number_view_return_mode; // the gesture again leaves the view
    return;
  }
  if (!inNumberView(state)) state.number_view_return_mode = state.grid_mode;
  state.grid_mode = view;
  state.delete_held = false;
  state.arrow_held_cc = 0;
  state.arrow_repeating = false;
}

void
LaunchpadManager::stepNumberView(const DeviceState & state, int delta, Controller & controller) {
  auto & song = controller.getSong();
  if (state.grid_mode == GridMode::TEMPO) controller.setTempo(song.getTempo() + delta);
  else if (state.grid_mode == GridMode::SWING) controller.setSwing(song.getSwing() + delta);
}

void
LaunchpadManager::handleArrowRelease(int device_id, int cc_number) {
  auto & state = deviceState(device_id);
  if (state.arrow_held_cc == cc_number) {
    state.arrow_held_cc = 0;
    state.arrow_repeating = false;
  }
}

void
LaunchpadManager::tickNumberView(Controller & controller) {
  constexpr auto kArrowRepeatDelay = chrono::milliseconds(400);
  constexpr auto kArrowRepeatInterval = chrono::milliseconds(100);
  auto now = chrono::steady_clock::now();
  for (auto & [ device_id, state ] : devices_) {
    if (state.arrow_held_cc == 0 || !inNumberView(state)) continue;
    if (!state.arrow_repeating) {
      if (now - state.arrow_press_time < kArrowRepeatDelay) continue;
      state.arrow_repeating = true;
      state.arrow_last_step = now;
    } else if (now - state.arrow_last_step < kArrowRepeatInterval) {
      continue;
    }
    state.arrow_last_step = now;
    stepNumberView(state, state.arrow_held_cc == 91 ? +1 : -1, controller);
  }
}

bool
LaunchpadManager::isTrackPickerRow(int device_id, int y) const {
  auto * state = findDeviceState(device_id);
  return state && state->track_picker_active && y == LAUNCHPAD_TRACK_PICKER_ROW;
}

void
LaunchpadManager::toggleTrackPicker(int device_id, DeviceState::TrackPickerPurpose purpose) {
  auto & state = deviceState(device_id);
  // Live mixer-submode radio group member (see GridMode's own comment)
  // - a no-op unless already somewhere in that family
  // (inMixerFamily()), so opening from NOTES/CUSTOM/DRAW does
  // nothing, but switching from a fader mode straight into the picker (or
  // between two picker purposes) always works.
  if (!inMixerFamily(state)) return;
  bool already_active = state.track_picker_active && state.track_picker_purpose == purpose;
  armMixerHoldPreview(state, already_active);
  state.grid_mode = GridMode::LIVE; // leaving a fader mode for the picker always lands on the plain grid underneath
  state.track_picker_active = !already_active;
  state.track_picker_purpose = purpose; // harmless to set even when closing - only read while track_picker_active
}

bool LaunchpadManager::hasStopSoloMuteCycle(int device_id) const {
  if (!launchpad_io_) return false;
  auto model = launchpad_io_->modelForSession(device_id);
  return model && LaunchpadProtocol::getModelInfo(*model).stop_solo_mute_cycle_button;
}

void LaunchpadManager::cycleStopSoloMute(int device_id) {
  using Purpose = DeviceState::TrackPickerPurpose;
  auto & state = deviceState(device_id);
  state.grid_mode = GridMode::LIVE;
  state.mixer_hold_pending = false; // a tap-only cycle, nothing to revert on release
  if (!state.track_picker_active) {
    state.track_picker_active = true;
    state.track_picker_purpose = Purpose::STOP_CLIP;
  } else if (state.track_picker_purpose == Purpose::STOP_CLIP) {
    state.track_picker_purpose = Purpose::SOLO;
  } else if (state.track_picker_purpose == Purpose::SOLO) {
    state.track_picker_purpose = Purpose::MUTE;
  } else {
    state.track_picker_active = false;
  }
}

void
LaunchpadManager::armMixerHoldPreview(DeviceState & state, bool already_active) {
  if (already_active) {
    // Closing the member already showing - not a switch to anything, so
    // there's nothing to preview-and-revert; make sure a stale arm from
    // an earlier press can't somehow still fire on a later release.
    state.mixer_hold_pending = false;
    return;
  }
  state.mixer_hold_pending = true;
  state.mixer_hold_press_time = std::chrono::steady_clock::now();
  state.mixer_hold_previous_grid_mode = state.grid_mode;
  state.mixer_hold_previous_track_picker_active = state.track_picker_active;
  state.mixer_hold_previous_track_picker_purpose = state.track_picker_purpose;
}

bool
LaunchpadManager::isMixerFunctionButton(int cc_number) {
  switch (cc_number) {
  case 19: case 89: case 79: case 69: case 59: case 49: case 39: case 30: case 29: case 20:
    return true;
  default:
    return false;
  }
}

void
LaunchpadManager::handleMixerFunctionRelease(int device_id, int cc_number, Controller & controller) {
  auto & state = deviceState(device_id);
  if (cc_number == 89 && state.duplicate_held) {
    state.duplicate_held = false;
    return;
  }
  if (cc_number == 79 && state.delete_held) {
    state.delete_held = false;
    return;
  }
  if (cc_number == 69 && state.quantize_held) {
    endQuantize(device_id, controller);
    return;
  }
  if (!state.mixer_hold_pending) return; // stray/duplicate release, or this press never armed one (a repress that closed something)
  state.mixer_hold_pending = false;
  if (std::chrono::steady_clock::now() - state.mixer_hold_press_time < kMixerHoldPreviewThreshold) return; // a quick tap - the switch that already happened on press stands
  // A genuine hold, now released - revert to whatever was showing right
  // before this press (armMixerHoldPreview()'s own snapshot).
  state.grid_mode = state.mixer_hold_previous_grid_mode;
  state.track_picker_active = state.mixer_hold_previous_track_picker_active;
  state.track_picker_purpose = state.mixer_hold_previous_track_picker_purpose;
}

void
LaunchpadManager::handleTrackPickerPadEvent(const LaunchpadPadEvent & ev, Controller & controller) {
  if (ev.getKind() != LaunchpadPadEvent::PRESS) return;
  auto & state = deviceState(ev.getDeviceIndex());
  if (ev.getY() != LAUNCHPAD_TRACK_PICKER_ROW) return; // defensive only - the caller (isTrackPickerRow()) never routes any other row here

  auto track_index = ev.getX();
  if (track_index < 0 || track_index >= static_cast<int>(live_.track_ids.size())) return; // no track behind this column
  auto track_id = live_.track_ids[static_cast<size_t>(track_index)];

  switch (state.track_picker_purpose) {
  case DeviceState::TrackPickerPurpose::STOP_CLIP:
    controller.getClipPlayer().stopTrack(track_id);
    break;
  case DeviceState::TrackPickerPurpose::MUTE:
    controller.toggleTrackMuted(track_id);
    break;
  case DeviceState::TrackPickerPurpose::SOLO:
    controller.toggleTrackSolo(track_id);
    break;
  case DeviceState::TrackPickerPurpose::RECORD_ARM:
    // Pure per-track bookkeeping regardless of track type (Controller::
    // toggleTrackArmed()'s own comment) - a SampleTrack's own audio
    // capture isn't on the new quantized-start-via-pad-press path yet
    // (ClipPlayer::triggerClip()'s own SampleTrack carve-out), but arming it
    // here is still exactly this same harmless toggle; only what a later
    // pad press on it actually does differs.
    controller.toggleTrackArmed(track_id);
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
  // In a Tempo/Swing view CC92 is the down arrow (a hold repeats, see
  // tickNumberView()), and the rest of the arrow row does nothing.
  if (auto & view_state = deviceState(device_id); inNumberView(view_state)) {
    if (name == "move-row-down") {
      view_state.arrow_held_cc = 92;
      view_state.arrow_repeating = false;
      view_state.arrow_press_time = chrono::steady_clock::now();
      stepNumberView(view_state, -1, controller);
      return true;
    }
    if (name == "move-row-up" || name == "pad-next-track" || name == "pad-prev-track" || name == "octave-up" || name == "octave-down") return true;
  }
  if (name == "octave-up") {
    octaveUp(device_id);
    return true;
  }
  if (name == "octave-down") {
    octaveDown(device_id);
    return true;
  }
  if (name == "move-row-up" || name == "move-row-down") {
    // While a clip's step view shows, these shift the playing surface's own
    // octave on a pitched track (a drum kit has nothing to shift) instead of
    // their Live-only/terminal-only meaning.
    if (gridMode(device_id) == GridMode::NOTES && controller.getFocusedClipTrackId() >= 0) {
      auto track = controller.getSong().getMasterTrack().getChildByInternalId(controller.getFocusedClipTrackId());
      if (track && track->getType() == TrackType::INSTRUMENT_CONTROL) {
        if (name == "move-row-up") octaveUp(device_id);
        else octaveDown(device_id);
      }
      return true;
    }
    // Only meaningful in GridMode::LIVE otherwise - moves the overview's
    // own bar cursor via live_move_bar_callback_ (see that member's own
    // comment for why this doesn't scroll a local row window: Live
    // view's rows are a track's own clips). Outside LIVE,
    // "move-row-up"/"move-row-down" isn't this class's command at all
    // (PatternEditor's own row navigation owns it, reached via
    // UI::executeCommand()'s fallback, not through here) - declining lets
    // that happen normally.
    if (gridMode(device_id) != GridMode::LIVE) return false;
    if (live_move_bar_callback_) live_move_bar_callback_(name == "move-row-down" ? 1 : -1);
    return true;
  }
  if (name == "pad-next-track" || name == "pad-prev-track") {
    // Reserved while in Live View (per-device - see handleRawButton()'s
    // own CC95/96 comment): the cursor keys no longer switch this device
    // back to note-entry view, and no longer enter/exit Live View
    // either - CC95/96 are the only way there now, on whichever device
    // that's actually pressed on, independent of every other connected
    // Launchpad.
    if (gridMode(device_id) == GridMode::LIVE) return true;
    if (num_tracks <= 0) return true;

    // Also reserved - repurposed, not just declined - while this device
    // is actually showing a Live-View-focused clip's own step grid,
    // percussion or pitched alike (Controller::toggleDrumClipFocus()'s
    // own comment): paginates through the clip's own steps instead of
    // switching tracks, so the shared cursor stays put and Record Arm
    // alone is the way out of that editing session.
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
      if (!launchpad_io_) return true;
      // Nothing to page to when the clip fits one window: a press would only
      // re-clamp to where it already is, matching refreshLeds()'s own dark
      // LED for exactly this case. kStepGridScrollStep at a time, not a
      // whole window, so consecutive windows overlap and a performer can
      // follow where a press landed relative to before.
      auto max_base = std::max(0, length - kStepWindow);
      if (max_base <= 0) return true;
      auto & paged = deviceState(device_id);
      auto delta = (name == "pad-next-track" ? 1 : -1) * kStepGridScrollStep;
      paged.drum_edit_step_offset = std::clamp(paged.drum_edit_step_offset + delta, 0, max_base);
      return true;
    }

    // Moves the one shared cursor (fallback_track_index), not a
    // per-device assignment of this device's own - see
    // track_move_callback_'s own comment for why every connected
    // Launchpad, not just this one, follows the result.
    if (track_move_callback_) {
      track_move_callback_(LaunchpadLayout::advanceTrackIndex(fallback_track_index, name == "pad-next-track" ? 1 : -1, num_tracks));
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
    return LaunchpadLayout::drumPadNoteForPad(x, y);
  }
  if (x < 0 || x > 7 || y < 0 || y > 7) return -1;
  return resolveKeyboardNotes(song, device_id)[static_cast<size_t>(x + 8 * y)];
}

array<int, 64>
LaunchpadManager::resolveKeyboardNotes(const Song & song, int device_id) const {
  array<int, 64> notes;
  notes.fill(-1);
  auto edo_steps = LaunchpadLayout::edoSteps(song.getTuning());
  if (edo_steps <= 0) return notes; // every non-percussion Tuning is currently pitched

  // Scale degrees are offsets above the tonic's pitch class, so the octave
  // register is added here. Deliberately not "(octave - 4) * edo_steps":
  // the computer-keyboard tables (InputEvent.h) bake in a several-octaves-up
  // baseline for their lowest key, and one further octave on top of that
  // keeps the Launchpad's lowest pad comfortably audible.
  auto register_base = (octave(device_id) + 1) * edo_steps;
  auto max_index = LaunchpadLayout::scaleDegreeIndexForPad(7, 7);
  auto degrees = song.getScaleDegreesWindow(0, max_index + 1, true);
  if (degrees.size() != static_cast<size_t>(max_index + 1)) return notes;
  for (int y = 0; y < 8; y++) {
    for (int x = 0; x < 8; x++) {
      notes[static_cast<size_t>(x + 8 * y)] = degrees[static_cast<size_t>(LaunchpadLayout::scaleDegreeIndexForPad(x, y))] + register_base;
    }
  }
  return notes;
}

void
LaunchpadManager::handlePadEvent(LaunchpadPadEvent & ev, Controller & controller, int fallback_track_index, int edit_step_size) {
  last_active_device_ = ev.getDeviceIndex();
  auto & song = controller.getSong();
  auto & info = controller.getPlaybackInfo();

  auto track_ids = song.getPlayableTrackIds();

  auto device_id = ev.getDeviceIndex();

  // Send A/Send B/Send Main/Pan mode: the whole grid means something else
  // entirely while active (see LaunchpadManager::GridMode). For Send A/B/
  // Main, column x is track_ids[x] (the first 8 playable tracks, not this
  // device's assigned track), row y sets that track's send level. Pan is
  // transposed relative to the other three - a horizontal fader (row y is
  // the track, column x sets that track's azimuth), not a vertical one -
  // `track_index`/`position_index` below resolve that swap once, rather
  // than every call site picking getX()/getY() apart itself. Only a PRESS
  // does anything (Pan also reads a RELEASE, for its two-pad gestures);
  // AFTERTOUCH is swallowed too, never falling through to note-entry
  // below. CUSTOM is excluded here (unlike LIVE/
  // DRAW, which never reach this function at all - see UI::
  // handleLaunchpadPadEvent) since it addresses "this device's assigned
  // track" the same way NOTES does, not a fixed track-per-row-or-column
  // layout.
  auto grid_mode = gridMode(device_id);
  if (grid_mode != GridMode::NOTES && grid_mode != GridMode::CUSTOM) {
    bool is_pan = grid_mode == GridMode::PAN;
    int track_index = is_pan ? ev.getY() : ev.getX();
    int position_index = is_pan ? ev.getX() : ev.getY();
    if (ev.getKind() == LaunchpadPadEvent::PRESS && track_index < 8) {
      // The first 8 rows/columns (whichever selects the track here) must
      // always be usable, even in a song that doesn't have that many
      // tracks yet - a Launchpad's physical layout doesn't know or care
      // how many tracks currently exist, so auto-create plain
      // InstrumentTracks (the same default 't' key/add-track uses) up to
      // the pressed one rather than silently doing nothing.
      while (static_cast<int>(track_ids.size()) <= track_index) {
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
      auto track_id = track_ids[static_cast<size_t>(track_index)];
      // resolveSendFaderTarget()/resolveAzimuthFaderTarget() both need the
      // value as it actually is right now (to compare against the pressed
      // row for a same-row micro-value tap, or to compute the fresh-press
      // glide's own distance-scaled duration) - LeafTrack's own current
      // fields, not track_send_a/etc. (session-wide, computed once per
      // frame in refresh(), and this can run between two of those frames).
      auto track = song.getMasterTrack().getChildByInternalId(track_id);
      auto leaf_track = track ? dynamic_cast<LeafTrack *>(track) : nullptr;
      if (!leaf_track) return;
      if (grid_mode == GridMode::SEND_A) {
        auto & fader = fader_state_send_a_[track_id];
        bool is_micro_tap; float duration_seconds = 0.0f;
        float target_db = resolveSendFaderTarget(fader, linearToDb(leaf_track->getSends().a), position_index, ev.getVelocity(), is_micro_tap, duration_seconds);
        if (is_micro_tap) controller.setTrackSendA(track_id, target_db);
        else controller.glideTrackSendA(track_id, target_db, duration_seconds);
        recordFaderAutomationIfArmed(controller, fader, track_id, Command::sendAGlide(target_db, duration_seconds));
      } else if (grid_mode == GridMode::SEND_B) {
        auto & fader = fader_state_send_b_[track_id];
        bool is_micro_tap; float duration_seconds = 0.0f;
        float target_db = resolveSendFaderTarget(fader, linearToDb(leaf_track->getSends().b), position_index, ev.getVelocity(), is_micro_tap, duration_seconds);
        if (is_micro_tap) controller.setTrackSendB(track_id, target_db);
        else controller.glideTrackSendB(track_id, target_db, duration_seconds);
        recordFaderAutomationIfArmed(controller, fader, track_id, Command::sendBGlide(target_db, duration_seconds));
      } else if (grid_mode == GridMode::SEND_MAIN) {
        auto & fader = fader_state_send_main_[track_id];
        bool is_micro_tap; float duration_seconds = 0.0f;
        float target_db = resolveSendFaderTarget(fader, linearToDb(leaf_track->getSends().main), position_index, ev.getVelocity(), is_micro_tap, duration_seconds);
        if (is_micro_tap) controller.setTrackSendMain(track_id, target_db);
        else controller.glideTrackSendMain(track_id, target_db, duration_seconds);
        recordFaderAutomationIfArmed(controller, fader, track_id, Command::volumeGlide(target_db, duration_seconds));
      } else { // PAN
        handlePanPress(controller, device_id, track_id, position_index, ev.getVelocity());
      }
    } else if (is_pan && ev.getKind() == LaunchpadPadEvent::RELEASE && track_index < static_cast<int>(track_ids.size())) {
      handlePanRelease(controller, device_id, track_ids[static_cast<size_t>(track_index)], position_index);
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
  // opened with shift + pad) pins every connected NOTES-grid device
  // to it, regardless of wherever the shared cursor itself has since
  // wandered off to elsewhere in the terminal (Live View's own column,
  // PatternEditor, ...) - editing stays open until CC95 or the same
  // gesture closes it, never merely by looking at a different track meanwhile.
  if (controller.getFocusedClipTrackId() >= 0) track_id = controller.getFocusedClipTrackId();

  // CUSTOM has nothing built for it yet.
  if (grid_mode == GridMode::CUSTOM) return;

  // Split step view: while a clip is open for editing on a percussion or
  // pitched track (and nothing is recording a clip take), the top
  // four rows are the clip's steps for the selected sound and the bottom
  // four stay the playing surface. A press on the surface selects that
  // sound and then falls through to ordinary note entry below, so it
  // sounds and records exactly as it would without a clip open. The step
  // view only ever edits a clip, never the track's own background Pattern,
  // which has no pagination and spans the whole song.
  if (!controller.isAnyClipRecording() && controller.getFocusedClipTrackId() == track_id) {
    auto assigned_track = song.getMasterTrack().getChildByInternalId(track_id);
    if (assigned_track && (assigned_track->getType() == TrackType::PERCUSSION_CONTROL || assigned_track->getType() == TrackType::INSTRUMENT_CONTROL)) {
      if (ev.getY() >= LaunchpadLayout::kPlayRows) {
        auto pitched = assigned_track->getType() == TrackType::INSTRUMENT_CONTROL;
        handleStepGridPadEvent(ev, controller, stepEditNote(song, device_id, track_id), track_id, pitched);
        return;
      }
      if (ev.getKind() == LaunchpadPadEvent::PRESS) {
        auto selected = resolveNote(song, device_id, track_id, ev.getX(), ev.getY());
        if (selected >= 0) deviceState(device_id).selected_step_note = selected;
      }
    }
  }

  // Multi-track record fan-out: while any track is currently recording a
  // clip take, it supersedes the assigned track entirely for
  // ordinary chromatic note entry below - the take's own track and
  // instrument decide what plays, not whichever track the shared cursor
  // (or a pinned drum-clip focus) happens to be on. The lowest track_id
  // among Controller::getClipRecordingTrackIds(), since that set has
  // no other stable order - refresh()'s own identical override
  // (pinned_track_id) is this method's LED-rendering counterpart, so the
  // grid's own coloring always agrees with what a press here actually
  // does. Once resolved this way, note_value/tuning/percussion-ness below
  // all naturally follow from this one track, same as the ordinary case.
  auto recording_track_ids = controller.getClipRecordingTrackIds();
  int recording_reference_track_id = -1;
  for (auto candidate : recording_track_ids) {
    if (recording_reference_track_id < 0 || candidate < recording_reference_track_id) recording_reference_track_id = candidate;
  }
  // Unconditional once anything is recording, never merely "if numerically
  // smaller than the assigned track" - the whole point is that recording
  // supersedes the assigned track entirely, not only when it happens to
  // sort first; comparing straight against the still-assigned track_id
  // here missed every case where the recording track's own id was larger
  // (a very common case - a track armed/selected later in a session
  // usually has a larger id), silently leaving notes writing into the
  // assigned track (audible, but never recorded) instead.
  if (recording_reference_track_id >= 0) track_id = recording_reference_track_id;

  auto note_value = resolveNote(song, device_id, track_id, ev.getX(), ev.getY());
  if (note_value < 0) return; // unused percussion pad (row 7), or an unpitched/degenerate tuning

  // Every other currently-recording track that shares this one's own
  // percussion-ness (a percussion track's fixed GM-drum pad layout and a
  // pitched track's isomorphic one can't both be shown - or meaningfully
  // played - on the same grid at once, so an incompatible one is simply
  // excluded rather than receiving a nonsensical note) receives this
  // exact same resolved note alongside the assigned/reference track -
  // computed once here, reused by both PRESS and RELEASE below.
  bool track_is_percussion = false;
  {
    auto track = song.getMasterTrack().getChildByInternalId(track_id);
    track_is_percussion = track && track->getType() == TrackType::PERCUSSION_CONTROL;
  }
  vector<int> fan_out_track_ids;
  for (auto candidate : recording_track_ids) {
    if (candidate == track_id) continue;
    auto candidate_track = song.getMasterTrack().getChildByInternalId(candidate);
    bool candidate_is_percussion = candidate_track && candidate_track->getType() == TrackType::PERCUSSION_CONTROL;
    if (candidate_is_percussion == track_is_percussion) fan_out_track_ids.push_back(candidate);
  }

  auto current_delay = info.getCurrentDelay();
  auto & event_queue = controller.getPlaybackEventQueue();

  // Where a live press lands on the Live clock: raw by default, the row
  // it's in plus how far into it (the note's delay), or - with Record
  // Quantise on (Song::getRecordQuantize()) - the nearest row, no delay.
  // One shared decision for every target track below (the primary and
  // every fan-out one alike), not recomputed per track.
  auto & clip_player = controller.getClipPlayer();
  auto take_step = song.getRecordQuantize() ? clip_player.quantizedStep() : clip_player.rawStep();
  auto quantized_row = [&](int take_track_id) {
    return controller.ensureClipRecordingClip(take_track_id, take_step.step, take_step.bar_start);
  };

  if (ev.getKind() == LaunchpadPadEvent::PRESS) {
    // A clip take targeting this exact track writes into that
    // take's own clip directly, indexed by the live clock (this device's
    // NOTE grid is the only way a clip take ever receives notes at
    // all), never the arrangement position.
    // ensureClipRecordingClip() itself owns turning an absolute clock
    // step into a row relative to this take's own row 0 (established from
    // whichever step happens to be this take's *first* one - see its own
    // comment), so row 0 always means "the start of the bar this take
    // began in", not the instant of this specific press.
    bool clip_recording_here = controller.isClipRecording(track_id);
    auto row = info.getAbsolutePosition();
    if (clip_recording_here) {
      row = quantized_row(track_id);
    }
    auto & state = deviceState(device_id);

    Pattern * session_pattern = nullptr;
    int session_row = 0;
    if (clip_recording_here) {
      if (row < 0) return; // nothing actually armed for this track (shouldn't normally happen)
      auto & clips = song.getClips(track_id);
      auto clip_index = controller.getClipRecordingClipIndex(track_id);
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
      : resolveEditTarget(song, track_id, row, controller.getFocusedClip());
    if (clip_recording_here && !session_pattern) return; // nothing valid to write into (shouldn't normally happen)

    // The transport itself is already running by the time any press can
    // reach here - Record Arm starts it immediately on arming
    // (refresh()'s own note-capture-armed rising-edge handling) - except
    // for a clip take, which never starts it at all.

    // A live take writes into a real, individually-manageable Clip
    // instance, not directly into the track's own background Pattern - a
    // no-op once that clip already exists (or if a clip is focused, which
    // already resolves correctly without this). Re-resolves edit_target
    // immediately after: it was computed before this take could have just
    // placed a brand new instance here, so it would otherwise still point
    // at the (now superseded) background - the free-slot search and this
    // press's own write below both need the fresh one.
    if (!clip_recording_here && state.capture_enabled && info.isPlaying()) {
      controller.ensureNoteRecordingClip(auto_record_clip_ids_, track_id, row);
      edit_target = resolveEditTarget(song, track_id, row, controller.getFocusedClip());
    }

    // Whole-row replace semantics for a live take: idempotent (see its
    // own comment), so calling it defensively is safe - only actually
    // does anything the first time (row, track_id) is touched this
    // session. Cleared *before* the free-slot search just below, not
    // after - otherwise a column still holding an old, about-to-be-
    // erased note reads as "taken" and gets skipped past, when the old
    // note is actually gone (or about to be, from this same call) and
    // the new one should be free to land in the very first column.
    if (state.capture_enabled && auto_started_playback_) controller.ensureRowCleared(auto_record_cleared_rows_, row, track_id);

    // Free-slot search (mirrors Arrangement::pushNote), deliberately not
    // "map size": a column count collides on non-LIFO release order,
    // which is the common case for a chordally-played grid controller. Computed unconditionally (even with Capture off,
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

    // clip_recording_here is its own permission to write - an active
    // take is armed independently of the ordinary (non-Live-View)
    // capture_enabled flag, and must not depend on it (Controller::
    // isTrackArmed()'s own doc comment: "toggle-record-arm" no longer
    // touches capture_enabled at all for a clip take).
    if (clip_recording_here || state.capture_enabled) {
      // A clip take uses take_step's own delay (0 once quantized),
      // not current_delay, which reads the global transport's own delay
      // tracking - meaningless while it never advances during one.
      Note note(note_value, velocity, clip_recording_here ? static_cast<short>(take_step.delay) : current_delay);
      Song::Edit edit(song, "record note");
      edit_target.pattern->setNote(edit_target.effective_row, note_column, note);
    }

    if (controller.isMonitoring(track_id)) {
      event_queue.push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::PLAY_NOTE, controller.getActiveBufferName(), track_id, note_column, note_value, velocity));
    }

    // Multi-track record fan-out: every other compatible currently-
    // recording track (fan_out_track_ids, computed once above) receives
    // this exact same resolved note alongside the primary track - always
    // written (session recording is its own permission, same reasoning as
    // just above), always quantized the same way, each independently
    // finding its own free column and its own take's own current row.
    for (auto fan_out_track_id : fan_out_track_ids) {
      auto fan_out_row = quantized_row(fan_out_track_id);
      if (fan_out_row < 0) continue;
      auto & fan_out_clips = song.getClips(fan_out_track_id);
      auto fan_out_clip_index = controller.getClipRecordingClipIndex(fan_out_track_id);
      if (fan_out_clip_index < 0 || fan_out_clip_index >= static_cast<int>(fan_out_clips.size())) continue;
      auto & fan_out_clip = fan_out_clips[static_cast<size_t>(fan_out_clip_index)];
      auto fan_out_session_row = fan_out_row % std::max(1, fan_out_clip.getLength());
      auto & fan_out_pattern = fan_out_clip.getLeafPattern();
      auto & fan_out_notes = fan_out_pattern.getNotes(fan_out_session_row);
      int fan_out_column = 0;
      while ((fan_out_column < static_cast<int>(fan_out_notes.size()) && fan_out_notes[static_cast<size_t>(fan_out_column)].isDefined()) ||
             isColumnLiveHeld(fan_out_track_id, fan_out_column)) {
        fan_out_column++;
      }
      Song::Edit edit(song, "record note");
      fan_out_pattern.setNote(fan_out_session_row, fan_out_column, Note(note_value, velocity, static_cast<short>(take_step.delay)));
      if (controller.isMonitoring(fan_out_track_id)) {
        event_queue.push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::PLAY_NOTE, controller.getActiveBufferName(), fan_out_track_id, fan_out_column, note_value, velocity));
      }
      recordActiveNote(device_id, ev.getX(), ev.getY(), {fan_out_column, fan_out_row, fan_out_track_id});
    }

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
    auto held_ptr = findActiveNotes(device_id, ev.getX(), ev.getY());
    if (!held_ptr) return;
    auto held_notes = *held_ptr; // copied out - clearActiveNotes() below invalidates the pointer
    clearActiveNotes(device_id, ev.getX(), ev.getY());
    auto & state = deviceState(device_id);

    // One press can hold several targets at once now (multi-track record
    // fan-out - the PRESS branch's own comment); released together, same
    // as they were pressed together. If any of them was a Live View
    // take, none of the ordinary (non-recording) release handling below
    // applies to any of them - PRESS's own track_id override means a
    // press is either entirely session-recording (the primary track_id
    // itself already redirected to one) or entirely ordinary, never a mix.
    bool any_clip_recording = false;
    for (auto & held : held_notes) {
      controller.endNotePressure(held.track_id, held.note_column);
      // Always silence the live-audition voice.
      event_queue.push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::STOP_NOTE, controller.getActiveBufferName(), held.track_id, held.note_column));

      // A clip take writes its own release off into the take's
      // clip, keyed off the same live clock step (rounded the same way
      // the PRESS branch above rounds it) rather than the arrangement
      // row.
      bool clip_recording_here = controller.isClipRecording(held.track_id);
      if (clip_recording_here) {
        any_clip_recording = true;
        // clip_recording_here is its own permission to write,
        // independent of capture_enabled - see the PRESS branch's own
        // identical reasoning.
        auto release_row = quantized_row(held.track_id);
        auto & clips = song.getClips(held.track_id);
        auto clip_index = controller.getClipRecordingClipIndex(held.track_id);
        // Same "not the row the note itself is on" rule as the ordinary
        // performance-recording branch below - a single Pattern row can't
        // hold both a note and its own off.
        if (release_row >= 0 && release_row != held.row &&
            clip_index >= 0 && clip_index < static_cast<int>(clips.size())) {
          auto & clip = clips[static_cast<size_t>(clip_index)];
          Song::Edit edit(song, "record note release");
          clip.getLeafPattern().setNote(release_row % std::max(1, clip.getLength()), held.note_column, Note(0, 0, static_cast<short>(take_step.delay)));
        }
      } else if (state.capture_enabled && info.isPlaying()) {
        // Live performance recording: write an explicit OFF at the row the
        // transport has since reached, mirroring handleMidiEvent's NOTE_OFF -
        // UNLESS that's still the same row the note itself is on. In this
        // tracker's own pattern model (a single line can't hold both a note
        // and its own note-off), a release fast enough to land before the
        // row has advanced must not be recorded as an off, or it would
        // instantly erase the note it belongs to.
        auto release_row = info.getAbsolutePosition();
        if (release_row != held.row) {
          controller.writeReleaseOff(auto_record_cleared_rows_, auto_started_playback_, release_row, held.track_id, held.note_column, current_delay);
        }
      }
    }
    // Step entry: advance once the whole chord gesture has been released
    // on *this* device (not per pad - see the PRESS branch; and scoped to
    // this device, not every connected Launchpad, so one device's chord
    // release doesn't prematurely advance while another device is still
    // mid-chord), so the next tap/chord lands on a fresh row instead of
    // piling onto this one. Only reachable at all with Capture off (real
    // playback, engaged by the PRESS branch's own auto-play push, is the
    // norm whenever Capture is on) and never for a clip take (a
    // live take has no transport position of its own to advance) - a
    // stopped, pure-audition release must not touch the cursor either.
    if (!any_clip_recording && state.capture_enabled && !info.isPlaying() && !hasAnyActiveNotes(device_id)) {
      controller.moveEditPosition(edit_step_size);
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

    auto held_ptr = findActiveNotes(device_id, ev.getX(), ev.getY());
    if (!held_ptr || held_ptr->empty()) return; // no held note(s) to modulate

    bool take_held = false;
    for (auto & held : *held_ptr) take_held = take_held || controller.isClipRecording(held.track_id);
    bool write_pressure = deviceState(device_id).capture_enabled || take_held;

    for (auto & held : *held_ptr) {
      int row = 0;
      int delay = current_delay;
      int held_track = held.track_id;
      int column = held.note_column;
      int note_row = held.row;
      Controller::PressureWriter write_row;
      Controller::PressureRowSource current_row;
      if (controller.isClipRecording(held_track)) {
        // A clip take writes into its own clip at the live clock's
        // row, like its release does - never on the row the note itself is
        // on, which would overwrite the note.
        row = quantized_row(held_track);
        delay = take_step.delay;
        if (write_pressure) {
          write_row = [&controller, held_track, column, note_row](int r, short p) {
            auto & clips = controller.getSong().getClips(held_track);
            auto clip_index = controller.getClipRecordingClipIndex(held_track);
            if (r < 0 || r == note_row || clip_index < 0 || clip_index >= static_cast<int>(clips.size())) return;
            auto & clip = clips[static_cast<size_t>(clip_index)];
            auto & pattern = clip.getLeafPattern();
            auto clip_row = r % std::max(1, clip.getLength());
            auto note = pattern.getNote(clip_row, column);
            if (note.isDefined() && !note.isAftertouch()) return;
            note.setDelay(0);
            note.setVelocity(p);
            pattern.setNote(clip_row, column, note);
          };
          current_row = [&controller, held_track]() {
            if (!controller.isClipRecording(held_track)) return -1;
            auto & player = controller.getClipPlayer();
            auto step = controller.getSong().getRecordQuantize() ? player.quantizedStep() : player.rawStep();
            return controller.ensureClipRecordingClip(held_track, step.step, step.bar_start);
          };
        }
      } else {
        // While playing (the norm whenever Capture is on - see the PRESS
        // branch's auto-play push), modulate the currently-sounding row
        // (transport has moved on, matching handleMidiEvent); while stopped
        // (only reachable with Capture on if the auto-play push hasn't been
        // processed by the Player thread yet), modulate the row the note
        // actually landed on.
        row = info.isPlaying() ? info.getAbsolutePosition() : held.row;
        if (write_pressure) {
          // Not on the note's own row: a row can't hold the note and its
          // aftertouch, and clearing it would erase the note.
          write_row = [this, &controller, held_track, column, note_row](int r, short p) {
            if (r == note_row) return;
            // Clear before reading, not just before writing - otherwise the
            // isDefined() check could pick up stale pre-existing data from
            // before the row was cleared for the live take.
            if (auto_started_playback_) controller.ensureRowCleared(auto_record_cleared_rows_, r, held_track);
            controller.applyNotePressure(r, held_track, column, p, 0);
          };
          current_row = [&controller]() {
            auto & playback = controller.getPlaybackInfo();
            return playback.isPlaying() ? playback.getAbsolutePosition() : -1;
          };
        }
      }
      // Live modulation always happens, whether or not Capture records it,
      // and plays the same row average that gets recorded.
      Song::Edit edit(song, "note pressure");
      if (!write_pressure) edit.discard();
      auto pressure = controller.notePressure(row, held_track, column, static_cast<short>(ev.getVelocity()), delay, write_row, current_row);
      event_queue.push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::NOTE_PRESSURE, controller.getActiveBufferName(), held_track, column, note_value, pressure));
    }
  }
}

void
LaunchpadManager::handleLivePadEvent(const LaunchpadPadEvent & ev, Controller & controller) {
  last_active_device_ = ev.getDeviceIndex();
  auto & state = deviceState(ev.getDeviceIndex());

  // CC91 ("move-row-up") held as a shift modifier opens this pad's own
  // clip for direct step-grid editing instead of triggering/assigning it
  // - see DeviceState::row_up_shift_held's own comment. Resolved on this
  // pad's own release, not its press: a press while shift is held is
  // recorded (row_up_shift_pending_pad) and swallowed outright - never
  // falls through to the ordinary meaning below, even on a clip that
  // turns out not to be step-sequenced (a no-op then, not a trigger) -
  // and only the matching release actually calls Controller::
  // toggleDrumClipFocus(), so a press that's abandoned (shift released
  // first, the pad dragged off) never has to be undone. Works regardless
  // of Record Arm/this track's own armed state, unlike an ordinary
  // Live-View press - it's a completely different physical gesture (a
  // distinct button combo), not competing with whatever a plain press on
  // this same pad already means while armed. Only reachable here, not
  // from the step grid a successful open switches a device to - the
  // same shift+pad combo closes it again only once back on the plain
  // Live grid (CC95), since the step grid's own pads mean lane/step,
  // not (track, clip index), and have nothing to shift-combine with at
  // all.
  // Delete held (shift + Pan): every press is swallowed; a populated slot
  // loses its clip, an empty one its stop button. Instant unless the clip
  // is sounding on a running transport (ClipPlayer::deleteClip()).
  if (state.delete_held) {
    if (ev.getKind() != LaunchpadPadEvent::PRESS) return;
    auto column = ev.getX();
    if (column < 0 || column >= static_cast<int>(live_.track_ids.size())) return;
    controller.getClipPlayer().deleteClip(live_.track_ids[static_cast<size_t>(column)], 7 - ev.getY());
    return;
  }
  // Quantise held (shift + Send A): a press on a populated slot quantizes
  // that clip; every press is swallowed.
  if (state.quantize_held) {
    if (ev.getKind() != LaunchpadPadEvent::PRESS) return;
    state.quantize_used = true;
    auto column = ev.getX();
    if (column < 0 || column >= static_cast<int>(live_.track_ids.size())) return;
    auto track_id = live_.track_ids[static_cast<size_t>(column)];
    if (quantizeClip(controller.getSong(), track_id, 7 - ev.getY())) {
      controller.getUIEventQueue().push(std::make_unique<LogEvent>("Quantised clip"));
    } else {
      controller.getUIEventQueue().push(std::make_unique<LogEvent>("Quantise: nothing to quantise there"));
    }
    return;
  }
  // Duplicate held (shift + Volume): a press on a populated slot copies that
  // clip into the slot below it, overwriting; every press is swallowed.
  if (state.duplicate_held) {
    if (ev.getKind() != LaunchpadPadEvent::PRESS) return;
    auto column = ev.getX();
    if (column < 0 || column >= static_cast<int>(live_.track_ids.size())) return;
    auto slot = duplicateClip(controller.getSong(), live_.track_ids[static_cast<size_t>(column)], 7 - ev.getY());
    if (slot < 0) controller.getUIEventQueue().push(std::make_unique<LogEvent>("Duplicate: nothing to copy there"));
    return;
  }
  if (ev.getKind() == LaunchpadPadEvent::PRESS && state.row_up_shift_held) {
    // Marked combined immediately, not deferred to the release below -
    // CC91's own release (handleShiftButton()) can land before this
    // pad's does, and needs to already know not to fire "move-row-up" in
    // that case.
    state.row_up_shift_combined = true;
    state.row_up_shift_pending_pad = true;
    state.row_up_shift_pending_x = ev.getX();
    state.row_up_shift_pending_y = ev.getY();
    return;
  }
  if (ev.getKind() == LaunchpadPadEvent::RELEASE && state.row_up_shift_pending_pad &&
      ev.getX() == state.row_up_shift_pending_x && ev.getY() == state.row_up_shift_pending_y) {
    state.row_up_shift_pending_pad = false;
    auto track_index = ev.getX();
    if (track_index < 0 || track_index >= static_cast<int>(live_.track_ids.size())) return;
    auto track_id = live_.track_ids[static_cast<size_t>(track_index)];
    // Same y-flip as refresh()'s own clip_colors computation - y=0 is
    // the bottom-left pad, so y=7 is that track's first clip.
    // toggleDrumClipFocus() itself is what shows the step grid empty
    // rather than declining outright for a lane-less PercussionTrack -
    // see its own comment - so its return value is a pure "did this
    // address a PercussionTrack clip at all" check, nothing further to do
    // here either way.
    // Selecting works on any slot, empty ones included; shift + Note opens
    // the selected clip for step editing.
    selected_track_id_ = track_id;
    selected_clip_index_ = 7 - ev.getY();
    controller.selectClipSlot(selected_track_id_, selected_clip_index_);
    return;
  }

  if (ev.getKind() != LaunchpadPadEvent::PRESS) return;

  // No column scroll yet (see LiveWindow's own comment).
  auto track_index = ev.getX();
  if (track_index < 0 || track_index >= static_cast<int>(live_.track_ids.size())) return;
  auto track_id = live_.track_ids[static_cast<size_t>(track_index)];

  controller.getClipPlayer().triggerClip(track_id, 7 - ev.getY());
}

void
LaunchpadManager::triggerSceneRow(Controller & controller, int row) {
  // Same y-flip Live View's own columns use (handleLivePadEvent()) -
  // row 0 (bottom) is clip index 7, row 7 (top) is clip index 0.
  controller.getClipPlayer().launchScene(7 - row, live_.track_ids);
}

void
LaunchpadManager::startAssignPlayback(Controller & controller) {
  // Controller::startAutoRecordPlayback(), not startAutoRecordSession():
  // the latter also mutes pattern-driven scheduling, which is the only
  // way a clip placed into the arrangement is heard.
  controller.startAutoRecordPlayback(auto_started_playback_);
}

int
LaunchpadManager::stepEditNote(const Song & song, int device_id, int track_id) const {
  auto * state = findDeviceState(device_id);
  if (state && state->selected_step_note >= 0) return state->selected_step_note;
  return resolveNote(song, device_id, track_id, 0, 0); // the first pad: the kick, or the tonic
}

void
LaunchpadManager::handleStepGridPadEvent(LaunchpadPadEvent & ev, Controller & controller, int note, int track_id, bool pitched) {
  auto step = LaunchpadLayout::stepForPad(ev.getX(), ev.getY());
  if (step < 0 || note < 0) return;

  auto & event_queue = controller.getPlaybackEventQueue();

  if (ev.getKind() == LaunchpadPadEvent::PRESS) {
    auto & song = controller.getSong();
    // A clip can be longer than the 32 steps shown: this device's step
    // offset (scrolled with the pad-prev-track/pad-next-track buttons) picks the
    // window, and a step past the clip's end does nothing.
    auto length = focusedDrumClipLength(song, track_id, controller.getFocusedClip());
    auto max_offset = std::max(0, length - kStepWindow);
    auto offset = std::clamp(deviceState(ev.getDeviceIndex()).drum_edit_step_offset, 0, max_offset);
    auto row = offset + step;
    if (length < 0 || row >= length) return;

    auto edit_target = resolveEditTarget(song, track_id, row, controller.getFocusedClip());
    auto & row_notes = edit_target.pattern->getNotes(edit_target.effective_row);
    // Identified by value, not by column, so a step typed in the pattern
    // editor or pasted from elsewhere toggles just the same.
    int existing_column = -1;
    for (size_t i = 0; i < row_notes.size(); i++) {
      auto & n = row_notes[i];
      if (n.isDefined() && !n.isOff() && !n.isAftertouch() && n.getValue() == note) { existing_column = static_cast<int>(i); break; }
    }
    bool was_hit = existing_column >= 0;

    // A pitched note lasts one step: its note-off sits in the same column
    // on the next row, when that cell is free (or already the off).
    EditTarget off_target { nullptr, 0 };
    if (pitched && row + 1 < length) off_target = resolveEditTarget(song, track_id, row + 1, controller.getFocusedClip());

    Song::Edit edit(song, "toggle step");
    if (was_hit) {
      edit_target.pattern->deleteNote(edit_target.effective_row, existing_column);
      if (off_target.pattern) {
        auto & next = off_target.pattern->getNotes(off_target.effective_row);
        if (existing_column < static_cast<int>(next.size()) && next[static_cast<size_t>(existing_column)].isOff() && next[static_cast<size_t>(existing_column)].getValue() == note) {
          off_target.pattern->deleteNote(off_target.effective_row, existing_column);
        }
      }
    } else {
      auto column = edit_target.pattern->pushNote(edit_target.effective_row, Note(note, static_cast<short>(constants::DEFAULT_VELOCITY)));
      if (off_target.pattern) {
        auto & next = off_target.pattern->getNotes(off_target.effective_row);
        if (column >= static_cast<int>(next.size()) || !next[static_cast<size_t>(column)].isDefined()) {
          off_target.pattern->setNote(off_target.effective_row, column, Note(note, 0));
        }
      }
    }

    // Only a step just set is auditioned - one being removed has nothing
    // left to want to hear. Fixed velocity: no per-step velocity here.
    if (!was_hit) {
      auto velocity = static_cast<short>(constants::DEFAULT_VELOCITY);
      event_queue.push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::PLAY_NOTE, controller.getActiveBufferName(), track_id, note, note, velocity));
    }
  } else if (ev.getKind() == LaunchpadPadEvent::RELEASE) {
    event_queue.push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::STOP_NOTE, controller.getActiveBufferName(), track_id, note));
  }
}

void
LaunchpadManager::triggerAuditionStep(const Song & song, int track_id, Controller & controller, int step) {
  auto track = song.getMasterTrack().getChildByInternalId(track_id);
  if (!track) return;

  auto & event_queue = controller.getPlaybackEventQueue();

  // Idle auditioning only ever previews an explicitly focused clip
  // (Controller::getFocusedClip()), never the background/whatever
  // instance happens to be active there - "nothing selected" means
  // silence, not an uncontrolled loop of whatever was last recorded on
  // this track. resolveReadTarget()'s own is_focused_override tells the
  // two apart; falling through to ordinary resolution (no focus, or a
  // focus that belongs to some other track) means there's nothing to
  // preview here right now.
  auto read_target = resolveReadTarget(song, track_id, step, controller.getFocusedClip());
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
  for (auto & [ pos, notes ] : state->active_notes) {
    for (auto & note : notes) {
      if (find(track_ids.begin(), track_ids.end(), note.track_id) == track_ids.end()) {
        track_ids.push_back(note.track_id);
      }
    }
  }
  for (auto track_id : track_ids) {
    event_queue.push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::CHANNEL_PRESSURE, controller.getActiveBufferName(), track_id, ev.getVelocity()));
  }
}

void
LaunchpadManager::refreshLeds(int device_id, DeviceState & state) {
  vector<LaunchpadProtocol::PadColor> colors;

  if (state.grid_mode == GridMode::LIVE) {
    // Fully resolved already (identity hue, off where a track has no clip
    // in that row) - see refresh()'s own clip_colors computation and
    // DeviceState::clip_colors's own comment. Checked first, ahead of
    // every other branch below: LIVE is a hard override forced on by
    // refresh() itself, not a per-device toggle a user could combine with
    // Send/Pan/Draw/drum-machine display. clip_highlight (parallel,
    // same indexing) overrides a triggered/queued pad's own static color
    // with a real hardware flash/pulse instead - see its own constants'
    // comment for why that has to be a fixed palette index rather than
    // this pad's own identity hue.
    for (int y = 0; y < 8; y++) {
      for (int x = 0; x < 8; x++) {
        size_t i = static_cast<size_t>(y * 8 + x);
        auto led = LaunchpadProtocol::padToNoteNumber(x, y);
        LaunchpadProtocol::PadColor pad;
        pad.led_index = led;
        // The pad currently pending a shift+pad combo (DeviceState::
        // row_up_shift_pending_pad, LaunchpadManager::
        // handleLivePadEvent()) overrides whatever it would otherwise
        // show - bright white, the same color CC91's own LED shows while
        // held, so the two lit pads visually pair up and confirm exactly
        // what releasing will do. Per-device, unlike clip_highlight/
        // clip_colors below (computed once, identical for every
        // connected device) - only this device's own held press shows it.
        if (state.row_up_shift_pending_pad && x == state.row_up_shift_pending_x && y == state.row_up_shift_pending_y) {
          pad.r = pad.g = pad.b = 127;
          colors.push_back(pad);
          continue;
        }
        switch (state.clip_highlight[i]) {
        case ClipHighlight::PLAYING:
          pad.type = LaunchpadProtocol::LightingType::PULSE;
          pad.palette = LAUNCHPAD_CLIP_GREEN_PALETTE_BRIGHT;
          break;
        case ClipHighlight::QUEUED:
          pad.type = LaunchpadProtocol::LightingType::FLASH;
          pad.flash_to = LAUNCHPAD_CLIP_GREEN_PALETTE_BRIGHT;
          pad.flash_from = LAUNCHPAD_CLIP_GREEN_PALETTE_DIM;
          break;
        case ClipHighlight::PAUSED:
          pad.r = LAUNCHPAD_CLIP_PAUSED.r;
          pad.g = LAUNCHPAD_CLIP_PAUSED.g;
          pad.b = LAUNCHPAD_CLIP_PAUSED.b;
          break;
        // An armed track's own red overlay - RECORDING/RECORD_QUEUED are
        // the exact same pulse/flash treatment as PLAYING/QUEUED above,
        // just red instead of green; RECORD_STOPPING reuses RECORD_
        // QUEUED's own flash rather than a fifth color (ClipHighlight's
        // own comment).
        case ClipHighlight::RECORDING:
          pad.type = LaunchpadProtocol::LightingType::PULSE;
          pad.palette = LAUNCHPAD_CLIP_RED_PALETTE_BRIGHT;
          break;
        case ClipHighlight::RECORD_QUEUED:
        case ClipHighlight::RECORD_STOPPING:
          pad.type = LaunchpadProtocol::LightingType::FLASH;
          pad.flash_to = LAUNCHPAD_CLIP_RED_PALETTE_BRIGHT;
          pad.flash_from = LAUNCHPAD_CLIP_RED_PALETTE_DIM;
          break;
        // A static dim red, same as an idle clip's own identity color
        // below is static - an empty slot has no identity hue of its own
        // to show, so this is the one state here with no unarmed
        // equivalent at all rather than a red version of one.
        case ClipHighlight::ARMED_EMPTY:
          pad.r = LAUNCHPAD_TRACK_PICKER_RECORD_ARM_DIM.r;
          pad.g = LAUNCHPAD_TRACK_PICKER_RECORD_ARM_DIM.g;
          pad.b = LAUNCHPAD_TRACK_PICKER_RECORD_ARM_DIM.b;
          break;
        case ClipHighlight::NONE: {
          auto & c = state.clip_colors[i];
          pad.r = static_cast<uint8_t>(c.getRed() / 2);
          pad.g = static_cast<uint8_t>(c.getGreen() / 2);
          pad.b = static_cast<uint8_t>(c.getBlue() / 2);
          break;
        }
        }
        colors.push_back(pad);
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
  } else if (inNumberView(state)) {
    // Tempo/Swing view: the value as a number (LaunchpadLayout::renderNumber()),
    // its tens digit white and the rest in the view's colour.
    constexpr Rgb kTempoColor{0, 50, 127}, kSwingColor{127, 55, 0}, kWhite{127, 127, 127};
    auto side = state.grid_mode == GridMode::TEMPO ? kTempoColor : kSwingColor;
    auto number = LaunchpadLayout::renderNumber(state.grid_mode == GridMode::TEMPO ? cached_tempo_ : cached_swing_);
    for (int y = 0; y < 8; y++) {
      for (int x = 0; x < 8; x++) {
        auto pixel = number[static_cast<size_t>(7 - y)][static_cast<size_t>(x)]; // renderNumber() is top-first
        Rgb c = pixel == LaunchpadLayout::NumberPixel::MIDDLE ? kWhite : pixel == LaunchpadLayout::NumberPixel::SIDE ? side : Rgb{0, 0, 0};
        colors.push_back({LaunchpadProtocol::padToNoteNumber(x, y), c.r, c.g, c.b});
      }
    }
  } else if (state.grid_mode != GridMode::NOTES && state.grid_mode != GridMode::CUSTOM) {
    // Send/Pan mode: the whole grid means something else entirely - each
    // column is one of the first 8 root tracks. Send A/B/Main fill
    // bottom-up as a bargraph of that track's current send level
    // (sendLinearToRow, its own dB curve's inverse) - a magnitude (Send
    // Main's own zero-config value, 1.0/0dB, so shows fully filled until
    // turned down). Pan is a horizontal bar growing from the centre
    // toward the side the azimuth points to (panBarPads()). No
    // active/inactive feedback needed
    // on the mode buttons themselves (see refreshLeds' extra-button
    // section below) - this repaint *is* the confirmation the mode
    // actually changed. Send A/B/Main each show one fixed hue for every
    // column (which track it is is already obvious from context - the
    // whole grid is one bargraph per column); Pan instead shows each
    // column in that track's own identity color (state.track_colors),
    // the same "which track" cue Live View's columns already give -
    // its rows are tracks, so nothing else tells them apart.
    bool is_pan = state.grid_mode == GridMode::PAN;
    auto & values = state.grid_mode == GridMode::SEND_A ? state.track_send_a
                  : state.grid_mode == GridMode::SEND_B ? state.track_send_b
                  : state.grid_mode == GridMode::SEND_MAIN ? state.track_send_main
                  : state.track_azimuth;
    // Parallel to values above - see DeviceState::track_send_main_micro's
    // own comment.
    auto & micro_values = state.grid_mode == GridMode::SEND_A ? state.track_send_a_micro
                         : state.grid_mode == GridMode::SEND_B ? state.track_send_b_micro
                         : state.track_send_main_micro;
    // Parallel to values above - see DeviceState::track_send_main_row's
    // own comment.
    auto & pressed_rows = state.grid_mode == GridMode::SEND_A ? state.track_send_a_row
                         : state.grid_mode == GridMode::SEND_B ? state.track_send_b_row
                         : state.track_send_main_row;
    // Pan ignores this - see the comment above on why it uses each
    // column's own track_colors entry instead. Same constants the opener
    // button's own LED uses (LAUNCHPAD_MIXER_*_BRIGHT above) - one hue per
    // fader, not independently picked here.
    Rgb base = state.grid_mode == GridMode::SEND_A ? LAUNCHPAD_MIXER_SEND_A_BRIGHT
             : state.grid_mode == GridMode::SEND_B ? LAUNCHPAD_MIXER_SEND_B_BRIGHT
             : LAUNCHPAD_MIXER_VOLUME_BRIGHT; // SEND_MAIN
    // `pressed_row`/`_row`-suffixed arrays above all speak of "row" as the
    // position along that parameter's own measurement axis (its FaderState
    // origin, shared with the live-press code in handlePadEvent()) - a
    // physical grid row for Send A/B/Main, but a physical *column* for Pan
    // once `track_index`/`position_index` below resolve the same rotation
    // handlePadEvent() itself already applies (see that method's own
    // comment) into an actual (x, y) pad address.
    for (int track_index = 0; track_index < 8; track_index++) {
      // A track index past the real track count has no value to show at
      // all - values[track_index] is just a stale/default 0.0f there, not
      // "this track's level is 0" - go fully dark rather than painting
      // whatever that default happens to map to (row 0 for Send A/B,
      // dead-center for Pan).
      bool has_track = track_index < state.grid_track_count;
      // Color's own 0-255 range, rescaled to the hardware's 0-127
      // velocity-scaled one - same conversion hslToRgb() already uses.
      auto & identity = state.track_colors[static_cast<size_t>(track_index)];
      Rgb column_base = is_pan ? Rgb{static_cast<uint8_t>(identity.getRed() * 127 / 255),
        static_cast<uint8_t>(identity.getGreen() * 127 / 255), static_cast<uint8_t>(identity.getBlue() * 127 / 255)} : base;
      // Pan shows a bar growing from the centre (panBarPads()); the sends
      // fill up from the bottom row to the current position.
      std::array<LaunchpadLayout::PanPad, 8> pan_pads{};
      int lit_position = 0;
      if (is_pan) {
        pan_pads = LaunchpadLayout::panBarPads(values[static_cast<size_t>(track_index)]);
      } else {
        lit_position = sendLinearToRow(values[static_cast<size_t>(track_index)]);
        // lit_position above rounds to the *nearest* position - correct
        // for a value that arrived some other way, but wrong once a
        // micro-value cycle pushes the live value more than halfway
        // toward the position above, which would otherwise flip the
        // displayed "current" one there and bleed the micro-value's own
        // brightness scale onto a pad nobody actually pressed. Prefer the
        // position this fader was actually last pressed to instead, but
        // only when the naively-derived one is still consistent with it
        // (equal to it, or its immediate neighbor toward the next
        // position) - a value that's since moved somewhere else entirely
        // (automation, a different device, a stale press from long
        // before) falls back to the plain derived position rather than
        // showing a wrong one.
        int pressed_position = pressed_rows[static_cast<size_t>(track_index)];
        if (pressed_position >= 0) {
          int next_position = std::min(7, pressed_position + 1);
          if (lit_position == pressed_position || lit_position == next_position) lit_position = pressed_position;
        }
      }
      for (int position_index = 0; position_index < 8; position_index++) {
        bool lit = has_track && (is_pan ? pan_pads[static_cast<size_t>(position_index)] != LaunchpadLayout::PanPad::OFF : position_index <= lit_position);
        Rgb color = {0, 0, 0};
        if (lit && is_pan) {
          auto pad = pan_pads[static_cast<size_t>(position_index)];
          color = column_base;
          if (pad != LaunchpadLayout::PanPad::TIP) {
            auto hsl = rgbToHsl(column_base);
            hsl.l *= pad == LaunchpadLayout::PanPad::CENTER ? kPanCenterScale : kPanBarScale;
            color = hslToRgb(hsl);
          }
        } else if (lit) {
          color = column_base;
          // Only the fader's own top/current pad shows the micro-value
          // offset (a bargraph's filled-in rows below it stay at plain
          // full brightness, same as before micro-values existed), and
          // only once one is actually active (micro_step > 0 - a plain,
          // non-repeated press leaves this at its own unscaled base
          // color, matching every other lit position) - dimmest
          // (LAUNCHPAD_FADER_MICRO_MIN_SCALE) at micro_step 1, back up to
          // unscaled base at micro_step 2 (the highest micro-value, right
          // before wrapping back to 1).
          int micro_step = micro_values[static_cast<size_t>(track_index)];
          if (position_index == lit_position && micro_step > 0) {
            auto hsl = rgbToHsl(column_base);
            float scale = LAUNCHPAD_FADER_MICRO_MIN_SCALE + (1.0f - LAUNCHPAD_FADER_MICRO_MIN_SCALE) *
              static_cast<float>(micro_step - 1);
            hsl.l *= scale;
            color = hslToRgb(hsl);
          }
        }
        // Pan is transposed relative to Send A/B/Main (row y is the
        // track, column x sets that track's azimuth) - see
        // handlePadEvent()'s own comment for why.
        auto led = is_pan ? LaunchpadProtocol::padToNoteNumber(position_index, track_index)
                           : LaunchpadProtocol::padToNoteNumber(track_index, position_index);
        colors.push_back({led, color.r, color.g, color.b});
      }
    }
  } else if (state.grid_mode == GridMode::CUSTOM) {
    // Nothing built for CUSTOM yet (GridMode::CUSTOM's own comment).
    for (int y = 0; y < 8; y++) {
      for (int x = 0; x < 8; x++) colors.push_back({LaunchpadProtocol::padToNoteNumber(x, y), 0, 0, 0});
    }
  } else {
    // NOTES: the playing surface - the 4x4 GM kit on a percussion track, the
    // scale keyboard on a pitched one - and, while a clip is open for
    // editing (show_step_grid), the top four rows as its 32 steps. The
    // pad pressed last is the selected sound: white, and the one the step
    // rows show. Lit steps are green; unlit-but-real steps a faint dark
    // outline; steps past the clip's end stay black. The playhead step gets
    // the same lightness-only boost padColor() uses for note loudness
    // (idle -> active luminosity).
    constexpr Rgb kStepLitColor { 0, 110, 20 };
    constexpr Rgb kStepUnlitColor { 12, 12, 12 };
    constexpr Rgb kSelectedColor { 127, 127, 127 };
    bool percussion = state.tuning == Tuning::PERCUSSION;
    auto edo_steps = LaunchpadLayout::edoSteps(state.tuning);
    LaunchpadLayout::Basis basis;
    vector<LaunchpadLayout::PadClassification> levels;
    if (!percussion && edo_steps > 0) {
      basis = LaunchpadLayout::computeBasis(edo_steps);
      // Computed once per refresh, not per pad.
      if (!basis.degenerate) levels = LaunchpadLayout::computeConsonanceLevels(basis, edo_steps);
    }
    auto tonic = edo_steps > 0 && state.key >= 0 ? ((state.key % edo_steps) + edo_steps) % edo_steps : 0;
    int selected = -1;
    if (state.show_step_grid) selected = state.selected_step_note >= 0 ? state.selected_step_note : state.keyboard_notes[0];
    if (state.show_step_grid && percussion && state.selected_step_note < 0) selected = LaunchpadLayout::drumPadNoteForPad(0, 0);

    for (int y = 0; y < 8; y++) {
      for (int x = 0; x < 8; x++) {
        Rgb color {0, 0, 0};
        if (state.show_step_grid && y >= LaunchpadLayout::kPlayRows) {
          auto step = LaunchpadLayout::stepForPad(x, y);
          if (step < state.step_view_length) {
            color = (state.step_view_bits & (1u << step)) != 0 ? kStepLitColor : kStepUnlitColor;
            if (step == state.drum_playhead_step) {
              auto hsl = rgbToHsl(color);
              hsl.l = LAUNCHPAD_ACTIVE_LUMINOSITY;
              color = hslToRgb(hsl);
            }
          }
        } else if (percussion) {
          auto note = LaunchpadLayout::drumPadNoteForPad(x, y);
          if (note >= 0) {
            color = note == selected ? kSelectedColor : padColor(percussionFamilyColor(LaunchpadLayout::percussionFamilyForNote(note)), state.active_note_loudness, note);
          }
        } else {
          auto note = state.keyboard_notes[static_cast<size_t>(x + 8 * y)];
          if (note >= 0 && edo_steps > 0) {
            if (note == selected) {
              color = kSelectedColor;
            } else if (basis.degenerate) {
              color = padColor({40, 40, 40}, state.active_note_loudness, note); // no meaningful scale structure
            } else {
              auto pitch_class = ((note - tonic) % edo_steps + edo_steps) % edo_steps;
              color = padColor(consonanceColor(levels[static_cast<size_t>(pitch_class)]), state.active_note_loudness, note);
            }
          }
        }
        colors.push_back({LaunchpadProtocol::padToNoteNumber(x, y), color.r, color.g, color.b});
      }
    }
  }

  // Track-picker overlay (see DeviceState::track_picker_active's own
  // comment): a post-process pass that only ever overwrites the picker
  // row itself with each selectable track's own bright/dim purpose color
  // (LAUNCHPAD_TRACK_PICKER_*_BRIGHT/DIM above - see
  // LAUNCHPAD_TRACK_PICKER_ROW's own comment for what bright vs. dim means
  // per purpose) - every other row is left exactly as Live View's own
  // rendering above already computed it, since the overlay is Live-
  // view-only now (GridMode's own comment) and Live View stays fully
  // interactive underneath (UI::handleLaunchpadPadEvent only ever routes
  // the picker row itself here - see isTrackPickerRow()). Colors already
  // pushed in row-major (x + y*8) order above, matching this loop's own
  // indexing.
  if (state.track_picker_active) {
    for (int x = 0; x < 8; x++) {
      auto & c = colors[static_cast<size_t>(LAUNCHPAD_TRACK_PICKER_ROW * 8 + x)];
      bool has_track = x < static_cast<int>(live_.track_ids.size());
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
        case DeviceState::TrackPickerPurpose::RECORD_ARM:
          pick_color = state.track_picker_armed[static_cast<size_t>(x)] ? LAUNCHPAD_TRACK_PICKER_RECORD_ARM_BRIGHT : LAUNCHPAD_TRACK_PICKER_RECORD_ARM_DIM;
          break;
        }
      }
      // A static color, even over a playing/queued pad's flash or pulse.
      c = { c.led_index, pick_color.r, pick_color.g, pick_color.b };
    }
  }

  // Extra-button LEDs. CC numbers unreachable on X/Mini MK3 (30, 20 -
  // Pro MK3's left column) are harmless to include here: those models
  // simply don't have the physical button, so the colourspec entry has
  // nothing to light.
  //
  // 92/93/94 go dark in GridMode::LIVE - handleCommand()'s own
  // comments on "pad-next-track"/"pad-prev-track" (reserved there, an
  // unconditional no-op returning true) and "move-row-down" (moves the
  // *terminal* overview's own bar cursor, which Live View's own
  // clip-pool/track-column grid never reflects - nothing on this device
  // itself ever visibly changes) - a lit static color would otherwise
  // misleadingly suggest a press here does something a performer looking
  // only at the Launchpad could ever actually see.
  bool arrows_active = state.grid_mode != GridMode::LIVE;
  // 91/92 ("move-row-up"/"move-row-down") are repurposed while the step
  // grid is showing, exactly like 93/94 below - handleCommand()'s own
  // comment - scrolling this device's own row window
  // (DeviceState::drum_edit_row_offset) by a few rows at a time instead of
  // their ordinary Live-only/terminal-only meaning, so a performer can
  // reach any row a scale/chromatic run has, not just whichever 8 were
  // shown when the clip was opened. Meaningful only for a *pitched*
  // track's own step grid - a PercussionTrack's lanes are a small, fixed,
  // manually-curated list with nothing to scroll to at all
  // (DeviceState::drum_edit_row_offset's own comment) - so unlike before
  // this now goes dark for a percussion step grid, matching every other
  // "nothing a performer could see would happen" button here; for a
  // pitched one it's always lit (no ceiling to go dark for - the row
  // window can always keep scrolling, up or down, without ever becoming a
  // true no-op the way paging below can).
  bool row_scroll_useful = !state.show_step_grid || !state.assigned_track_is_percussion;
  uint8_t arrow_white = (arrows_active && row_scroll_useful) ? 30 : 0;
  // 93/94 ("pad-prev-track"/"pad-next-track") are their own case while the step
  // grid is showing: handleCommand()'s own comment repurposes them into
  // scrolling through a clip longer than one 32-step window - genuinely
  // useful only when there's still some step this device isn't showing
  // (drum_edit_max_step_offset > 0), the same threshold handleCommand()'s
  // own no-op guard uses. Outside the step grid they keep their ordinary
  // out-of-Live meaning (moving the shared cursor track) unchanged.
  // White, not a distinct hue - all four of 91-94 read as one family of
  // step-grid navigation once a clip's open (row window vs. step window),
  // so they share 92's own color rather than each getting its own.
  bool paging_useful = state.drum_edit_max_step_offset > 0;
  bool page_arrows_lit = state.show_step_grid ? paging_useful : arrows_active;
  uint8_t page_arrow_white = page_arrows_lit ? 60 : 0;
  // Tempo/Swing views: CC91/92 are the value's up/down arrows, the rest of
  // the arrow row does nothing.
  bool number_view = inNumberView(state);
  if (number_view) {
    arrow_white = 60;
    page_arrow_white = 0;
  }
  // 91 ("move-row-up") doubles as a shift modifier with a real,
  // Launchpad-visible meaning specifically from Live View
  // (LaunchpadManager::handleShiftButton()), on top of the octave-shift
  // meaning above once the step grid is showing - so it stays dim-lit in
  // every GridMode (never dark in Live the way 92 still is - one of
  // 91's own two meanings is always live there), and lights full bright
  // while actually held, the same "held == bright" convention every other
  // momentary control here already uses; the pad it's currently combined
  // with (DeviceState::row_up_shift_pending_pad) gets the identical
  // bright-white treatment below, so the two lit pads visually pair up
  // while the press is held.
  uint8_t row_up_level = state.row_up_shift_held ? 127 : number_view ? 60 : 30;
  colors.push_back({91, row_up_level, row_up_level, row_up_level});
  colors.push_back({92, arrow_white, arrow_white, arrow_white}); // move-row-down, dim white (static)
  colors.push_back({93, page_arrow_white, page_arrow_white, page_arrow_white}); // pad-prev-track, dim white (static)
  colors.push_back({94, page_arrow_white, page_arrow_white, page_arrow_white}); // pad-next-track, dim white (static)
  // Session (CC95)/Note (CC96)/Custom (CC97) are this device's own
  // GridMode selectors (DRAW, reached by shift + Custom, is the fourth) -
  // each lit when active, same
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
  // mixer_mode) - green while active and in scene-launch (the
  // default) submode, orange while active and in mixer submode instead,
  // dim green while not active at all - "active" meaning anywhere in the
  // family (inMixerFamily()), not just the plain grid, so a fader/
  // picker still reads as "Session" underneath.
  {
    Rgb clip_color = !inMixerFamily(state) ? Rgb{0, 20, 0} : state.mixer_mode ? Rgb{127, 64, 0} : Rgb{0, 127, 0};
    colors.push_back({95, clip_color.r, clip_color.g, clip_color.b});
  }
  // Note (CC96) goes fully dark while the step grid is showing rather than
  // its usual lit-when-active color: pressing it while already forced into
  // NOTES (openStepView(), the usual way this device got here)
  // changes nothing at all, a true no-op unlike every other reason this
  // LED ever lights.
  uint8_t note_level = state.show_step_grid ? 0 : state.grid_mode == GridMode::NOTES ? uint8_t(90) : uint8_t(20);
  colors.push_back({96, note_level, note_level, note_level});
  if (state.row_up_shift_held) {
    colors.push_back({97, 90, 0, 127}); // Draw, purple, while shift is held
  } else {
    colors.push_back({97, state.grid_mode == GridMode::CUSTOM ? uint8_t(90) : uint8_t(20), 0, state.grid_mode == GridMode::CUSTOM ? uint8_t(127) : uint8_t(20)});
  }
  // CC98 (Session Record): bright red while anything is recording
  // (record_arm_led_on), dim red otherwise - dark while the step grid is
  // showing, where a tap would only ever say there is nothing to overdub.
  // Recording stays lit even there: it's real, track-global state a
  // performer still needs to see.
  {
    Rgb record_color = state.record_arm_led_on ? Rgb{127, 0, 0} : state.show_step_grid ? Rgb{0, 0, 0} : Rgb{40, 0, 0};
    colors.push_back({98, record_color.r, record_color.g, record_color.b});
  }
  // 99 (top-right corner, the grid position the Programmer-mode protocol
  // maps one past the 91-98 top row) isn't actually a pressable button on
  // real Launchpad X hardware - see handleRawButton()'s own comment - so
  // it's left off/reserved rather than wired to reflect any state.
  colors.push_back({99, 0, 0, 0});
  // Record Arm/Volume/Pan/SendA/SendB/Stop Clip/Mute/Solo (19/89/79/69/
  // 59/49/39/29, and Pro MK3 left-column twins 30/20) are Live's own
  // mixer-submode radio group (DeviceState::mixer_mode/GridMode's
  // own comment): outside the Live family entirely (NOTES/CUSTOM/
  // DRAW), none of them mean anything (handleRawButton()'s own comment on
  // this same group), so all eight go fully off rather than showing a
  // color that looks pressable but isn't. Inside the family, while mixer
  // submode is off (the default), all eight are plain scene-launch
  // triggers with no state of their own to show, so they go uniformly dim
  // white - LAUNCHPAD_SCENE_LAUNCH_BUTTON_COLOR - rather than any of
  // their mixer-mode hues, which would otherwise misleadingly suggest a
  // fader/picker is one press away. While it's on, each shows its own hue
  // (LAUNCHPAD_TRACK_PICKER_*_BRIGHT/DIM above for the four picker
  // purposes - the exact same colors the picker row itself shows, since
  // which target track the action lands on isn't decided until a pad
  // there is actually pressed; a parallel BRIGHT/dim pair for the four
  // fader modes below) at full brightness for whichever one of the eight
  // is currently active, dim otherwise - never more than one bright at
  // once, matching the radio group's own "only one active" rule
  // (inMixerFamily()).
  bool in_mixer_family = inMixerFamily(state);
  bool mini_layout = hasStopSoloMuteCycle(device_id);
  bool mixer_mode = state.mixer_mode && !mini_layout;
  bool picker_record_arm = state.track_picker_active && state.track_picker_purpose == DeviceState::TrackPickerPurpose::RECORD_ARM;
  bool picker_mute = state.track_picker_active && state.track_picker_purpose == DeviceState::TrackPickerPurpose::MUTE;
  bool picker_solo = state.track_picker_active && state.track_picker_purpose == DeviceState::TrackPickerPurpose::SOLO;
  bool picker_stop_clip = state.track_picker_active && state.track_picker_purpose == DeviceState::TrackPickerPurpose::STOP_CLIP;
  Rgb record_arm_button_color = state.grid_mode == GridMode::NOTES && !state.show_step_grid ? (state.record_arm_led_on ? Rgb{127, 0, 0} : Rgb{40, 0, 0}) : !in_mixer_family ? Rgb{0, 0, 0}
                                                                                                                                                       : !mixer_mode        ? LAUNCHPAD_SCENE_LAUNCH_BUTTON_COLOR
                                                                                                                                                       : picker_record_arm  ? LAUNCHPAD_TRACK_PICKER_RECORD_ARM_BRIGHT
                                                                                                                                                                            : LAUNCHPAD_TRACK_PICKER_RECORD_ARM_DIM;
  Rgb stop_clip_button_color = !in_mixer_family ? Rgb{0, 0, 0} : !mixer_mode ? LAUNCHPAD_SCENE_LAUNCH_BUTTON_COLOR : picker_stop_clip ? LAUNCHPAD_TRACK_PICKER_STOP_CLIP_BRIGHT : LAUNCHPAD_TRACK_PICKER_STOP_CLIP_DIM;
  Rgb solo_button_color = !in_mixer_family ? Rgb{0, 0, 0} : !mixer_mode ? LAUNCHPAD_SCENE_LAUNCH_BUTTON_COLOR : picker_solo ? LAUNCHPAD_TRACK_PICKER_SOLO_BRIGHT : LAUNCHPAD_TRACK_PICKER_SOLO_DIM;
  Rgb mute_button_color = !in_mixer_family ? Rgb{0, 0, 0} : !mixer_mode ? LAUNCHPAD_SCENE_LAUNCH_BUTTON_COLOR : picker_mute ? LAUNCHPAD_TRACK_PICKER_MUTE_BRIGHT : LAUNCHPAD_TRACK_PICKER_MUTE_DIM;
  Rgb send_b_button_color = !in_mixer_family ? Rgb{0, 0, 0} : !mixer_mode ? LAUNCHPAD_SCENE_LAUNCH_BUTTON_COLOR : state.grid_mode == GridMode::SEND_B ? LAUNCHPAD_MIXER_SEND_B_BRIGHT : LAUNCHPAD_MIXER_SEND_B_DIM;
  Rgb send_a_button_color = !in_mixer_family ? Rgb{0, 0, 0} : !mixer_mode ? LAUNCHPAD_SCENE_LAUNCH_BUTTON_COLOR : state.grid_mode == GridMode::SEND_A ? LAUNCHPAD_MIXER_SEND_A_BRIGHT : LAUNCHPAD_MIXER_SEND_A_DIM;
  Rgb pan_button_color = !in_mixer_family ? Rgb{0, 0, 0} : !mixer_mode ? LAUNCHPAD_SCENE_LAUNCH_BUTTON_COLOR : state.grid_mode == GridMode::PAN ? LAUNCHPAD_MIXER_PAN_BRIGHT : LAUNCHPAD_MIXER_PAN_DIM;
  Rgb volume_button_color = !in_mixer_family ? Rgb{0, 0, 0} : !mixer_mode ? LAUNCHPAD_SCENE_LAUNCH_BUTTON_COLOR : state.grid_mode == GridMode::SEND_MAIN ? LAUNCHPAD_MIXER_VOLUME_BRIGHT : LAUNCHPAD_MIXER_VOLUME_DIM;

  // While shift is held (or a duplicate is in progress) the right-side
  // buttons show their alternate functions instead (see handleRawButton()).
  if (state.row_up_shift_held || state.duplicate_held || state.delete_held || state.quantize_held) {
    // Record Arm (undo) and Mute (redo) are reserved: lit dim so they read as
    // taken, but they do nothing yet.
    record_arm_button_color = Rgb{40, 40, 40};
    mute_button_color = Rgb{40, 40, 40};
    solo_button_color = cached_metronome_on_ ? Rgb{127, 100, 0} : Rgb{40, 30, 0};
    pan_button_color = state.delete_held ? Rgb{127, 0, 60} : Rgb{60, 0, 30}; // magenta: red is Quantise's record-quantise-off
    // Record Quantise: green while on, red while off, white while held.
    send_a_button_color = state.quantize_held ? Rgb{127, 127, 127} : cached_record_quantize_ ? Rgb{0, 127, 0} : Rgb{127, 0, 0};
    volume_button_color = state.duplicate_held ? Rgb{127, 127, 127} : Rgb{0, 100, 127};
    // Send B opens the Tempo view (blue), Stop Clip the Swing view (orange);
    // bright for the one showing.
    send_b_button_color = state.grid_mode == GridMode::TEMPO ? Rgb{0, 50, 127} : Rgb{0, 20, 50};
    stop_clip_button_color = state.grid_mode == GridMode::SWING ? Rgb{127, 55, 0} : Rgb{50, 22, 0};
  } else if (number_view) {
    // The view being shown is bright, the other dim: a press switches to it.
    send_b_button_color = state.grid_mode == GridMode::TEMPO ? Rgb{0, 50, 127} : Rgb{0, 8, 20};
    stop_clip_button_color = state.grid_mode == GridMode::SWING ? Rgb{127, 55, 0} : Rgb{20, 9, 0};
  }
  if (mini_layout && in_mixer_family && !(state.row_up_shift_held || state.duplicate_held || state.delete_held || state.quantize_held || number_view) &&
      !(state.grid_mode == GridMode::NOTES && !state.show_step_grid)) {
    // Bottom button shows the cycle position: white while the bottom row
    // shows clips, then the picker's own hue per purpose.
    record_arm_button_color = !state.track_picker_active                                                 ? Rgb{127, 127, 127}
                              : state.track_picker_purpose == DeviceState::TrackPickerPurpose::STOP_CLIP ? LAUNCHPAD_TRACK_PICKER_STOP_CLIP_BRIGHT
                              : state.track_picker_purpose == DeviceState::TrackPickerPurpose::SOLO      ? LAUNCHPAD_TRACK_PICKER_SOLO_BRIGHT
                                                                                                         : LAUNCHPAD_TRACK_PICKER_MUTE_BRIGHT;
  }
  colors.push_back({19, record_arm_button_color.r, record_arm_button_color.g, record_arm_button_color.b});
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
        return a.led_index == b.led_index && a.type == b.type && a.r == b.r && a.g == b.g && a.b == b.b &&
          a.palette == b.palette && a.flash_to == b.flash_to && a.flash_from == b.flash_from;
      });
  if (!colors_changed) return;

  launchpad_io_->sendLeds(device_id, colors);
  state.last_sent_colors = move(colors);
}


void
LaunchpadManager::refresh(const Song & song, const vector<int> & track_ids, const PlaybackInfo & playback_info, int fallback_track_index, Controller & controller, const LiveWindow & live) {
  if (!launchpad_io_) return;

  flushPendingPanPresses(controller);
  cursor_track_index_ = fallback_track_index;

  // Mirrored once per frame, same as the note-capture-armed edge
  // detection below - see cached_global_octave_'s own comment.
  cached_global_octave_ = controller.getGlobalOctave();
  cached_metronome_on_ = controller.isMetronomeOn();
  cached_tempo_ = song.getTempo();
  cached_swing_ = song.getSwing();
  tickNumberView(controller);
  cached_record_quantize_ = song.getRecordQuantize();
  // Cached for handleLivePadEvent() - see live_'s own comment.
  live_ = live;

  // Record Arm's own note-capture side effects live in this file (this
  // class's own auto-started-transport tracking, not reachable from
  // Controller directly), but
  // the flag itself can now flip from anywhere - M-x, a keybinding, a
  // Launchpad press alike (see was_note_capture_armed_'s own comment) -
  // so they react here, to the flag's own rising/falling edge each frame,
  // rather than inline in a button handler.
  bool note_capture_armed = controller.isNoteCaptureArmed();
  if (note_capture_armed && !was_note_capture_armed_) {
    // Starts the transport immediately, the same as SampleTrack's own
    // Record Arm - not deferred to the first actually-captured note/step
    // - so both branches behave alike; the clip itself still only gets
    // created once a note actually lands
    // (Controller::ensureNoteRecordingClip()'s own lazy-create gate).
    // startAutoRecordSession(), not the plainer startAutoRecordPlayback():
    // it also mutes the song's own pattern-driven scheduling, so the live
    // take is heard through its own separate stream rather than doubled
    // against old content. Never while any clip take is running
    // (isAnyClipRecording()) - a clip take populates a clip
    // slot directly with no arrangement involved at all, so there's
    // nothing for the transport to be running for.
    if (!controller.isAnyClipRecording() && !playback_info.isPlaying()) {
      controller.startAutoRecordSession(auto_started_playback_, auto_record_cleared_rows_, last_cleared_row_, auto_record_clip_ids_);
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
  // (Controller::getFocusedClipTrackId(), ClipGrid's own Enter) -
  // triggerAuditionStep() below only fires for this one track, not
  // whichever track a Launchpad device happens to be assigned to: a
  // focus is deliberately hardware-independent (no Launchpad needs to be
  // connected at all to hear the clip you're editing), and deliberately
  // exclusive - a single, currently-selected-for-editing clip, not
  // Live-View-style multi-track simultaneous launching. Silencing the
  // *previous* focus's track on any focus change is Controller's own job
  // (setFocusedClip()/clearFocusedClip()), not this loop's.
  auto focused_track_id = controller.getFocusedClipTrackId();

  // The step grid's preview clock - computed once here, shared by every
  // connected device below, not per-device. Active exactly while the
  // transport is stopped and Record Arm is off (or a clip take is
  // recording, which keeps ClipPlayer's clock running too) - while
  // playing, the pattern-driven path in SongState::renderBlock() already
  // plays the same track, and running both at once would double-trigger;
  // while armed, the player is presumably about to record something
  // deliberate and doesn't want an uncontrolled loop underneath it.
  // audition_step is this frame's step for the per-device playhead
  // display below, or -1 when the clock isn't running at all.
  int audition_step = -1;
  bool audition_active = !playback_info.isPlaying() && (!note_capture_armed || controller.isAnyClipRecording());
  if (audition_active) {
    auto now = chrono::steady_clock::now();
    if (!preview_clock_.isRunning()) {
      // (Re)starting from step 0, fired at once rather than after a row
      // of dead air.
      preview_clock_.start();
      preview_clock_last_refresh_ = now;
      if (focused_track_id >= 0) triggerAuditionStep(song, focused_track_id, controller, preview_clock_.currentStep());
    } else {
      float dt = chrono::duration<float>(now - preview_clock_last_refresh_).count();
      preview_clock_last_refresh_ = now;
      auto tempo = song.getTempo();
      // A row is a 16th note at this tempo; tempo <= 0 makes advance() a
      // no-op.
      float row_duration = tempo > 0 ? 60.0f / 4.0f / static_cast<float>(tempo) : 0.0f;
      for (int step : preview_clock_.advance(dt, row_duration)) {
        if (focused_track_id >= 0) triggerAuditionStep(song, focused_track_id, controller, step);
      }
    }
    audition_step = preview_clock_.currentStep();
  } else {
    // A held note triggerAuditionStep() started would otherwise ring out
    // with nothing left driving it once the clock stops mid-note.
    if (preview_clock_.isRunning() && focused_track_id >= 0) {
      controller.getPlaybackEventQueue().push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::STOP_ALL_NOTES, controller.getActiveBufferName(), focused_track_id));
    }
    preview_clock_.stop();
  }

  // The Send A/Send B/Send Main/Pan grid modes always address the first 8
  // root tracks (not whichever track a device happens to be assigned to) -
  // the same values apply to every connected device, computed once here
  // rather than per-device inside the loop below.
  array<float, 8> track_send_main{}, track_send_a{}, track_send_b{}, track_azimuth{};
  array<int, 8> track_send_main_micro{}, track_send_a_micro{}, track_send_b_micro{};
  // DeviceState::track_send_main_row's own comment - -1 (never touched)
  // by default, matching FaderState's own untouched default.
  array<int, 8> track_send_main_row{}, track_send_a_row{}, track_send_b_row{};
  track_send_main_row.fill(-1); track_send_a_row.fill(-1); track_send_b_row.fill(-1);
  // DeviceState::track_colors' own comment - same identity hue/lightness
  // Live View's clip_colors below computes, just once per track
  // rather than once per clip pad.
  array<Color, 8> track_colors{};
  SongStructure structure(song);
  for (int i = 0; i < 8 && i < num_tracks; i++) {
    // No track-type check needed - track_ids is already
    // getPlayableTrackIds()'s own "every LeafTrack" list (its own doc
    // comment), so this cast always succeeds; a stale, narrower type
    // whitelist here once silently left a SampleTrack's own fader/LED
    // feedback at 0 regardless of its actual send/pan values.
    auto track_id = track_ids[static_cast<size_t>(i)];
    track_colors[static_cast<size_t>(i)] = Color::fromHSL(structure.getBaselineInfo(track_id).getHue(), 0.8f, 0.3f);
    auto track = song.getMasterTrack().getChildByInternalId(track_id);
    if (track) {
      auto & leaf_track = dynamic_cast<const LeafTrack&>(*track);
      track_send_main[static_cast<size_t>(i)] = leaf_track.getSends().main;
      track_send_a[static_cast<size_t>(i)] = leaf_track.getSends().a;
      track_send_b[static_cast<size_t>(i)] = leaf_track.getSends().b;
      track_azimuth[static_cast<size_t>(i)] = leaf_track.getAzimuth();
    }
    // A track absent from a given map has never had that fader touched,
    // and defaults to micro_step 0 (FaderState's own default) - exactly
    // right, since "never touched" and "resting plainly on this row's own
    // canonical value" are the same displayed state.
    auto microStepFor = [&](std::unordered_map<int, FaderState> & states) {
      auto it = states.find(track_id);
      return it != states.end() ? it->second.micro_step : 0;
    };
    track_send_main_micro[static_cast<size_t>(i)] = microStepFor(fader_state_send_main_);
    track_send_a_micro[static_cast<size_t>(i)] = microStepFor(fader_state_send_a_);
    track_send_b_micro[static_cast<size_t>(i)] = microStepFor(fader_state_send_b_);

    // DeviceState::track_send_main_row's own comment - -1 (the array's
    // own fill(-1) default above) when never touched.
    auto lastPressedRowFor = [&](std::unordered_map<int, FaderState> & states) {
      auto it = states.find(track_id);
      return it != states.end() && it->second.touched ? it->second.last_pressed_row : -1;
    };
    track_send_main_row[static_cast<size_t>(i)] = lastPressedRowFor(fader_state_send_main_);
    track_send_a_row[static_cast<size_t>(i)] = lastPressedRowFor(fader_state_send_a_);
    track_send_b_row[static_cast<size_t>(i)] = lastPressedRowFor(fader_state_send_b_);
  }

  // GridMode::LIVE's own shared LED grid - same "computed once here,
  // identical for every connected device" reasoning as track_send_main/
  // etc. above, and same reason this stays a plain Color array rather than
  // a DeviceState-nested computation: refreshLeds() only ever reads
  // DeviceState, never Song/PlaybackInfo directly (see its own branches).
  // Computed unconditionally (not gated on anything overview-focus-
  // related) since which devices, if any, are actually showing Live
  // view is now purely each one's own CC95/96 selection - see
  // handleRawButton()'s own comment. x = column, indexed into
  // live.track_ids - the overview's own filtered column list, not
  // track_ids above (this class's usual root-track-id parameter, which
  // includes non-color-eligible tracks Live View never shows a column
  // for); no column scroll yet (see LiveWindow's own comment). y is
  // flipped the same way the old plain-navigation overview's own rows
  // were: y=0 is the bottom-left pad (see LaunchpadProtocol::
  // padToNoteNumber()'s own doc comment), so y=7 (top) is that track's
  // first clip and y=0 (bottom) its last visible one; no row
  // scroll yet either, so a track with more than 8 clips only
  // shows the first 8 for now.
  array<Color, 64> clip_colors;
  // Parallel to clip_colors above - see DeviceState::clip_highlight's
  // own comment.
  array<ClipHighlight, 64> clip_highlight {};
  // The track-picker overlay's own per-track state (see
  // DeviceState::track_picker_active's own comment and
  // LAUNCHPAD_TRACK_PICKER_ROW's own comment in this file for what each
  // one drives) - computed alongside clip_colors below since both walk
  // the same per-track live.track_ids loop.
  array<bool, 8> track_picker_playing {};
  array<bool, 8> track_picker_soloed {};
  array<bool, 8> track_picker_muted {};
  array<bool, 8> track_picker_armed {};
  {
    auto & clip_player = controller.getClipPlayer();
    for (int x = 0; x < 8; x++) {
      if (x >= static_cast<int>(live.track_ids.size())) continue;
      auto live_track_id = live.track_ids[static_cast<size_t>(x)];
      // Same hue/near-fully-saturated identity the overview's own
      // terminal glyphs use, but at its own, dimmer lightness: a directly-
      // emitted LED pixel at a given lightness reads brighter than the
      // same value does as terminal glyph text, so the two surfaces are
      // tuned independently here rather than sharing one constant.
      auto identity = Color::fromHSL(structure.getBaselineInfo(live_track_id).getHue(), 0.8f, 0.3f);
      auto & clips = song.getClips(live_track_id);
      // STOP_CLIP's own picker-row state: a clip is playing right now - a
      // launched one on a taken-over track, else whatever the arrangement
      // has at the transport's position.
      bool any_playing = false;
      if (clip_player.isTakenOver(live_track_id)) {
        any_playing = clip_player.isLaunched(live_track_id);
      } else if (playback_info.isPlaying()) {
        any_playing = resolveInstanceAt(song, live_track_id, playback_info.getAbsolutePosition()).clip_index >= 0;
      }
      track_picker_playing[static_cast<size_t>(x)] = any_playing;
      track_picker_armed[static_cast<size_t>(x)] = controller.isTrackArmed(live_track_id);
      auto track = song.getMasterTrack().getChildByInternalId(live_track_id);
      if (track) {
        auto & leaf_track = dynamic_cast<const LeafTrack &>(*track);
        track_picker_soloed[static_cast<size_t>(x)] = leaf_track.isSolo();
        track_picker_muted[static_cast<size_t>(x)] = leaf_track.isMuted();
      }
      for (int y = 0; y < 8; y++) {
        auto clip_index = 7 - y;
        auto highlight = controller.getClipPlayer().clipHighlight(live_track_id, clip_index);
        clip_highlight[static_cast<size_t>(y * 8 + x)] = highlight;
        // An armed track's red states need no identity color underneath
        // (refreshLeds() never reads it for them); an empty, unarmed slot
        // stays dark.
        bool has_clip = clip_index < static_cast<int>(clips.size()) && !clips[static_cast<size_t>(clip_index)].isEmpty();
        bool recording_state = highlight == ClipHighlight::ARMED_EMPTY || highlight == ClipHighlight::RECORD_QUEUED ||
          highlight == ClipHighlight::RECORDING || highlight == ClipHighlight::RECORD_STOPPING;
        if (has_clip && !recording_state) clip_colors[static_cast<size_t>(y * 8 + x)] = identity;
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

    Tuning tuning = Tuning::EDO12;
    int key_val = -1;
    unordered_map<int, float> active_note_loudness;
    bool is_percussion = false;
    bool is_step_grid_track = false; // percussion or pitched: has a split step view
    uint32_t step_view_bits = 0;
    int step_view_length = 0;
    int drum_playhead_step = -1;
    // Mirrors DeviceState::drum_edit_max_step_offset's own comment -
    // computed below alongside drum_clip_editing, carried into state
    // after the loop the same way every other per-device field here is.
    int drum_max_step_offset = 0;
    // Whether a specific clip is actually open for editing on this
    // track (Controller::getFocusedClipTrackId()) - the step grid only
    // ever edits a clip this way, never the track's own background
    // Pattern (DeviceState::show_step_grid's own comment has the full
    // reasoning), so this doubles as "should the step grid show at all".
    bool drum_clip_editing = false;
    // A drum clip open for editing pins this device's own display to it
    // while actually in NOTES or CUSTOM mode (where the step grid/lane
    // picker themselves live) - see handlePadEvent()'s own identical
    // override for why (this is its LED-rendering counterpart); a device
    // that's since switched to a different GridMode (Send A, say, opened
    // temporarily on top) is unaffected and keeps following the shared
    // cursor for whatever that mode shows instead. Multi-track record
    // fan-out takes priority over even that: while any track is
    // recording, this device's own display always reflects one of the
    // recording tracks (the lowest track_id, same tie-break as
    // handlePadEvent()'s own identical override), not a pinned drum-clip
    // focus or the shared cursor - a press here already writes into that
    // track regardless of what's pinned, so the grid has to agree.
    int recording_reference_track_id = -1;
    if (state.grid_mode == GridMode::NOTES || state.grid_mode == GridMode::CUSTOM) {
      for (auto candidate : controller.getClipRecordingTrackIds()) {
        if (recording_reference_track_id < 0 || candidate < recording_reference_track_id) recording_reference_track_id = candidate;
      }
    }
    int pinned_track_id = recording_reference_track_id >= 0 ? recording_reference_track_id :
      (state.grid_mode == GridMode::NOTES || state.grid_mode == GridMode::CUSTOM) ? controller.getFocusedClipTrackId() : -1;
    if (pinned_track_id >= 0 || (track_index >= 0 && track_index < num_tracks)) {
      auto track_id = pinned_track_id >= 0 ? pinned_track_id : track_ids[static_cast<size_t>(track_index)];
      auto track = song.getMasterTrack().getChildByInternalId(track_id);
      tuning = track ? song.getTuningForTrack(*track) : song.getTuning();
      key_val = song.getKey();
      is_percussion = track && track->getType() == TrackType::PERCUSSION_CONTROL;
      is_step_grid_track = is_percussion || (track && track->getType() == TrackType::INSTRUMENT_CONTROL);
      if (is_step_grid_track) {
        // A focused clip can be longer than the 32 steps shown - this
        // device's own step offset (DeviceState::drum_edit_step_offset,
        // see handleCommand()'s own comment) picks which window of it is
        // drawn. Nothing is shown unless a clip is actually open for
        // editing on this track (DeviceState::show_step_grid).
        drum_clip_editing = controller.getFocusedClipTrackId() == track_id;
        auto drum_clip_length = drum_clip_editing ? focusedDrumClipLength(song, track_id, controller.getFocusedClip()) : -1;
        step_view_length = drum_clip_length > 0 ? std::clamp(drum_clip_length - std::clamp(state.drum_edit_step_offset, 0, std::max(0, drum_clip_length - kStepWindow)), 0, kStepWindow) : 0;
        drum_max_step_offset = drum_clip_length > 0 ? std::max(0, drum_clip_length - kStepWindow) : 0;
        auto drum_offset = drum_clip_editing ? std::clamp(state.drum_edit_step_offset, 0, std::max(0, drum_clip_length - kStepWindow)) : 0;
        // The selected sound's hits, identified by value like
        // handleStepGridPadEvent()'s own toggle. Per step, whatever is
        // active at that row (ArrangementOps.h's resolveReadTarget()).
        auto edit_note = drum_clip_editing ? stepEditNote(song, device_id, track_id) : -1;
        if (edit_note >= 0) {
          for (int step = 0; step < kStepWindow; step++) {
            if (drum_clip_length > 0 && drum_offset + step >= drum_clip_length) break;
            auto read_target = resolveReadTarget(song, track_id, drum_offset + step, controller.getFocusedClip());
            for (auto & n : read_target.pattern->getNotes(read_target.effective_row)) {
              if (n.isDefined() && !n.isOff() && !n.isAftertouch() && n.getValue() == edit_note) {
                step_view_bits |= 1u << step;
                break;
              }
            }
          }
        }
        // While playing, the real song position; while stopped, the
        // free-running audition clock's own shared step - or no playhead
        // at all if that clock isn't currently running either (Record Arm
        // is on). Only shown if it falls within this device's own window.
        if (playback_info.isPlaying()) {
          drum_playhead_step = playback_info.getAbsolutePosition() % kStepWindow;
        } else if (audition_step >= 0) {
          auto clip_row = drum_clip_length > 0 ? audition_step % drum_clip_length : audition_step % kStepWindow;
          drum_playhead_step = (clip_row >= drum_offset && clip_row < drum_offset + kStepWindow) ? clip_row - drum_offset : -1;
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
    state.record_arm_led_on = note_capture_armed || controller.isThresholdArmed() || controller.isRecording() || controller.isAnyClipRecording();
    state.tuning = tuning;
    state.key = key_val;
    state.active_note_loudness = move(active_note_loudness);
    state.clip_colors = clip_colors;
    state.clip_highlight = clip_highlight;
    state.track_picker_playing = track_picker_playing;
    state.track_picker_soloed = track_picker_soloed;
    state.track_picker_muted = track_picker_muted;
    state.track_picker_armed = track_picker_armed;
    state.track_send_main = track_send_main;
    state.track_send_a = track_send_a;
    state.track_send_b = track_send_b;
    state.track_azimuth = track_azimuth;
    state.track_send_main_micro = track_send_main_micro;
    state.track_send_a_micro = track_send_a_micro;
    state.track_send_b_micro = track_send_b_micro;
    state.track_send_main_row = track_send_main_row;
    state.track_send_a_row = track_send_a_row;
    state.track_send_b_row = track_send_b_row;
    state.track_colors = track_colors;
    state.grid_track_count = min(8, num_tracks);
    state.assigned_track_is_percussion = is_percussion;
    state.show_step_grid = is_step_grid_track && !controller.isAnyClipRecording() && drum_clip_editing;
    state.drum_edit_max_step_offset = drum_max_step_offset;
    state.step_view_bits = step_view_bits;
    state.step_view_length = step_view_length;
    state.keyboard_notes = resolveKeyboardNotes(song, device_id);
    state.drum_playhead_step = drum_playhead_step;

    refreshLeds(device_id, state);
  }
}
