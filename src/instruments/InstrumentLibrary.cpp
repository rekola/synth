#include "InstrumentLibrary.h"

#include "InstrumentProvider.h"
#include "PadSynth.h"
#include "Additive.h"
#include "../effects/EnvelopeFilter.h"
#include "../effects/TapeDegradation.h"
#include "../effects/BiquadFilter.h"
#include "../state/MemoryParameterSource.h"

#include <memory>
#include <string>

using namespace std;

namespace {

unique_ptr<EnvelopeFilter> makeEnvelope(float attack, float hold, float decay, float sustain, float release) {
  auto env = make_unique<EnvelopeFilter>();
  MemoryParameterSource params;
  params.set("attack", attack);
  params.set("hold", hold);
  params.set("decay", decay);
  params.set("sustain", sustain);
  params.set("release", release);
  env->loadParameters(params);
  return env;
}

unique_ptr<PadSynth> makePadSynth(const string & preset) {
  auto pad = make_unique<PadSynth>();
  MemoryParameterSource params;
  params.set("preset", preset);
  pad->loadParameters(params);
  return pad;
}

// <envelope>+<padsynth> - the shape every GM synth-pad override and the
// mellotron are built from (docs/effects.md's own canonical nesting
// pattern), assembled directly in C++ via MemoryParameterSource rather
// than parsed from an XML string, the same round-trip-without-XML
// technique SongState::initialize() already uses for bus-slot effects.
unique_ptr<Track> makeEnvelopePad(const string & padsynth_preset, float attack, float hold, float decay, float sustain, float release) {
  auto env = makeEnvelope(attack, hold, decay, sustain, release);
  env->addChild(makePadSynth(padsynth_preset));
  return env;
}

// Registers `path` only when nothing (no SoundFont, no earlier library
// registration) has already claimed that exact taxonomy leaf - the
// "fallback when the SoundFont has no piano" half of the additive piano's
// job. Deliberately an exact-key check (getTaxonomyPaths(), not
// resolvePath()'s walk-up): resolvePath("piano.acoustic.grand") could
// still find something via a shorter prefix or a kGmPathDefaults redirect
// even with no exact SF2 registration at this leaf, and that's a real,
// intentional fallback of its own - this function only cares whether this
// exact leaf is unclaimed.
void registerFallbackPath(InstrumentProvider & provider, const string & path, shared_ptr<Track> instrument) {
  if (provider.getTaxonomyPaths().count(path)) return;
  provider.registerPath(path, move(instrument));
}

}

void registerLibraryInstruments(InstrumentProvider & provider) {
  // GM synth pads (programs 89-96) - each <envelope>+<padsynth>, overriding
  // whatever loadSoundFont() registered at the same pad.* path through the
  // ordinary registerPath() last-write-wins rule, not by special-casing
  // GenericInstrument's resolution. ADSR/preset choices below aim for each
  // pad's own GM character; see this function's own end for the one pad
  // (Sweep) an oscillator-based instrument can't fully deliver.
  provider.registerPath("pad.newAge", makeEnvelopePad("glass", 0.8f, 0.0f, 0.3f, 0.9f, 1.2f));
  provider.registerPath("pad.warm", makeEnvelopePad("warm", 0.6f, 0.0f, 0.4f, 0.85f, 1.0f));
  provider.registerPath("pad.poly", makeEnvelopePad("warm", 0.2f, 0.0f, 0.3f, 0.8f, 0.6f));
  provider.registerPath("pad.choir", makeEnvelopePad("formant-vocal", 0.5f, 0.0f, 0.3f, 0.9f, 0.8f));
  provider.registerPath("pad.bowed", makeEnvelopePad("bowed-ensemble", 0.4f, 0.0f, 0.3f, 0.9f, 0.7f));
  provider.registerPath("pad.metallic", makeEnvelopePad("glass", 0.3f, 0.0f, 0.5f, 0.7f, 1.0f));
  provider.registerPath("pad.halo", makeEnvelopePad("formant-vocal", 1.0f, 0.0f, 0.4f, 0.9f, 1.5f));

  // Sweep (pad.sweep) is a slow filter sweep over the note's own life -
  // motion no static oscillator (PADsynth included) can produce; there is
  // no LFO-modulated per-voice filter in this codebase to drive one
  // dynamically (see this function's own doc comment / the task's final
  // report for this open item). Approximated instead with a static
  // <biquadFilter> darkening a warm pad - a real, audible difference from
  // the other pads, just not the sweeping motion GM's own Sweep implies.
  {
    auto filter = make_unique<BiquadFilter>();
    MemoryParameterSource filter_params;
    filter_params.set("type", string("lowpass"));
    filter_params.set("fc", 0.15f);
    filter_params.set("Q", 0.7071f); // Butterworth Q - BiquadFilter has no default, 0 would divide by zero
    filter->loadParameters(filter_params);
    filter->addChild(makeEnvelopePad("warm", 0.9f, 0.0f, 0.4f, 0.85f, 1.2f));
    provider.registerPath("pad.sweep", move(filter));
  }

  // Additive piano - <envelope>+<additive preset="struck-string">. Sustain
  // 0 (a piano's own sound is entirely percussive/decaying, never a held
  // plateau) with a long decay stage (8s, well past where the additive
  // engine's own per-partial decay has already gone inaudible - see
  // SinusoidBank's -90dB culling) so the envelope itself never audibly
  // truncates the tail, and a short release (0.3s) so lifting the key ends
  // the note promptly rather than ringing on indefinitely.
  auto additive_piano_envelope = []() {
    auto env = makeEnvelope(0.005f, 0.0f, 8.0f, 0.0f, 0.3f);
    auto additive = make_unique<Additive>();
    MemoryParameterSource additive_params;
    additive_params.set("preset", string("struck-string"));
    additive->loadParameters(additive_params);
    env->addChild(move(additive));
    return env;
  };

  // Always registered at its own dedicated leaf so it can be forced for
  // comparison regardless of whether a SoundFont piano exists.
  provider.registerPath("piano.additive", additive_piano_envelope());
  // Only takes over piano.acoustic.grand itself when nothing already
  // claimed that exact leaf - the "fallback when the SoundFont has no
  // piano" role; a real SF2 grand piano always wins when one is loaded.
  registerFallbackPath(provider, "piano.acoustic.grand", additive_piano_envelope());

  // Mellotron - tapeDegradation(preset="mellotron") wrapping
  // envelope+padsynth(preset="mellotron"), the exact tape+envelope+
  // oscillator nesting docs/tape_degradation.md already documents for a
  // voice-attached tape machine. Not a General MIDI instrument - GM has no
  // mellotron program - so it's registered under the existing `keyboard`
  // taxonomy root instead, following the same root.technology.name shape
  // GM's own electric-keyboard entries use (piano.electric.tine,
  // keyboard.electric.clavinet): keyboard.tape.mellotron. OutlineView's
  // Library section lists every taxonomy path generically (it doesn't
  // distinguish GM-backed from hand-registered entries), so this shows up
  // there automatically, sorted alongside the other keyboard.* entries.
  {
    auto tape = make_unique<TapeDegradation>();
    MemoryParameterSource tape_params;
    tape_params.set("preset", string("mellotron"));
    tape->loadParameters(tape_params);
    tape->addChild(makeEnvelopePad("mellotron", 0.05f, 0.0f, 0.3f, 0.95f, 0.4f));
    provider.registerPath("keyboard.tape.mellotron", move(tape));
  }
}
