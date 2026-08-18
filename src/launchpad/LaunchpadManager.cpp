#include "LaunchpadManager.h"

#include "LaunchpadIO.h"
#include "LaunchpadLayout.h"
#include "LaunchpadProtocol.h"
#include "LaunchpadPadEvent.h"
#include "LaunchpadChannelPressureEvent.h"
#include "../state/PlaybackInfo.h"
#include "../playback/PlaybackControlEvent.h"
#include "../model/Song.h"
#include "../model/InstrumentTrack.h"
#include "../model/DrumMachineTrack.h"
#include "../Controller.h"
#include "../util/constants.h"

#include <algorithm>
#include <chrono>

using namespace std;

namespace {
  // How long CC97 (DRAW mode toggle) must be held before release means
  // "clear the canvas" instead of "toggle the mode" - see
  // handleDrawToggleButton(). Long enough that a normal deliberate tap
  // (entering/exiting DRAW mode) never accidentally clears instead.
  constexpr auto kDrawClearHoldThreshold = std::chrono::milliseconds(600);

  // How long a DRAW-mode grid pad must be held before release means "just
  // adjust brightness" instead of "cycle to the next hue" - see
  // releaseDrawPad(). Same value as kDrawClearHoldThreshold above (both are
  // "long press" thresholds for a DRAW-mode gesture) but named/declared
  // separately since they're conceptually independent controls that could
  // reasonably be tuned apart later.
  constexpr auto kDrawPadLongPressThreshold = std::chrono::milliseconds(600);

  // How long Stop Clip's Clear confirm arm stays live (plans/drum-
  // machine.md, Phase 7) - a second press within this window after the
  // first actually clears; past it, the arm is stale and a press just
  // re-arms instead. Long enough to be a deliberate double-tap, short
  // enough that an unrelated press much later never accidentally lands
  // as a confirm.
  constexpr auto kClearConfirmWindow = std::chrono::milliseconds(1500);
  // How fast the Clear confirm indicator blinks while armed (refreshLeds())
  // - half-period, so a full on/off cycle is twice this.
  constexpr auto kClearConfirmBlinkPeriod = std::chrono::milliseconds(200);

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

  float hueToChannel(float p, float q, float t) {
    if (t < 0.0f) t += 1.0f;
    if (t > 1.0f) t -= 1.0f;
    if (t < 1.0f / 6.0f) return p + (q - p) * 6.0f * t;
    if (t < 1.0f / 2.0f) return q;
    if (t < 2.0f / 3.0f) return p + (q - p) * (2.0f / 3.0f - t) * 6.0f;
    return p;
  }

  Rgb hslToRgb(Hsl hsl) {
    float h = hsl.h / 360.0f, s = hsl.s, l = hsl.l;
    float r, g, b;
    if (s == 0.0f) {
      r = g = b = l; // achromatic (includes black - s stays 0 through rgbToHsl)
    } else {
      float q = l < 0.5f ? l * (1.0f + s) : l + s - l * s;
      float p = 2.0f * l - q;
      r = hueToChannel(p, q, h + 1.0f / 3.0f);
      g = hueToChannel(p, q, h);
      b = hueToChannel(p, q, h - 1.0f / 3.0f);
    }
    auto to_byte = [](float v) { return uint8_t(clamp(v, 0.0f, 1.0f) * 127.0f); };
    return {to_byte(r), to_byte(g), to_byte(b)};
  }

  // Idle luminosity vs. the luminosity of a pad whose voice is at full
  // loudness - hue/saturation come from the base consonance/percussion
  // color and are otherwise untouched, only lightness ramps between the
  // two as that voice's loudness (its own gain, decaying with any
  // envelope it has) moves from 0 to 1.
  constexpr float LAUNCHPAD_IDLE_LUMINOSITY = 0.35f;
  constexpr float LAUNCHPAD_ACTIVE_LUMINOSITY = 1.0f;

  // The drum picker's own idle/assigned lightness levels (Phase 6,
  // plans/drum-machine.md) - deliberately its own pair, not a reuse of
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
  // Only depths 3-4 (RECURSIVE tier) get their own distinguishable hue -
  // depth 3 ("somewhat prominent still") at higher saturation, depth 4
  // clearly more muted (also given a much wider hue separation from depth
  // 3 than the drift's own step size would otherwise give - see
  // LaunchpadLayout's kDepth3HueOffset/kDepth4HueOffset comment - closely
  // spaced hues within the same family were hard to tell apart on real
  // hardware (e.g. A and B, both major-family in the key of C 31-EDO,
  // looked nearly identical at the drift's default step size). Depth 5+
  // is capped to one flat, deliberately unprominent gray (achromatic -
  // saturation 0, hue irrelevant) rather than continuing to differentiate
  // ever-closer hues.
  constexpr float kConsonanceDepth3Saturation = 0.85f;
  constexpr float kConsonanceDepth4Saturation = 0.5f;
  constexpr int kConsonanceMaxDistinguishableDepth = 4;

