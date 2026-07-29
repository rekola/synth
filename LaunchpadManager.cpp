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
  // Fokker organ / Archiphone style landmark colors (see LaunchpadLayout::
  // PadCategory) - brightness carries the hierarchy: 127 for the diatonic
  // anchors, 70 in the blue channel for diesis (31-EDO's characteristic
  // in-between notes), 70 in red/amber/magenta for the dimmer, more
  // "ordinary" chromatic/accidental classes.
  constexpr Rgb FOKKER_TONIC      = {0,   127, 0};   // bright green - strongest landmark
  constexpr Rgb FOKKER_DIATONIC   = {127, 127, 0};   // bright yellow - other scale degrees
  constexpr Rgb FOKKER_SHARP      = {70,  0,   0};   // dim red
  constexpr Rgb FOKKER_FLAT       = {70,  35,  0};   // dim amber/orange
  constexpr Rgb FOKKER_DIESIS     = {0,   70,  127}; // medium blue
  constexpr Rgb FOKKER_ACCIDENTAL = {70,  0,   70};  // dim magenta - ambiguous tie (e.g. 12edo black keys)

  // DRAW mode's coloring-toy palette - a plain rainbow, cycling back to
  // off. Order/values are not meaningful the way the Fokker colors above
  // are (no music-theory landmark to preserve), just distinct and bright
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
  // loudness - hue/saturation come from the base Fokker/percussion color
  // and are otherwise untouched, only lightness ramps between the two as
  // that voice's loudness (its own gain, decaying with any envelope it
  // has) moves from 0 to 1.
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
  return false;
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
  auto tonic = song.getKey() >= 0 ? song.getKey() : 0;
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

    // Free-slot search (mirrors Pattern::pushNote), deliberately not
    // "map size" the way active_midi_notes assigns columns - that has a
    // latent collision bug on non-LIFO release order, which is the common
    // case for a chordally-played grid controller (see the plan's design
    // decision 3).
    auto & notes = pattern.getNotes(row, track_id);
    int note_column = static_cast<int>(notes.size());
    for (int i = 0; i < static_cast<int>(notes.size()); i++) {
      if (!notes[i].isDefined()) {
	note_column = i;
	break;
      }
    }
    recordActiveNote(device_id, ev.getX(), ev.getY(), {note_column, row, track_id});

    auto velocity = LaunchpadProtocol::getModelInfo(ev.getModel()).velocity_sensitive ?
      static_cast<short>(ev.getVelocity()) : static_cast<short>(0x28); // same default as keyboard entry

    Note note(note_value, velocity, current_delay);
    pattern.setNote(row, track_id, note_column, note);
    song.incVersion();

    event_queue.push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::PLAY_NOTE, track_id, note_column, note_value, velocity));

    // Deliberately NOT auto-advancing here (unlike single-note keyboard
    // entry): a chord is multiple near-simultaneous presses that must all
    // land on the *same* row - advancing per-press would spread a chord
    // across rows the moment any two presses straddle the (asynchronous)
    // MOVE_POSITION round-trip. Advance is deferred to RELEASE, once every
    // currently-held pad has been let go (see below) - matching how
    // Renoise's own "chord mode" treats simultaneously-pressed MIDI notes
    // as one gesture, not N independent steps.
  } else if (ev.getKind() == LaunchpadPadEvent::RELEASE) {
    auto held_ptr = findActiveNote(device_id, ev.getX(), ev.getY());
    if (!held_ptr) return;
    auto held = *held_ptr;
    clearActiveNote(device_id, ev.getX(), ev.getY());

    // Always silence the live-audition voice.
    event_queue.push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::STOP_NOTE, held.track_id, held.note_column));

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
	pattern.setNote(release_row, held.track_id, held.note_column, Note(0, 0, current_delay));
	song.incVersion();
      }
    } else if (!hasAnyActiveNotes(device_id)) {
      // Step entry: advance once the whole chord gesture has been
      // released on *this* device (not per pad - see the PRESS branch;
      // and scoped to this device, not every connected Launchpad, so one
      // device's chord release doesn't prematurely advance while another
      // device is still mid-chord), so the next tap/chord lands on a
      // fresh row instead of piling onto this one.
      event_queue.push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::MOVE_POSITION, edit_step_size));
    }
  } else if (ev.getKind() == LaunchpadPadEvent::AFTERTOUCH) {
    // Mini MK3 never emits this (no pressure sensing); defensive check
    // anyway in case a future model reports itself incorrectly.
    if (!LaunchpadProtocol::getModelInfo(ev.getModel()).poly_aftertouch) return;

    auto held_ptr = findActiveNote(device_id, ev.getX(), ev.getY());
    if (!held_ptr) return; // no held note to modulate
    auto & held = *held_ptr;

    // Live modulation always happens, regardless of the write-throttle
    // below - mirrors handleMidiEvent's NOTE_PRESSURE handling exactly.
    event_queue.push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::NOTE_PRESSURE, held.track_id, held.note_column, note_value, ev.getVelocity()));

    // Rate-limit the persisted pattern write: Pattern::setNote already
    // overwrites in place (so "one aftertouch object per column per row" is
    // free), this threshold purely avoids redundant work/redraw churn for
    // a dense pressure stream, not a correctness requirement.
    const int aftertouch_threshold = 4;
    auto delta = ev.getVelocity() - held.last_aftertouch_value;
    if (delta < 0) delta = -delta;
    if (delta < aftertouch_threshold) return;
    held.last_aftertouch_value = ev.getVelocity();

    // While playing, modulate the currently-sounding row (transport has
    // moved on, matching handleMidiEvent); while stopped, modulate the
    // row the note actually landed on (step entry already advanced past
    // it - see the RELEASE branch above for why using the live row would
    // be wrong here too).
    auto target_row = info.isPlaying() ? info.getRowIndex() : held.row;
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
      auto tonic = state.key >= 0 ? state.key : 0;
      // Matches resolveNote's base_note (see its comment) - purely for
      // consistency, since classifyPad's result is invariant to a uniform
      // octave shift of base_note anyway.
      auto base_note = tonic + (state.octave + 1) * edo_steps;

      for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
          Rgb color;
          if (basis.degenerate) {
            color = {40, 40, 40}; // degraded/fallback visual mode - no meaningful scale structure
          } else {
            switch (LaunchpadLayout::classifyPad(basis, edo_steps, x - GRID_ORIGIN_X, y - GRID_ORIGIN_Y, base_note)) {
            case LaunchpadLayout::PadCategory::TONIC:      color = FOKKER_TONIC;      break;
            case LaunchpadLayout::PadCategory::DIATONIC:   color = FOKKER_DIATONIC;   break;
            case LaunchpadLayout::PadCategory::SHARP:      color = FOKKER_SHARP;      break;
            case LaunchpadLayout::PadCategory::FLAT:       color = FOKKER_FLAT;       break;
            case LaunchpadLayout::PadCategory::DIESIS:     color = FOKKER_DIESIS;     break;
            case LaunchpadLayout::PadCategory::ACCIDENTAL: color = FOKKER_ACCIDENTAL; break;
            }
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
  colors.push_back({98, state.playing ? uint8_t(0) : uint8_t(20), state.playing ? uint8_t(127) : uint8_t(20), state.playing ? uint8_t(0) : uint8_t(20)}); // toggle-playing/record
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
  colors.push_back({39, state.muted ? uint8_t(127) : uint8_t(20), 0, 0}); // toggle-mute
  colors.push_back({49, 0, 0, 0});    // reserved
  colors.push_back({59, 40, 0, 40});  // Send B physical button -> send-b-mode, dim magenta (static)
  colors.push_back({69, 0, 40, 40});  // Send A physical button -> send-a-mode, dim cyan (static)
  colors.push_back({79, 40, 20, 0});  // Pan physical button -> pan-mode, dim orange (static)
  colors.push_back({89, 40, 40, 0});  // Volume physical button -> send-main-mode, dim yellow (static)
  colors.push_back({30, state.muted ? uint8_t(127) : uint8_t(20), 0, 0}); // toggle-mute (Pro MK3 left column pos. 6)
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
  bool playing = playback_info.isPlaying();
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
    state.playing = playing;
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
