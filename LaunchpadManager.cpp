#include "LaunchpadManager.h"

#include "LaunchpadIO.h"
#include "LaunchpadLayout.h"
#include "LaunchpadProtocol.h"
#include "LaunchpadPadEvent.h"
#include "PlaybackInfo.h"
#include "PlaybackControlEvent.h"
#include "Song.h"
#include "InstrumentTrack.h"
#include "Controller.h"

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
  // LaunchpadLayout's kFourthFifthHue comment) - user feedback: "could
  // share the same color for now", slightly desaturated per later
  // feedback (a touch less vivid than tonic, still clearly prominent).
  constexpr float kConsonanceFourthFifthSaturation = 0.8f;
  // Only depths 3-4 (RECURSIVE tier) get their own distinguishable hue -
  // depth 3 ("somewhat prominent still") at higher saturation, depth 4
  // clearly more muted (also given a much wider hue separation from depth
  // 3 than the drift's own step size would otherwise give - see
  // LaunchpadLayout's kDepth3HueOffset/kDepth4HueOffset comment - closely
  // spaced hues within the same family were hard to tell apart on real
  // hardware, e.g. user feedback that A and B, both major-family in the
  // key of C 31-EDO, looked nearly identical). Depth 5+ is capped to one
  // flat, deliberately unprominent gray (achromatic - saturation 0, hue
  // irrelevant) rather than continuing to differentiate ever-closer hues.
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

  // Pan mode maps a track's azimuth to one of 8 compass points spaced 45
  // degrees apart around the *full* circle (not clamped to a stereo-like
  // +-90 range) - this is a full 3D ambisonic engine, not a stereo panner,
  // so "behind" positions are just as reachable as "in front" ones. Row 4
  // (dead center of the 8) lands exactly on 0 degrees (front) for a
  // memorable, symmetric mapping.
  constexpr float PAN_ROW_DEGREES = 45.0f;
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
  return track_ids[track_index];
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
  // 98 (top row 8, printed with a record-circle icon) is the real
  // Launchpad X's own dedicated "Capture MIDI" button - a per-device
  // record-arm toggle, not a Song/Track-mutating command, so it's a
  // direct hardware-state flip here rather than a named command (same
  // shape as the grid-mode toggles above). Used to be wired to
  // toggle-playing via the named-command pipeline - see
  // LaunchpadProtocol::commandForButton's own comment; toggle-playing
  // stays reachable via Space.
  if (cc_number == 98) {
    auto & state = deviceState(device_id);
    state.capture_enabled = !state.capture_enabled;
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
LaunchpadManager::ensureRowCleared(Song & song, int pattern_idx, int row, int track_id) {
  if (!auto_record_cleared_rows_.insert({row, track_id}).second) return; // already cleared this session
  auto & pattern = song.getPattern(pattern_idx);
  pattern.setNotes(row, track_id, {});
  song.incVersion();
}

void
LaunchpadManager::onRowAdvanced(Controller & controller) {
  if (!auto_started_playback_) return;

  auto & info = controller.getPlaybackInfo();
  auto new_row = info.getRowIndex();
  auto pattern_idx = info.getPatternIndex();

  if (pattern_idx != last_cleared_pattern_idx_ || new_row < last_cleared_row_) {
    // Pattern changed, or the row went backwards (a loop/pattern-sequence
    // wraparound) - resync to just this row rather than trying to
    // backfill a range spanning the boundary, which has no single
    // well-defined meaning here.
    last_cleared_row_ = new_row - 1;
    last_cleared_pattern_idx_ = pattern_idx;
  }
  if (new_row <= last_cleared_row_) return; // nothing new to sweep

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

  auto & song = controller.getSong();
  for (int row = last_cleared_row_ + 1; row <= new_row; row++) {
    for (auto track_id : track_ids) {
      ensureRowCleared(song, pattern_idx, row, track_id);
    }
  }
  last_cleared_row_ = new_row;
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
  // reproduces that same baseline; +1 further octave on top of that per
  // user feedback (the "octave*N" register alone was still too low to be
  // comfortably useful on the Launchpad specifically).
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
      if (track && (track->getType() == TrackType::INSTRUMENT_CONTROL || track->getType() == TrackType::PERCUSSION_CONTROL)) {
        auto track_id = track->getInternalId();
        if (grid_mode == GridMode::SEND_A) {
          controller.setTrackSendA(track_id, static_cast<float>(ev.getY()) / 7.0f);
        } else if (grid_mode == GridMode::SEND_B) {
          controller.setTrackSendB(track_id, static_cast<float>(ev.getY()) / 7.0f);
        } else if (grid_mode == GridMode::SEND_MAIN) {
          controller.setTrackSendMain(track_id, static_cast<float>(ev.getY()) / 7.0f);
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

  auto note_value = resolveNote(song, device_id, track_id, ev.getX(), ev.getY());
  if (note_value < 0) return; // unused percussion pad (row 7), or an unpitched/degenerate tuning

  auto & pattern = song.getPattern(info.getPatternIndex());
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
    if (was_first_captured_note && !info.isPlaying()) {
      // togglePlaying() (rather than a raw PLAY push) also synchronously
      // updates Controller's own PlaybackInfo - info (bound by reference
      // above) reflects isPlaying()==true immediately, not just once the
      // Player thread eventually processes the event and reports back.
      controller.togglePlaying();
      // Mutes only the song's own pattern-driven scheduling (SongState::
      // render()'s own comment has the full reasoning) - never the live
      // PLAY_NOTE/STOP_NOTE/NOTE_PRESSURE path this press's own sound
      // comes through, so the player's own performance is unaffected.
      event_queue.push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::SET_RECORDING_MUTE, 1));
      auto_started_playback_ = true;
      auto_record_cleared_rows_.clear();
      last_cleared_row_ = -1;
      last_cleared_pattern_idx_ = -1;
    }

    // Whole-row replace semantics for a live take: idempotent (see its
    // own comment), so calling it defensively is safe - only actually
    // does anything the first time (row, track_id) is touched this
    // session. Cleared *before* the free-slot search just below, not
    // after - otherwise a column still holding an old, about-to-be-
    // erased note reads as "taken" and gets skipped past, when the old
    // note is actually gone (or about to be, from this same call) and
    // the new one should be free to land in the very first column.
    if (state.capture_enabled && auto_started_playback_) ensureRowCleared(song, info.getPatternIndex(), row, track_id);

    // Free-slot search (mirrors Pattern::pushNote), deliberately not
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
    auto & notes = pattern.getNotes(row, track_id);
    int note_column = 0;
    while ((note_column < static_cast<int>(notes.size()) && notes[note_column].isDefined()) ||
	   isColumnLiveHeld(track_id, note_column)) {
      note_column++;
    }

    auto velocity = LaunchpadProtocol::getModelInfo(ev.getModel()).velocity_sensitive ?
      static_cast<short>(ev.getVelocity()) : static_cast<short>(0x28); // same default as keyboard entry

    recordActiveNote(device_id, ev.getX(), ev.getY(), {note_column, row, track_id});

    if (state.capture_enabled) {
      Note note(note_value, velocity, current_delay);
      pattern.setNote(row, track_id, note_column, note);
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
	  if (auto_started_playback_) ensureRowCleared(song, info.getPatternIndex(), release_row, held.track_id);
	  pattern.setNote(release_row, held.track_id, held.note_column, Note(0, 0, current_delay));
	  song.incVersion();
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
    // it - see auto_started_playback_'s own comment. The session is
    // considered over either way once this fires (flag cleared
    // regardless) - but only actually call togglePlaying() (which flips
    // whatever the *current* state is) if it's still genuinely playing;
    // otherwise the user must have manually stopped it themselves in the
    // meantime, and toggling again here would incorrectly restart it.
    if (auto_started_playback_ && !anyCaptureArmedNoteHeld()) {
      if (info.isPlaying()) {
	controller.togglePlaying();
	// Land past the just-written final OFF, not directly on it, so the
	// cursor is ready for whatever comes next - the same "advance once
	// you're done" step-entry already gives an ordinary note, just
	// triggered once here for the whole take instead of after every
	// row. Only when *we* actually stopped it here - if the user had
	// already manually stopped the transport before this release, the
	// cursor is wherever they left it and shouldn't be moved out from
	// under them. An *absolute* SET_POSITION, not a relative
	// MOVE_POSITION(1) - the audio thread keeps advancing in real time
	// for however long this event takes to actually reach it, so "+1
	// from wherever it's drifted to by then" occasionally landed two
	// rows past the OFF instead of one; "+1 from the exact row this
	// snapshot (info) saw" doesn't have that problem. See SongState::
	// setPosition()'s own comment for the full reasoning.
	controller.setEditPosition(info.getAbsolutePosition() + 1);
      }
      // Unconditional, regardless of the isPlaying() check above: if the
      // user manually stopped the transport themselves mid-hold,
      // recording_muted_ would otherwise stay stuck true (nothing else
      // ever clears it), silently muting their next ordinary, manually-
      // started playback.
      event_queue.push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::SET_RECORDING_MUTE, 0));
      auto_started_playback_ = false;
      auto_record_cleared_rows_.clear(); // not required for correctness (the next session's own start resets this too) - just don't hold onto a finished session's bookkeeping longer than needed
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
    if (auto_started_playback_) ensureRowCleared(song, info.getPatternIndex(), target_row, held.track_id);
    auto note = pattern.getNote(target_row, held.track_id, held.note_column);
    if (!note.isDefined()) note.setDelay(current_delay);
    note.setVelocity(static_cast<short>(ev.getVelocity()));
    pattern.setNote(target_row, held.track_id, held.note_column, note);
    song.incVersion();
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
    // bottom-up as a bargraph of that track's current send level (0-7 ->
    // 0.0-1.0) - a magnitude (Send Main's own zero-config value, 1.0, so
    // shows fully filled until turned down). Pan lights only the one row
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
                            : static_cast<int>(lround(values[static_cast<size_t>(x)] * 7.0f));
      for (int y = 0; y < 8; y++) {
        bool lit = has_track && (is_pan ? (y == lit_row) : (y <= lit_row));
        Rgb color = lit ? base : Rgb{0, 0, 0};
        colors.push_back({LaunchpadProtocol::padToNoteNumber(x, y), color.r, color.g, color.b});
      }
    }
  } else if (state.tuning == Tuning::PERCUSSION) {
    for (int y = 0; y < 8; y++) {
      for (int x = 0; x < 8; x++) {
        uint8_t r, g, b;
        switch (LaunchpadLayout::percussionFamilyForPad(x, y)) {
        case LaunchpadLayout::PercussionFamily::CORE:       r = 127; g = 0;   b = 0;   break;
        case LaunchpadLayout::PercussionFamily::HI_HAT:     r = 127; g = 127; b = 0;   break;
        case LaunchpadLayout::PercussionFamily::TOMS:       r = 127; g = 50;  b = 0;   break;
        case LaunchpadLayout::PercussionFamily::CYMBALS:    r = 100; g = 100; b = 127; break;
        case LaunchpadLayout::PercussionFamily::HAND_PERC:  r = 0;   g = 127; b = 0;   break;
        case LaunchpadLayout::PercussionFamily::LATIN:      r = 80;  g = 0;   b = 127; break;
        case LaunchpadLayout::PercussionFamily::WHISTLE:    r = 127; g = 0;   b = 80;  break;
        case LaunchpadLayout::PercussionFamily::ELECTRONIC: r = 40;  g = 40;  b = 40;  break;
        default:                                             r = 0;   g = 0;   b = 0;   break; // UNUSED (row 7)
        }
        auto note = LaunchpadLayout::percussionNoteForPad(x, y);
        auto color = padColor({r, g, b}, state.active_note_loudness, note);
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
  colors.push_back({97, 60, 60, 60}); // Custom physical button (inferred) -> draw-mode, dim white (static)
  // Reuses the red Mute's own LED used to show (see 39/30 below, which
  // moved to blue to make room) - Capture is now the record-armed
  // indicator, not a play/stop indicator (toggle-playing itself is still
  // reachable via Space, just no longer shown here).
  colors.push_back({98, state.capture_enabled ? uint8_t(127) : uint8_t(20), 0, 0}); // capture-armed toggle
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
  // Pro-MK3-left-column entries' own convention exactly. 19/49 (Record
  // Arm/Stop Clip) stay reserved.
  colors.push_back({19, 0, 0, 0});    // reserved
  colors.push_back({29, state.solo ? uint8_t(127) : uint8_t(20), state.solo ? uint8_t(127) : uint8_t(20), 0}); // toggle-solo
  colors.push_back({39, 0, 0, state.muted ? uint8_t(127) : uint8_t(20)}); // toggle-mute (blue - red moved to Capture, CC98)
  colors.push_back({49, 0, 0, 0});    // reserved
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
LaunchpadManager::refresh(const Song & song, const vector<int> & track_ids, const PlaybackInfo & playback_info, int fallback_track_index) {
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

  // The Send A/Send B/Send Main/Pan grid modes always address the first 8
  // root tracks (not whichever track a device happens to be assigned to) -
  // the same values apply to every connected device, computed once here
  // rather than per-device inside the loop below.
  array<float, 8> track_send_main{}, track_send_a{}, track_send_b{}, track_azimuth{};
  for (int i = 0; i < 8 && i < num_tracks; i++) {
    auto track = song.getTrackByInternalId(track_ids[static_cast<size_t>(i)]);
    if (track && (track->getType() == TrackType::INSTRUMENT_CONTROL || track->getType() == TrackType::PERCUSSION_CONTROL)) {
      auto & instrument_track = dynamic_cast<const InstrumentTrack&>(*track);
      track_send_main[static_cast<size_t>(i)] = instrument_track.getSends().main;
      track_send_a[static_cast<size_t>(i)] = instrument_track.getSends().a;
      track_send_b[static_cast<size_t>(i)] = instrument_track.getSends().b;
      track_azimuth[static_cast<size_t>(i)] = instrument_track.getAzimuth();
    }
  }

  for (auto device_id : ready_ids) {
    auto & state = deviceState(device_id);

    auto track_index = assignedTrackIndex(device_id, fallback_track_index);
    if (track_index < 0 || track_index >= num_tracks) track_index = fallback_track_index;

    Tuning tuning = Tuning::TET12;
    int key_val = -1;
    bool muted = false, solo = false;
    unordered_map<int, float> active_note_loudness;
    if (track_index >= 0 && track_index < num_tracks) {
      auto track_id = track_ids[track_index];
      auto track = song.getTrackByInternalId(track_id);
      tuning = track && track->getType() == TrackType::PERCUSSION_CONTROL ? Tuning::PERCUSSION : song.getTuning();
      key_val = song.getKey();
      if (track && (track->getType() == TrackType::INSTRUMENT_CONTROL || track->getType() == TrackType::PERCUSSION_CONTROL)) {
        auto & instrument_track = dynamic_cast<const InstrumentTrack&>(*track);
        muted = instrument_track.isMuted();
        solo = instrument_track.isSolo();
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

    refreshLeds(device_id, state);
  }
}