  Rgb consonanceColor(const LaunchpadLayout::PadClassification & classification) {
    if (classification.tier == LaunchpadLayout::PadTier::TONIC) {
      return hslToRgb({classification.hue, kConsonanceTonicSaturation, 0.5f});
    }
    if (classification.tier == LaunchpadLayout::PadTier::FOURTH || classification.tier == LaunchpadLayout::PadTier::FIFTH) {
      return hslToRgb({classification.hue, kConsonanceFourthFifthSaturation, 0.5f});
    }
    // RECURSIVE.
    if (classification.depth > kConsonanceMaxDistinguishableDepth) {
      return hslToRgb({0.0f, 0.0f, 0.5f}); // flat gray catch-all, depth 5+
    }
    auto saturation = classification.depth <= 3 ? kConsonanceDepth3Saturation : kConsonanceDepth4Saturation;
    return hslToRgb({classification.hue, saturation, 0.5f});
  }

  // The free-drumming layout's per-family base color - shared by the
  // ordinary percussion note-grid rendering and the drum picker (Phase 6,
  // plans/drum-machine.md), which reuses the exact same layout as its own
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
  // helper" convention model/InstrumentTrack.cpp's own linearToDb() and
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
  return state ? state->octave : 4;
}

void
LaunchpadManager::octaveUp(int device_id) {
  auto & state = deviceState(device_id);
  state.octave = LaunchpadLayout::clampOctave(state.octave, 1);
}

void
LaunchpadManager::octaveDown(int device_id) {
  auto & state = deviceState(device_id);
  state.octave = LaunchpadLayout::clampOctave(state.octave, -1);
}

int
LaunchpadManager::assignedTrackIndex(int device_id, int fallback_track_index) const {
  auto * state = findDeviceState(device_id);
  if (!state || state->assigned_track_id < 0) return fallback_track_index;
  return state->assigned_track_id;
}

void
LaunchpadManager::resetTrackAssignments() {
  for (auto & [ device_id, state ] : devices_) state.assigned_track_id = -1;
}

void
LaunchpadManager::advanceTrack(int device_id, int delta, int fallback_track_index, int num_tracks) {
  auto & state = deviceState(device_id);
  auto current = state.assigned_track_id < 0 ? fallback_track_index : state.assigned_track_id;
  state.assigned_track_id = LaunchpadLayout::advanceTrackIndex(current, delta, num_tracks);
}

int
LaunchpadManager::resolveTrackId(int device_id, const vector<int> & track_ids, int fallback_track_index) const {
  if (track_ids.empty()) return -1;
  auto track_index = assignedTrackIndex(device_id, fallback_track_index);
  if (track_index < 0 || track_index >= static_cast<int>(track_ids.size())) track_index = fallback_track_index;
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
  state.grid_mode = (state.grid_mode == mode) ? GridMode::NOTES : mode;
}

bool
LaunchpadManager::handleRawButton(int cc_number, int device_id) {
  // 69/79/89 confirmed against a real Launchpad X: Send A/Pan/Volume, 10
  // apart in that order (row 5/6/7 of the right column, CC = 19 + 10*row) -
  // not the arbitrary contiguous-slot guess this originally shipped with.
  // 59 (row 4, one further down the same sequence) is unconfirmed but a
  // strong inference: it matches Ableton Live's own standard Launchpad
  // "Track" control row order (Volume, Pan, Send A, Send B, Stop, Mute,
  // Solo, Record Arm - Volume/Pan/SendA already lined up exactly with that
  // order at 89/79/69). 89/Volume is repurposed as the Send Main fader
  // mode - the same bargraph shape as Send A/Send B, just controlling how
  // much of each track's own voices reach the main mix (InstrumentTrack::
  // getSendMain()) rather than the shared send bus. 97 (DRAW mode toggle)
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
  if (cc_number == 89) {
    toggleGridMode(device_id, GridMode::SEND_MAIN);
    return true;
  }
  if (cc_number == 79) {
    toggleGridMode(device_id, GridMode::PAN);
    return true;
  }
  if (cc_number == 69) {
    toggleGridMode(device_id, GridMode::SEND_A);
    return true;
  }
  if (cc_number == 59) {
    toggleGridMode(device_id, GridMode::SEND_B);
    return true;
  }
  // 19 (right column, continuing the Ableton "Track" control row order one
  // further past Send B - see this method's own doc comment) is the real
  // Launchpad X's own dedicated "Record Arm" button - a single, song-wide
  // record-arm flag (capture_enabled_ - see its own comment for why this
  // isn't per-device), not a Song/Track-mutating command, so it's a
  // direct hardware-state flip here rather than a named command (same
  // shape as the grid-mode toggles above, those *are* still per-device).
  // Moved here from CC98 ("Capture MIDI", now reserved/unused again) -
  // see DeviceState::capture_enabled's own comment for why. CC98 used to
  // be wired to toggle-playing via the named-command pipeline before
  // that; toggle-playing stays reachable via Space either way.
  if (cc_number == 19) {
    capture_enabled_ = !capture_enabled_;
    return true;
  }
  // 97 ("Custom") is the drum-picker latch (plans/drum-machine.md, Phase
  // 6) - unconditional, like every other toggle above, since picking is
  // only ever meaningful once a DrumMachineTrack is assigned but the
  // per-device UI state itself doesn't need to know that. DRAW mode used
  // to live here too; it moved to Stop Clip (CC49, handleStopClipButton())
  // since Custom needed to be free for the picker and DRAW still needs
  // its own dedicated button, not a fallback that disappears while a
  // drum machine happens to be assigned.
  if (cc_number == 97) {
    auto & state = deviceState(device_id);
    state.picker_active = !state.picker_active;
    return true;
  }
  return false;
}

