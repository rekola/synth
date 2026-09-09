#ifndef _GMINSTRUMENTDESCRIPTIONS_H_
#define _GMINSTRUMENTDESCRIPTIONS_H_

#include <string>

// One user-facing, one-sentence description per General MIDI taxonomy
// path (see GmInstrumentTable.h) - what OutlineView.cpp's Details panel
// shows for a Library > Instruments row, the same role
// GroovePatternTemplate::description plays for a groove.
//
// Kept separate from GmInstrumentTable.h/docs/instrument-paths.md on
// purpose: that pair is the canonical, resolution-critical program-
// number -> path mapping (mechanically regenerated together, and must
// stay in sync with each other) - this is plain UI copy with no bearing
// on what actually resolves to what, so folding it into that pipeline
// would create the wrong kind of "keep these in sync" coupling. Every
// entry below describes a real, standard General MIDI program (the most
// widely documented instrument set in digital music - not anything
// invented for this project), in the same order kGmBank0Table/
// kGmBank128Table list them, for easy side-by-side checking against that
// table.
struct GmInstrumentDescription { const char * path; const char * description; };

static constexpr GmInstrumentDescription kGmInstrumentDescriptions[] = {
  // Bank 0
  {"piano.acoustic.grand", "A bright, resonant acoustic grand piano."},
  {"piano.acoustic.grand.bright", "A brighter, more cutting take on the grand piano."},
  {"piano.electric.grand", "An electric grand piano, smoother than the acoustic kind."},
  {"piano.acoustic.upright.honkyTonk", "A slightly detuned upright piano, for a honky-tonk saloon sound."},
  {"piano.electric.tine", "A classic tine-based electric piano, warm and bell-like."},
  {"piano.electric.fm", "An FM-synthesis electric piano, glassier and more digital-sounding."},
  {"keyboard.plucked.harpsichord", "A plucked-string harpsichord, bright and percussive."},
  {"keyboard.electric.clavinet", "A funky, percussive electric clavinet."},
  {"percussion.pitched.metal.celesta", "A celesta - soft, bell-like, and delicate."},
  {"percussion.pitched.metal.glockenspiel", "A bright, chiming glockenspiel."},
  {"percussion.pitched.metal.musicBox", "A tinkling, wind-up music box."},
  {"percussion.pitched.metal.vibraphone", "A vibraphone, with a gentle metallic shimmer."},
  {"percussion.pitched.wood.marimba", "A warm, woody marimba."},
  {"percussion.pitched.wood.xylophone", "A bright, hard-mallet xylophone."},
  {"percussion.pitched.metal.tubularBells", "Tubular bells - deep, ringing chimes."},
  {"string.struck.dulcimer", "A hammered dulcimer, bright and cascading."},
  {"organ.tonewheel", "A classic tonewheel organ."},
  {"organ.tonewheel.percussive", "A tonewheel organ with a percussive attack on each note."},
  {"organ.tonewheel.overdriven", "A driven, gritty tonewheel organ."},
  {"organ.pipe", "A church-style pipe organ."},
  {"organ.reed", "A reed organ, breathy and slightly nasal."},
  {"reed.free.accordion", "An accordion."},
  {"reed.free.harmonica", "A harmonica."},
  {"reed.free.accordion.bandoneon", "A bandoneon, the reedy accordion cousin heard in tango."},
  {"guitar.acoustic.nylon", "A nylon-string classical guitar, warm and mellow."},
  {"guitar.acoustic.steel", "A steel-string acoustic guitar, brighter and twangier."},
  {"guitar.electric.hollow", "A hollow-body electric guitar, warm and jazzy."},
  {"guitar.electric.clean", "A clean electric guitar."},
  {"guitar.electric.muted", "A palm-muted electric guitar, tight and percussive."},
  {"guitar.electric.overdriven", "An overdriven electric guitar, edgy and pushed."},
  {"guitar.electric.distorted", "A heavily distorted electric guitar."},
  {"guitar.electric.harmonics", "Electric guitar harmonics - chiming, bell-like overtones."},
  {"bass.acoustic.upright", "An upright acoustic double bass."},
  {"bass.electric.finger", "A fingered electric bass."},
  {"bass.electric.pick", "A pick-played electric bass, sharper attack than fingered."},
  {"bass.electric.fretless", "A fretless electric bass, smooth and slidey."},
  {"bass.electric.slap", "A slapped electric bass, punchy and percussive."},
  {"bass.electric.slap.pop", "A slap-and-pop electric bass, with a snappy pop-string accent."},
  {"bass.synth", "A synth bass."},
  {"bass.synth.resonant", "A resonant synth bass, with a filter-swept edge."},
  {"string.bowed.violin", "A solo violin."},
  {"string.bowed.viola", "A solo viola, darker and mellower than the violin."},
  {"string.bowed.cello", "A solo cello."},
  {"string.bowed.contrabass", "A bowed double bass."},
  {"string.bowed.ensemble.tremolo", "A string ensemble playing tremolo - shimmering and sustained."},
  {"string.bowed.ensemble.pizzicato", "A plucked (pizzicato) string ensemble."},
  {"string.plucked.harp", "A concert harp."},
  {"percussion.pitched.drum.timpani", "Timpani - deep, tuned orchestral drums."},
  {"string.bowed.ensemble", "A full bowed string ensemble."},
  {"string.bowed.ensemble.slow", "A slow-attack string ensemble, swelling gently in."},
  {"string.synth", "A synthesized string ensemble."},
  {"string.synth.slow", "A slow-attack synth string pad."},
  {"voice.choir.ah", "A choir singing \"ah\"."},
  {"voice.choir.ooh", "A choir singing \"ooh\", softer and more closed."},
  {"voice.synth", "A synthesized voice."},
  {"sfx.orchestraHit", "A stabbing orchestral hit - the classic dramatic accent."},
  {"brass.trumpet", "A trumpet."},
  {"brass.trombone", "A trombone."},
  {"brass.tuba", "A tuba, deep and rounded."},
  {"brass.trumpet.muted", "A muted trumpet, softer and more nasal."},
  {"brass.horn", "A French horn, warm and mellow."},
  {"brass.section", "A full brass section."},
  {"brass.synth", "A synthesized brass section."},
  {"brass.synth.soft", "A softer, mellower synth brass."},
  {"reed.single.sax.soprano", "A soprano saxophone."},
  {"reed.single.sax.alto", "An alto saxophone."},
  {"reed.single.sax.tenor", "A tenor saxophone."},
  {"reed.single.sax.baritone", "A baritone saxophone, deep and husky."},
  {"reed.double.oboe", "An oboe, reedy and piercing."},
  {"reed.double.corAnglais", "A cor anglais, darker and mellower than the oboe."},
  {"reed.double.bassoon", "A bassoon, deep and woody."},
  {"reed.single.clarinet", "A clarinet."},
  {"flute.piccolo", "A piccolo, bright and very high."},
  {"flute.concert", "A concert flute."},
  {"flute.recorder", "A recorder."},
  {"flute.pan", "Pan flute/pipes."},
  {"flute.blownBottle", "A blown-bottle sound - hollow and breathy."},
  {"flute.shakuhachi", "A shakuhachi, the breathy Japanese bamboo flute."},
  {"flute.whistle", "A tin whistle."},
  {"flute.ocarina", "An ocarina."},
  {"lead.square", "A square-wave synth lead."},
  {"lead.saw", "A sawtooth synth lead."},
  {"lead.calliope", "A calliope-style synth lead, like a carnival organ."},
  {"lead.chiff", "A breathy, chiffy synth lead."},
  {"lead.charang", "A bright, slightly distorted synth lead."},
  {"lead.voice", "A vocal-formant synth lead."},
  {"lead.fifths", "A synth lead voiced in parallel fifths."},
  {"lead.bassLead", "A synth lead with a fat bass register underneath."},
  {"pad.newAge", "A soft, airy new-age pad."},
  {"pad.warm", "A warm, rounded synth pad."},
  {"pad.poly", "A polyphonic synth pad, reminiscent of a vintage poly-synth."},
  {"pad.choir", "A choir-like synth pad."},
  {"pad.bowed", "A slow, bowed-string-like synth pad."},
  {"pad.metallic", "A metallic, bell-like synth pad."},
  {"pad.halo", "A shimmering, ethereal synth pad."},
  {"pad.sweep", "A synth pad with a slow filter sweep."},
  {"texture.rain", "A shimmering \"rain\" synth texture."},
  {"texture.soundtrack", "A cinematic, soundtrack-style synth texture."},
  {"texture.crystal", "A bright, glassy synth texture."},
  {"texture.atmosphere", "A slow, evolving atmospheric synth texture."},
  {"texture.brightness", "A bright, glassy synth texture with a harder edge."},
  {"texture.goblins", "A dark, wobbling synth texture."},
  {"texture.echoes", "An echoing, rhythmic synth texture."},
  {"texture.sciFi", "A sweeping, sci-fi synth texture."},
  {"string.plucked.sitar", "A sitar, with its characteristic buzzing drone."},
  {"string.plucked.banjo", "A banjo."},
  {"string.plucked.shamisen", "A shamisen, the plucked Japanese three-string lute."},
  {"string.plucked.koto", "A koto, the plucked Japanese zither."},
  {"percussion.pitched.metal.kalimba", "A kalimba (thumb piano)."},
  {"reed.double.bagpipe", "Bagpipes."},
  {"string.bowed.violin.fiddle", "A fiddle - a violin played in a folk style."},
  {"reed.double.shehnai", "A shehnai, the reedy Indian double-reed horn."},
  {"percussion.pitched.metal.tinkleBell", "Small, delicate tinkling bells."},
  {"percussion.pitched.metal.agogo", "Agogo bells - two high, clear metallic tones."},
  {"percussion.pitched.metal.steelDrum", "A steel drum (steelpan)."},
  {"percussion.unpitched.wood.woodblock", "A wood block."},
  {"percussion.unpitched.drum.taiko", "A taiko drum, deep and booming."},
  {"percussion.pitched.drum.tom", "Melodic toms."},
  {"percussion.synth.drum", "A synthesized drum hit."},
  {"percussion.unpitched.metal.cymbal.reverse", "A reverse cymbal swell."},
  {"sfx.fretNoise", "Guitar fret/string noise."},
  {"sfx.breath", "Breath noise."},
  {"sfx.seashore", "Ocean/seashore ambience."},
  {"sfx.bird", "A bird tweet."},
  {"sfx.telephone", "A telephone ring."},
  {"sfx.helicopter", "A helicopter."},
  {"sfx.applause", "Applause."},
  {"sfx.gunshot", "A gunshot."},

  // Bank 128 - percussion kits
  {"kit.standard", "The standard General MIDI drum kit."},
  {"kit.room", "A roomier, more ambient-sounding drum kit."},
  {"kit.power", "A bigger, punchier rock drum kit."},
  {"kit.electronic", "An electronic drum kit."},
  {"kit.electronic.tr808", "A classic TR-808-style electronic drum kit."},
  {"kit.jazz", "A brushed, jazz-style drum kit."},
  {"kit.brush", "A soft, brushed drum kit."},
  {"kit.orchestra", "Orchestral percussion - concert bass drum, timpani, and the like."},
  {"kit.sfx", "A kit of sound effects rather than drums."},
};

// Plain linear scan - the table above is small (~140 entries) and only
// ever consulted once per Details-panel redraw, the same tradeoff
// GroovePatternLibrary.cpp's own findGroovePattern() makes. nullptr, not
// "", for "no description authored for this path" - matches
// InstrumentProvider::tryGetByLiteralName()'s own nullptr-on-miss
// convention.
inline const char * findGmInstrumentDescription(const std::string & path) {
  for (auto & entry : kGmInstrumentDescriptions) {
    if (path == entry.path) return entry.description;
  }
  return nullptr;
}

#endif