bool
LaunchpadManager::anyCaptureArmedNoteHeld() const {
  for (auto & [ device_id, state ] : devices_) {
    if (state.capture_enabled && !state.active_notes.empty()) return true;
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

  // Every track currently receiving live input, across every device -
  // not just the caller's own, since two different Launchpads could be
  // assigned to different tracks and both mid-hold at once.
  vector<int> track_ids;
  for (auto & [ device_id, state ] : devices_) {
    for (auto & [ pos, note ] : state.active_notes) {
      if (find(track_ids.begin(), track_ids.end(), note.track_id) == track_ids.end()) {
	track_ids.push_back(note.track_id);
      }
    }
  }

  controller.sweepAutoRecordRows(auto_record_cleared_rows_, last_cleared_row_, last_cleared_pattern_idx_, info.getPatternIndex(), info.getRowIndex(), track_ids);
}

bool
LaunchpadManager::handleDrawToggleButton(int device_id, bool is_press) {
  auto & state = deviceState(device_id);
  if (is_press) {
    state.draw_toggle_pressed = true;
    state.draw_toggle_press_time = std::chrono::steady_clock::now();
    return true;
  }
  if (!state.draw_toggle_pressed) return true; // stray/duplicate release
  state.draw_toggle_pressed = false;
  auto held = std::chrono::steady_clock::now() - state.draw_toggle_press_time;
  if (held >= kDrawClearHoldThreshold && state.grid_mode == GridMode::DRAW) {
    // Long hold, released while already in DRAW mode: blank the canvas
    // (DRAW_PALETTE[0] is "off" - see its own definition above) rather than
    // toggling the mode, so the canvas can be cleared without losing DRAW
    // mode itself. A long hold while NOT already in DRAW mode has nothing
    // to clear, so it falls through to the normal toggle below instead
    // (entering DRAW mode, same as a quick tap would).
    state.draw_color_index.fill(0);
  } else {
    toggleGridMode(device_id, GridMode::DRAW);
  }
  return true;
}

bool
LaunchpadManager::handleStopClipButton(int device_id, bool is_press, DrumMachineTrack * assigned_drum_track, Controller & controller) {
  if (!assigned_drum_track) return handleDrawToggleButton(device_id, is_press);
  if (!is_press) return true; // Clear is a plain tap, unlike DRAW's long-hold gesture - RELEASE is a no-op

  auto & state = deviceState(device_id);
  if (state.clear_confirm.press(std::chrono::steady_clock::now(), kClearConfirmWindow)) {
    // Second press within the window: actually clear. Every existing
    // lane's step data goes back to all-rest - the lane list itself
    // (which notes have a lane at all) is untouched, matching the plan's
    // own distinction between this gesture and the picker's lane removal.
    for (int note : assigned_drum_track->getLaneNotes()) assigned_drum_track->setSteps(note, 0);
    controller.getSong().incVersion();
  }
  return true;
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
LaunchpadManager::handleCommand(string_view name, int device_id, int fallback_track_index, int num_tracks) {
  if (name == "octave-up") {
    octaveUp(device_id);
    return true;
  }
  if (name == "octave-down") {
    octaveDown(device_id);
    return true;
  }
  if (name == "next-track" || name == "prev-track") {
    if (num_tracks <= 0) return true;
    advanceTrack(device_id, name == "next-track" ? 1 : -1, fallback_track_index, num_tracks);
    return true;
  }
  return false;
}

int
LaunchpadManager::resolveNote(const Song & song, int device_id, int track_id, int x, int y) const {
  auto track = song.getTrackByInternalId(track_id);
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

  auto track_ids = song.getRootTrackIds();

  auto device_id = ev.getDeviceIndex();

  // Send A/Send B/Send Main/Pan mode: the whole grid means something else
  // entirely while active (see LaunchpadManager::GridMode) - column x is
  // track_ids[x] (the first 8 root tracks, not this device's assigned
  // track), row y sets that track's send level or azimuth. Only a PRESS
  // does anything; RELEASE/AFTERTOUCH are swallowed too, never falling
  // through to note-entry below.
  auto grid_mode = gridMode(device_id);
  if (grid_mode != GridMode::NOTES) {
    if (ev.getKind() == LaunchpadPadEvent::PRESS && ev.getX() < 8) {
      // The first 8 columns must always be usable, even in a song that
      // doesn't have that many tracks yet - a Launchpad's physical layout
      // doesn't know or care how many tracks currently exist, so auto-create
      // plain InstrumentTracks (the same default 't' key/add-track uses) up
      // to the pressed column rather than silently doing nothing.
      while (static_cast<int>(track_ids.size()) <= ev.getX()) {
        song.addTrack(make_unique<InstrumentTrack>(0));
        track_ids = song.getRootTrackIds();
      }
      auto track = song.getTrackByInternalId(track_ids[static_cast<size_t>(ev.getX())]);
      if (track && (track->getType() == TrackType::INSTRUMENT_CONTROL || track->getType() == TrackType::PERCUSSION_CONTROL || track->getType() == TrackType::DRUM_MACHINE)) {
        auto track_id = track->getInternalId();
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
    }
    return;
  }

  // Mirrors the Send/Pan-mode auto-create above: a device's assigned track
  // (or the fallback) may point past however many tracks currently exist -
  // a brand-new/emptied song, or a device that was assigned to a track
  // index since deleted - so grow the song up to that index rather than
  // silently clamping back to the fallback track (which, for an empty
  // song, wouldn't exist either).
  auto track_index = assignedTrackIndex(device_id, fallback_track_index);
  if (track_index < 0) track_index = fallback_track_index;
  while (static_cast<int>(track_ids.size()) <= track_index) {
    song.addTrack(make_unique<InstrumentTrack>(0));
    track_ids = song.getRootTrackIds();
  }
  int track_id = track_ids[static_cast<size_t>(track_index)];

  // Step grid/drum picker: a DrumMachineTrack's grid means something else
  // entirely from ordinary chord entry, the same way Send/Pan mode
  // already short-circuits above.
  {
    auto assigned_track = song.getTrackByInternalId(track_id);
    if (assigned_track && assigned_track->getType() == TrackType::DRUM_MACHINE) {
      if (deviceState(device_id).picker_active) {
        handleDrumPickerPadEvent(ev, controller, static_cast<DrumMachineTrack &>(*assigned_track));
      } else {
        handleStepGridPadEvent(ev, controller, static_cast<DrumMachineTrack &>(*assigned_track), track_id);
      }
      return;
    }
  }

  auto note_value = resolveNote(song, device_id, track_id, ev.getX(), ev.getY());
  if (note_value < 0) return; // unused percussion pad (row 7), or an unpitched/degenerate tuning

  auto & scene = song.getScene(info.getPatternIndex());
  auto current_delay = info.getCurrentDelay();
  auto & event_queue = controller.getPlaybackEventQueue();

  if (ev.getKind() == LaunchpadPadEvent::PRESS) {
    auto row = info.getRowIndex();
    auto & state = deviceState(device_id);

    // Whether this press is about to become the first captured (Capture-
    // armed) held note anywhere - computed before recordActiveNote()
    // below adds this one, so it doesn't see itself. Drives the realtime
    // auto-play-while-held feature (see the RELEASE branch's matching
    // check) - only engages while Capture is actually armed on this
    // device, since the whole point is getting accurately-timed
    // recorded data; a pure-audition press has nothing to time-stamp.
    bool was_first_captured_note = state.capture_enabled && !anyCaptureArmedNoteHeld();

    // Engage realtime auto-play-while-held *before* the free-slot search
    // and this press's own write below, so - when this is the session-
    // starting press - ensureRowCleared() (just below) sees the fresh
    // auto_started_playback_ state in time to matter for both.
    // startAutoRecordSession() calls togglePlaying() (rather than pushing
    // a raw PLAY event), which also synchronously updates Controller's own
    // PlaybackInfo - info (bound by reference above) reflects
    // isPlaying()==true immediately, not just once the Player thread
    // eventually processes the event and reports back.
    if (was_first_captured_note && !info.isPlaying()) {
      controller.startAutoRecordSession(auto_started_playback_, auto_record_cleared_rows_, last_cleared_row_, last_cleared_pattern_idx_);
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

    // Free-slot search (mirrors Scene::pushNote), deliberately not
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
    auto & notes = scene.getNotes(row, track_id);
    int note_column = 0;
    while ((note_column < static_cast<int>(notes.size()) && notes[static_cast<size_t>(note_column)].isDefined()) ||
	   isColumnLiveHeld(track_id, note_column)) {
      note_column++;
    }

    auto velocity = LaunchpadProtocol::getModelInfo(ev.getModel()).velocity_sensitive ?
      static_cast<short>(ev.getVelocity()) : static_cast<short>(0x28); // same default as keyboard entry

    recordActiveNote(device_id, ev.getX(), ev.getY(), {note_column, row, track_id});

    if (state.capture_enabled) {
      Note note(note_value, velocity, current_delay);
      scene.setNote(row, track_id, note_column, note);
      song.incVersion();
    }

    event_queue.push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::PLAY_NOTE, track_id, note_column, note_value, velocity));

    // Deliberately NOT auto-advancing here (unlike single-note keyboard
    // entry): a chord is multiple near-simultaneous presses that must all
    // land on the *same* row - advancing per-press would spread a chord
    // across rows the moment any two presses straddle the (asynchronous)
    // MOVE_POSITION round-trip. Advance is deferred to RELEASE, once every
    // currently-held pad has been let go (see below) - matching how
    // Renoise's own "chord mode" treats simultaneously-pressed MIDI notes
    // as one gesture, not N independent steps. (With Capture armed and
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
    event_queue.push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::STOP_NOTE, held.track_id, held.note_column));

    if (state.capture_enabled) {
      if (info.isPlaying()) {
	// Live performance recording: write an explicit OFF at the row the
	// transport has since reached, mirroring handleMidiEvent's NOTE_OFF -
	// UNLESS that's still the same row the note itself is on. Per
	// Renoise's own pattern model (a single line can't hold both a note
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

    // Realtime auto-play-while-held: stop exactly when the last
    // Capture-armed held note anywhere releases, but only if this code
    // (not the user manually pressing Space) was the one that started
    // it - see auto_started_playback_'s own comment.
    // stopAutoRecordSession() itself handles only actually stopping if
    // it's still genuinely playing (the user may have manually stopped it
    // themselves in the meantime).
    if (auto_started_playback_ && !anyCaptureArmedNoteHeld()) {
      controller.stopAutoRecordSession(auto_started_playback_, auto_record_cleared_rows_, info);
    }
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
    event_queue.push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::NOTE_PRESSURE, held.track_id, held.note_column, note_value, ev.getVelocity()));

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
LaunchpadManager::handleStepGridPadEvent(LaunchpadPadEvent & ev, Controller & controller, DrumMachineTrack & track, int track_id) {
  auto & lane_notes = track.getLaneNotes();
  auto x = ev.getX(), y = ev.getY();
  if (y < 0 || y >= static_cast<int>(lane_notes.size()) || x < 0 || x >= track.getLoopLength()) return;
  int note = lane_notes[static_cast<size_t>(y)];

  auto & event_queue = controller.getPlaybackEventQueue();

  if (ev.getKind() == LaunchpadPadEvent::PRESS) {
    // Writes unconditionally, regardless of capture_enabled - "the arm
    // flag gates performance capture, not editing" (plans/drum-machine.md):
    // the step grid writes in both arm states, only free playing is gated.
    bool was_hit = (track.getSteps(note) & (1u << x)) != 0;
    track.setStep(note, x, !was_hit);
    controller.getSong().incVersion();

    // Auditions at a fixed velocity - pad pressure/aftertouch are both
    // ignored on this grid (no per-step velocity for the MVP). The GM
    // note number doubles as the column, matching SongState::renderBlock()'s
    // own step-driven emission (DrumMachineTrack.h) so editing and
    // sequenced playback choke/retrigger consistently. Clearing a step
    // (was_hit true) always auditions, transport running or not - there's
    // nothing else about to play it. Setting a step (was_hit false) only
    // auditions here when nothing is already going to hit it for real in
    // a moment: while the song is playing or the free-running audition
    // clock is looping, this exact lane/step is about to be triggered on
    // its own, at the actually-correct time - an immediate hit here would
    // land at a musically arbitrary point against that beat, on top of
    // (not instead of) the real one a moment later.
    bool suppress = !was_hit && (controller.getPlaybackInfo().isPlaying() || audition_clock_.isRunning());
    if (!suppress) {
      auto velocity = static_cast<short>(constants::DEFAULT_VELOCITY);
      event_queue.push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::PLAY_NOTE, track_id, note, note, velocity));
    }
  } else if (ev.getKind() == LaunchpadPadEvent::RELEASE) {
    // Always silence the live-audition voice, mirroring the NOTES-mode
    // RELEASE branch's own "always silence" comment above.
    event_queue.push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::STOP_NOTE, track_id, note));
  }
  // AFTERTOUCH: aftertouch is unused on this grid (plans/drum-machine.md) -
  // no-op, unlike ordinary NOTES-mode entry.
}

void
LaunchpadManager::handleDrumPickerPadEvent(LaunchpadPadEvent & ev, Controller & controller, DrumMachineTrack & track) {
  if (ev.getKind() != LaunchpadPadEvent::PRESS) return; // a plain tap - RELEASE/AFTERTOUCH are no-ops

  auto note = LaunchpadLayout::percussionNoteForPad(ev.getX(), ev.getY());
  if (note < 0) return; // unused pad in the free-drumming layout

  // addLane()/removeLane() apply the lane-list and step-data mutation
  // together, so there's no way to observe the two disagreeing - see
  // DrumMachineTrack.h's own comment. Removal is silent, no confirmation,
  // no undo, per the brief's own accepted risk for this gesture. addLane()
  // is itself a silent no-op once the track is already at
  // DrumMachineTrack::kMaxLanes (the step grid has exactly 8 rows to show
  // them in) - pressing an unlit pad while full just leaves it unlit,
  // same as pressing an already-assigned pad is already a no-op.
  bool was_assigned = track.hasLane(note);
  if (was_assigned) track.removeLane(note);
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
    controller.getPlaybackEventQueue().push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::PLAY_NOTE, track.getInternalId(), note, note, velocity));
  }
}

void
LaunchpadManager::triggerAuditionStep(const Song & song, const vector<int> & track_ids, Controller & controller, int step) {
  auto & event_queue = controller.getPlaybackEventQueue();
  for (auto track_id : track_ids) {
    auto track = song.getTrackByInternalId(track_id);
    if (!track || track->getType() != TrackType::DRUM_MACHINE) continue;
    auto & drum_track = static_cast<const DrumMachineTrack &>(*track);

    // Same PLAY_NOTE-with-note-as-column convention SongState::renderBlock()'s
    // own pattern-driven emission and the step grid's own audition press
    // already use (DrumMachineTrack.h) - no explicit STOP_NOTE, matching
    // that same precedent: a one-shot note-on per hit, relying on the
    // instrument's own envelope/choke machinery for anything past that.
    for (int note : drum_track.getHitNotesForRow(step)) {
      auto velocity = static_cast<short>(constants::DEFAULT_VELOCITY);
      event_queue.push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::PLAY_NOTE, track_id, note, note, velocity));
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
    event_queue.push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::CHANNEL_PRESSURE, track_id, ev.getVelocity()));
  }
}

void
LaunchpadManager::refreshLeds(int device_id, DeviceState & state) {
  vector<LaunchpadProtocol::PadColor> colors;

  if (state.grid_mode == GridMode::DRAW) {
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
  } else if (state.grid_mode != GridMode::NOTES) {
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
  } else if (state.assigned_track_is_drum_machine && state.picker_active) {
    // Drum picker (plans/drum-machine.md, Phase 6): the free-drumming
    // layout doubles as the picker surface, reusing the exact same
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
  } else if (state.assigned_track_is_drum_machine) {
    // Step grid (plans/drum-machine.md, Phase 5) - not a GridMode value of
    // its own, displays automatically whenever the assigned track is a
    // DrumMachineTrack (see this device's own assigned_track_is_drum_machine
    // comment). Rows are lanes (y=0 bottom = drum_lane_notes[0], the
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
        if (y < static_cast<int>(state.drum_lane_notes.size()) && x < state.drum_loop_length) {
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
      auto base_note = tonic + (state.octave + 1) * edo_steps;

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

  // Extra-button LEDs (see the Launchpad follow-up plan's button table).
  // CC numbers unreachable on X/Mini MK3 (30, 20 - Pro MK3's left column)
  // are harmless to include here: those models simply don't have the
  // physical button, so the colourspec entry has nothing to light.
  colors.push_back({91, 30, 30, 30}); // move-row-up, dim white (static)
  colors.push_back({92, 30, 30, 30}); // move-row-down, dim white (static)
  colors.push_back({93, 0, 0, 60});   // prev-track, dim blue (static)
  colors.push_back({94, 0, 0, 60});   // next-track, dim blue (static)
  colors.push_back({95, 0, 0, 0});    // reserved (Session, inferred)
  colors.push_back({96, 0, 0, 0});    // reserved (Note, inferred)
  // Custom (CC97) is the drum-picker latch (plans/drum-machine.md, Phase
  // 6) - lit when active, matching the active-state convention Mute/Solo
  // below already use, not the static/no-state convention the Send/Pan
  // mode buttons use (those repaint the whole grid as their own
  // confirmation; the picker's own grid repaint isn't as visually
  // distinct at a glance, so the button itself carries the state too).
  colors.push_back({97, state.picker_active ? uint8_t(90) : uint8_t(20), 0, state.picker_active ? uint8_t(127) : uint8_t(20)});
  // CC98 ("Capture MIDI") is reserved/unused again - the record-armed
  // indicator moved to CC19 ("Record Arm", right column - see below) per
  // plans/drum-machine.md's own rationale (DeviceState::capture_enabled's
  // comment has the full reasoning).
  colors.push_back({98, 0, 0, 0}); // reserved
  // 99 (top-right corner, the grid position the Programmer-mode protocol
  // maps one past the 91-98 top row) isn't actually a pressable button on
  // real Launchpad X hardware - see handleRawButton()'s own comment - so
  // it's left off/reserved rather than wired to reflect any state.
  colors.push_back({99, 0, 0, 0});
  // Right column: 89/79/69/59 are the Volume/Pan/Send A/Send B mode
  // buttons (Volume repurposed as Send Main - see handleRawButton()) -
  // static colors (matching each mode's own grid base color, dimmed), no
  // active/inactive state needed (see the grid-mode painting above, whose
  // own repaint is the confirmation a press registered). 39/29 are Mute/
  // Solo (real commands, not a mode toggle - see LaunchpadProtocol::
  // commandForButton) and do need active-state colors, matching the
  // Pro-MK3-left-column entries' own convention exactly. 19 is Record Arm -
  // reuses the red Capture-MIDI LED used to show (see 39/30, which moved
  // to blue to make room) now that the toggle itself lives here instead of
  // CC98. 49 is Stop Clip - DRAW mode's toggle when the assigned track
  // isn't a DrumMachineTrack (moved here from Custom, which the picker
  // now owns unconditionally), reserved/inert for the drum machine's own
  // Clear gesture when it is (plans/drum-machine.md Phase 7, not
  // implemented yet) - static dim white either way for now, matching
  // DRAW's own prior static (no-active-state) convention; Phase 7 can
  // give it a real confirm-armed indicator once Clear exists.
  colors.push_back({19, state.capture_enabled ? uint8_t(127) : uint8_t(20), 0, 0}); // record-arm toggle
  colors.push_back({29, state.solo ? uint8_t(127) : uint8_t(20), state.solo ? uint8_t(127) : uint8_t(20), 0}); // toggle-solo
  colors.push_back({39, 0, 0, state.muted ? uint8_t(127) : uint8_t(20)}); // toggle-mute (blue - red moved to Record Arm, CC19)

  // Stop Clip (CC49): DRAW mode's static dim-white toggle indicator when
  // the assigned track isn't a DrumMachineTrack (unchanged); the Clear
  // gesture's own confirm-armed indicator when it is (plans/drum-
  // machine.md Phase 7) - dim red when idle (distinct from DRAW's dim
  // white, so the button visibly means something different here), and
  // blinking bright/dim red while armed (kClearConfirmBlinkPeriod's own
  // half-period) rather than a real hardware "pulse" LED mode - this file
  // only ever speaks the static per-LED RGB SysEx message
  // (LaunchpadProtocol::buildRgbLedSysEx), never Programmer Mode's
  // separate flashing/pulsing LED message type, which hasn't been
  // confirmed against real hardware - blinking by changing the sent color
  // every refreshLeds() call (this already runs many times a second) gets
  // the same visible effect without a second, unconfirmed protocol path.
  // Also auto-expires a stale arm here (not just inside
  // handleStopClipButton()'s own check) so the indicator honestly returns
  // to idle once the window lapses even if no second press ever comes.
  state.clear_confirm.expireIfStale(std::chrono::steady_clock::now(), kClearConfirmWindow);
  if (!state.assigned_track_is_drum_machine) {
    colors.push_back({49, 60, 60, 60}); // draw-mode toggle, dim white (static)
  } else if (!state.clear_confirm.isArmed()) {
    colors.push_back({49, 40, 0, 0}); // Clear, idle - dim red
  } else {
    auto elapsed = std::chrono::steady_clock::now() - state.clear_confirm.armedTime();
    auto half_cycles = elapsed / kClearConfirmBlinkPeriod;
    bool bright_phase = (half_cycles % 2) == 0;
    colors.push_back({49, bright_phase ? uint8_t(127) : uint8_t(30), 0, 0});
  }
  colors.push_back({59, 40, 0, 40});  // Send B physical button -> send-b-mode, dim magenta (static)
  colors.push_back({69, 0, 40, 40});  // Send A physical button -> send-a-mode, dim cyan (static)
  colors.push_back({79, 40, 20, 0});  // Pan physical button -> pan-mode, dim orange (static)
  colors.push_back({89, 40, 40, 0});  // Volume physical button -> send-main-mode, dim yellow (static)
  colors.push_back({30, 0, 0, state.muted ? uint8_t(127) : uint8_t(20)}); // toggle-mute (Pro MK3 left column pos. 6; blue - see CC39)
  colors.push_back({20, state.solo ? uint8_t(127) : uint8_t(20), state.solo ? uint8_t(127) : uint8_t(20), 0}); // toggle-solo (Pro MK3 left column pos. 7)

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
LaunchpadManager::refresh(const Song & song, const vector<int> & track_ids, const PlaybackInfo & playback_info, int fallback_track_index, Controller & controller) {
  if (!launchpad_io_) return;
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

  // Phase 7's free-running drum-machine audition clock - computed once
  // here, shared by every connected device below, not per-device.
  // audition_clock_ itself (StepClock, LaunchpadTiming.h) is the pure,
  // unit-tested step-advance logic; everything here is just wall-clock
  // bookkeeping and plugging the real song/track data in. Active exactly
  // while the transport is stopped and Record Arm is off (per the plan's
  // own "Extend audition (Capture off)" framing - Record Arm is a single
  // global flag, not per-device, see capture_enabled_'s own comment) -
  // while playing, the pattern-driven
  // path in SongState::renderBlock() already triggers these same tracks from
  // real song position, and running both at once would double-trigger;
  // while armed, the player is presumably about to record something
  // deliberate and doesn't want an uncontrolled loop underneath it.
  // audition_step is this frame's step for the per-device playhead
  // display below, or -1 when the clock isn't running at all.
  int audition_step = -1;
  bool audition_active = !playback_info.isPlaying() && !capture_enabled_;
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
      triggerAuditionStep(song, track_ids, controller, audition_clock_.currentStep());
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
        triggerAuditionStep(song, track_ids, controller, step);
      }
    }
    audition_step = audition_clock_.currentStep();
  } else {
    audition_clock_.stop();
  }

  // The Send A/Send B/Send Main/Pan grid modes always address the first 8
  // root tracks (not whichever track a device happens to be assigned to) -
  // the same values apply to every connected device, computed once here
  // rather than per-device inside the loop below.
  array<float, 8> track_send_main{}, track_send_a{}, track_send_b{}, track_azimuth{};
  for (int i = 0; i < 8 && i < num_tracks; i++) {
    auto track = song.getTrackByInternalId(track_ids[static_cast<size_t>(i)]);
    if (track && (track->getType() == TrackType::INSTRUMENT_CONTROL || track->getType() == TrackType::PERCUSSION_CONTROL || track->getType() == TrackType::DRUM_MACHINE)) {
      auto & instrument_track = dynamic_cast<const InstrumentTrack&>(*track);
      track_send_main[static_cast<size_t>(i)] = instrument_track.getSends().main;
      track_send_a[static_cast<size_t>(i)] = instrument_track.getSends().a;
      track_send_b[static_cast<size_t>(i)] = instrument_track.getSends().b;
      track_azimuth[static_cast<size_t>(i)] = instrument_track.getAzimuth();
    }
  }

  for (auto device_id : ready_ids) {
    // A device not already in devices_ is being seen for the first time
    // this session (freshly connected, or reconnected after having been
    // pruned above on an earlier disconnect) - deviceState() below is
    // about to default-construct its DeviceState, octave included, so
    // this is the one moment to apply defaultOctaveOffsetForModel()'s
    // per-model starting point instead of leaving every device at the
    // same default register.
    bool is_new_device = devices_.find(device_id) == devices_.end();
    auto & state = deviceState(device_id);
    if (is_new_device) {
      if (auto model = launchpad_io_->modelForSession(device_id)) {
        state.octave = LaunchpadLayout::clampOctave(state.octave, defaultOctaveOffsetForModel(*model));
      }
    }

    auto track_index = assignedTrackIndex(device_id, fallback_track_index);
    if (track_index < 0 || track_index >= num_tracks) track_index = fallback_track_index;

    Tuning tuning = Tuning::TET12;
    int key_val = -1;
    bool muted = false, solo = false;
    unordered_map<int, float> active_note_loudness;
    bool is_drum_machine = false;
    vector<int> drum_lane_notes;
    array<uint8_t, 8> drum_lane_steps {};
    int drum_loop_length = 8;
    int drum_playhead_step = -1;
    if (track_index >= 0 && track_index < num_tracks) {
      auto track_id = track_ids[static_cast<size_t>(track_index)];
      auto track = song.getTrackByInternalId(track_id);
      tuning = track && track->getType() == TrackType::PERCUSSION_CONTROL ? Tuning::PERCUSSION : song.getTuning();
      key_val = song.getKey();
      if (track && (track->getType() == TrackType::INSTRUMENT_CONTROL || track->getType() == TrackType::PERCUSSION_CONTROL || track->getType() == TrackType::DRUM_MACHINE)) {
        auto & instrument_track = dynamic_cast<const InstrumentTrack&>(*track);
        muted = instrument_track.isMuted();
        solo = instrument_track.isSolo();
      }
      is_drum_machine = track && track->getType() == TrackType::DRUM_MACHINE;
      if (is_drum_machine) {
        auto & drum_track = static_cast<const DrumMachineTrack &>(*track);
        drum_lane_notes = drum_track.getLaneNotes();
        drum_loop_length = drum_track.getLoopLength();
        for (size_t i = 0; i < drum_lane_notes.size() && i < drum_lane_steps.size(); i++) {
          drum_lane_steps[i] = drum_track.getSteps(drum_lane_notes[i]);
        }
        // While playing, the real song position; while stopped, the
        // free-running audition clock's own shared step (Phase 7,
        // audition_step above) - or no playhead at all if that clock
        // isn't currently running either (Record Arm is on).
        if (playback_info.isPlaying() && drum_loop_length > 0) {
          drum_playhead_step = playback_info.getRowIndex() % drum_loop_length;
        } else if (audition_step >= 0 && drum_loop_length > 0) {
          drum_playhead_step = audition_step % drum_loop_length;
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
    state.capture_enabled = capture_enabled_; // mirrors the one song-wide flag - see its own comment
    state.tuning = tuning;
    state.key = key_val;
    state.muted = muted;
    state.solo = solo;
    state.active_note_loudness = move(active_note_loudness);
    state.track_send_main = track_send_main;
    state.track_send_a = track_send_a;
    state.track_send_b = track_send_b;
    state.track_azimuth = track_azimuth;
    state.grid_track_count = min(8, num_tracks);
    state.assigned_track_is_drum_machine = is_drum_machine;
    state.drum_lane_notes = move(drum_lane_notes);
    state.drum_lane_steps = drum_lane_steps;
    state.drum_loop_length = drum_loop_length;
    state.drum_playhead_step = drum_playhead_step;

    refreshLeds(device_id, state);
  }
}
