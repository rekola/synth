# GM instrument path taxonomy

Canonical path for every General MIDI program and percussion kit. This is the registry the
instrument resolver walks up: a request for `piano.acoustic.grand.yamahaCfx` finds no
provider at the leaf, walks up, and binds to the GM program registered at
`piano.acoustic.grand`.

This table is a set of *registrations*, not the set of legal path names. Other providers —
a user-supplied SoundFont, a future software instrument — register their own paths at
runtime, including leaves that appear nowhere here.

## Principles

1. **Roots are what a musician asks for, not what an organologist would say.** A piano and
   a hammered dulcimer are both struck chordophones, but nobody types `chordophone.` to get
   a piano. `piano`, `guitar` and `bass` are roots despite being string instruments;
   everything else stringed lives under `string`.
2. **Taxonomy over GM's own grouping.** GM's 16 families of 8 are a numbering convenience,
   not a classification — shakuhachi sits in "Pipe" and bagpipe in "Ethnic" although both
   are wind instruments of quite different kinds. Programs register where they belong, not
   where GM filed them. Nothing depends on GM's families surviving.
3. **A variant is a child of what it varies.** `piano.acoustic.grand.bright` degrades to
   `piano.acoustic.grand`, which is the desired behaviour: asking for the bright one on a
   font that lacks it gets you a grand piano, and asking for a grand never accidentally
   gets you the bright one.
4. **GM's numbered pairs get a descriptive leaf.** Where GM offers "X 1" and "X 2" with no
   stated difference, the lower program takes the base path and the higher takes a leaf
   naming its conventional character. Never `.1` and `.2` — those encode GM's numbering
   into paths that are supposed to outlive it.
5. **Paths are lowerCamelCase segments,** matching the SF2 generator names already used in
   the song format.

## Roots

`piano`, `keyboard`, `organ`, `guitar`, `bass`, `string`, `brass`, `reed`, `flute`,
`voice`, `lead`, `pad`, `texture`, `percussion`, `kit`, `sfx`.

`texture` holds GM's synth-effects pads (atmosphere, crystal); `sfx` holds the non-musical
sound effects (helicopter, applause). Keeping them apart matters because they are asked for
in completely different situations.

## Bank 0 — melodic programs

| Prog | GM# | GM name | Path |
|---|---|---|---|
| 0 | 1 | Acoustic Grand Piano | `piano.acoustic.grand` |
| 1 | 2 | Bright Acoustic Piano | `piano.acoustic.grand.bright` |
| 2 | 3 | Electric Grand Piano | `piano.electric.grand` |
| 3 | 4 | Honky-tonk Piano | `piano.acoustic.upright.honkyTonk` |
| 4 | 5 | Electric Piano 1 | `piano.electric.tine` |
| 5 | 6 | Electric Piano 2 | `piano.electric.fm` |
| 6 | 7 | Harpsichord | `keyboard.plucked.harpsichord` |
| 7 | 8 | Clavinet | `keyboard.electric.clavinet` |
| 8 | 9 | Celesta | `percussion.pitched.metal.celesta` |
| 9 | 10 | Glockenspiel | `percussion.pitched.metal.glockenspiel` |
| 10 | 11 | Music Box | `percussion.pitched.metal.musicBox` |
| 11 | 12 | Vibraphone | `percussion.pitched.metal.vibraphone` |
| 12 | 13 | Marimba | `percussion.pitched.wood.marimba` |
| 13 | 14 | Xylophone | `percussion.pitched.wood.xylophone` |
| 14 | 15 | Tubular Bells | `percussion.pitched.metal.tubularBells` |
| 15 | 16 | Dulcimer | `string.struck.dulcimer` |
| 16 | 17 | Drawbar Organ | `organ.tonewheel` |
| 17 | 18 | Percussive Organ | `organ.tonewheel.percussive` |
| 18 | 19 | Rock Organ | `organ.tonewheel.overdriven` |
| 19 | 20 | Church Organ | `organ.pipe` |
| 20 | 21 | Reed Organ | `organ.reed` |
| 21 | 22 | Accordion | `reed.free.accordion` |
| 22 | 23 | Harmonica | `reed.free.harmonica` |
| 23 | 24 | Tango Accordion | `reed.free.accordion.bandoneon` |
| 24 | 25 | Acoustic Guitar (nylon) | `guitar.acoustic.nylon` |
| 25 | 26 | Acoustic Guitar (steel) | `guitar.acoustic.steel` |
| 26 | 27 | Electric Guitar (jazz) | `guitar.electric.hollow` |
| 27 | 28 | Electric Guitar (clean) | `guitar.electric.clean` |
| 28 | 29 | Electric Guitar (muted) | `guitar.electric.muted` |
| 29 | 30 | Overdriven Guitar | `guitar.electric.overdriven` |
| 30 | 31 | Distortion Guitar | `guitar.electric.distorted` |
| 31 | 32 | Guitar Harmonics | `guitar.electric.harmonics` |
| 32 | 33 | Acoustic Bass | `bass.acoustic.upright` |
| 33 | 34 | Electric Bass (finger) | `bass.electric.finger` |
| 34 | 35 | Electric Bass (pick) | `bass.electric.pick` |
| 35 | 36 | Fretless Bass | `bass.electric.fretless` |
| 36 | 37 | Slap Bass 1 | `bass.electric.slap` |
| 37 | 38 | Slap Bass 2 | `bass.electric.slap.pop` |
| 38 | 39 | Synth Bass 1 | `bass.synth` |
| 39 | 40 | Synth Bass 2 | `bass.synth.resonant` |
| 40 | 41 | Violin | `string.bowed.violin` |
| 41 | 42 | Viola | `string.bowed.viola` |
| 42 | 43 | Cello | `string.bowed.cello` |
| 43 | 44 | Contrabass | `string.bowed.contrabass` |
| 44 | 45 | Tremolo Strings | `string.bowed.ensemble.tremolo` |
| 45 | 46 | Pizzicato Strings | `string.bowed.ensemble.pizzicato` |
| 46 | 47 | Orchestral Harp | `string.plucked.harp` |
| 47 | 48 | Timpani | `percussion.pitched.drum.timpani` |
| 48 | 49 | String Ensemble 1 | `string.bowed.ensemble` |
| 49 | 50 | String Ensemble 2 | `string.bowed.ensemble.slow` |
| 50 | 51 | Synth Strings 1 | `string.synth` |
| 51 | 52 | Synth Strings 2 | `string.synth.slow` |
| 52 | 53 | Choir Aahs | `voice.choir.ah` |
| 53 | 54 | Voice Oohs | `voice.choir.ooh` |
| 54 | 55 | Synth Voice | `voice.synth` |
| 55 | 56 | Orchestra Hit | `sfx.orchestraHit` |
| 56 | 57 | Trumpet | `brass.trumpet` |
| 57 | 58 | Trombone | `brass.trombone` |
| 58 | 59 | Tuba | `brass.tuba` |
| 59 | 60 | Muted Trumpet | `brass.trumpet.muted` |
| 60 | 61 | French Horn | `brass.horn` |
| 61 | 62 | Brass Section | `brass.section` |
| 62 | 63 | Synth Brass 1 | `brass.synth` |
| 63 | 64 | Synth Brass 2 | `brass.synth.soft` |
| 64 | 65 | Soprano Sax | `reed.single.sax.soprano` |
| 65 | 66 | Alto Sax | `reed.single.sax.alto` |
| 66 | 67 | Tenor Sax | `reed.single.sax.tenor` |
| 67 | 68 | Baritone Sax | `reed.single.sax.baritone` |
| 68 | 69 | Oboe | `reed.double.oboe` |
| 69 | 70 | English Horn | `reed.double.corAnglais` |
| 70 | 71 | Bassoon | `reed.double.bassoon` |
| 71 | 72 | Clarinet | `reed.single.clarinet` |
| 72 | 73 | Piccolo | `flute.piccolo` |
| 73 | 74 | Flute | `flute.concert` |
| 74 | 75 | Recorder | `flute.recorder` |
| 75 | 76 | Pan Flute | `flute.pan` |
| 76 | 77 | Blown Bottle | `flute.blownBottle` |
| 77 | 78 | Shakuhachi | `flute.shakuhachi` |
| 78 | 79 | Whistle | `flute.whistle` |
| 79 | 80 | Ocarina | `flute.ocarina` |
| 80 | 81 | Lead 1 (square) | `lead.square` |
| 81 | 82 | Lead 2 (sawtooth) | `lead.saw` |
| 82 | 83 | Lead 3 (calliope) | `lead.calliope` |
| 83 | 84 | Lead 4 (chiff) | `lead.chiff` |
| 84 | 85 | Lead 5 (charang) | `lead.charang` |
| 85 | 86 | Lead 6 (voice) | `lead.voice` |
| 86 | 87 | Lead 7 (fifths) | `lead.fifths` |
| 87 | 88 | Lead 8 (bass + lead) | `lead.bassLead` |
| 88 | 89 | Pad 1 (new age) | `pad.newAge` |
| 89 | 90 | Pad 2 (warm) | `pad.warm` |
| 90 | 91 | Pad 3 (polysynth) | `pad.poly` |
| 91 | 92 | Pad 4 (choir) | `pad.choir` |
| 92 | 93 | Pad 5 (bowed) | `pad.bowed` |
| 93 | 94 | Pad 6 (metallic) | `pad.metallic` |
| 94 | 95 | Pad 7 (halo) | `pad.halo` |
| 95 | 96 | Pad 8 (sweep) | `pad.sweep` |
| 96 | 97 | FX 1 (rain) | `texture.rain` |
| 97 | 98 | FX 2 (soundtrack) | `texture.soundtrack` |
| 98 | 99 | FX 3 (crystal) | `texture.crystal` |
| 99 | 100 | FX 4 (atmosphere) | `texture.atmosphere` |
| 100 | 101 | FX 5 (brightness) | `texture.brightness` |
| 101 | 102 | FX 6 (goblins) | `texture.goblins` |
| 102 | 103 | FX 7 (echoes) | `texture.echoes` |
| 103 | 104 | FX 8 (sci-fi) | `texture.sciFi` |
| 104 | 105 | Sitar | `string.plucked.sitar` |
| 105 | 106 | Banjo | `string.plucked.banjo` |
| 106 | 107 | Shamisen | `string.plucked.shamisen` |
| 107 | 108 | Koto | `string.plucked.koto` |
| 108 | 109 | Kalimba | `percussion.pitched.metal.kalimba` |
| 109 | 110 | Bagpipe | `reed.double.bagpipe` |
| 110 | 111 | Fiddle | `string.bowed.violin.fiddle` |
| 111 | 112 | Shanai | `reed.double.shehnai` |
| 112 | 113 | Tinkle Bell | `percussion.pitched.metal.tinkleBell` |
| 113 | 114 | Agogo | `percussion.pitched.metal.agogo` |
| 114 | 115 | Steel Drums | `percussion.pitched.metal.steelDrum` |
| 115 | 116 | Woodblock | `percussion.unpitched.wood.woodblock` |
| 116 | 117 | Taiko Drum | `percussion.unpitched.drum.taiko` |
| 117 | 118 | Melodic Tom | `percussion.pitched.drum.tom` |
| 118 | 119 | Synth Drum | `percussion.synth.drum` |
| 119 | 120 | Reverse Cymbal | `percussion.unpitched.metal.cymbal.reverse` |
| 120 | 121 | Guitar Fret Noise | `sfx.fretNoise` |
| 121 | 122 | Breath Noise | `sfx.breath` |
| 122 | 123 | Seashore | `sfx.seashore` |
| 123 | 124 | Bird Tweet | `sfx.bird` |
| 124 | 125 | Telephone Ring | `sfx.telephone` |
| 125 | 126 | Helicopter | `sfx.helicopter` |
| 126 | 127 | Applause | `sfx.applause` |
| 127 | 128 | Gunshot | `sfx.gunshot` |

## Bank 128 — percussion kits

A kit is one instrument carrying a whole keymapped GM percussion set, which matches the
existing design where the entire kit lives on one track. Kits are reached through the
`<instrumentMap>` element, never `<instrument>`, so walk-up cannot cross between `kit.` and
the pitched tree.

Kit entries are keyed by synth's percussion symbols (`BD`, `SD`, …), which correspond 1:1
to note numbers 35–81. These abbreviations are synth's own vocabulary, not a General MIDI
standard — what GM standardizes is which sound sits at which note number, and its full
names for them. The symbols are what appears in a song file; note numbers do not.

Only program 0 is GM Level 1. The rest are GS/GM2-convention kits; absence in a given font is
normal and walk-up handles it.

| Bank:Prog | Kit | Path |
|---|---|---|
| 128:0 | Standard | `kit.standard` |
| 128:8 | Room | `kit.room` |
| 128:16 | Power | `kit.power` |
| 128:24 | Electronic | `kit.electronic` |
| 128:25 | TR-808 | `kit.electronic.tr808` |
| 128:32 | Jazz | `kit.jazz` |
| 128:40 | Brush | `kit.brush` |
| 128:48 | Orchestra | `kit.orchestra` |
| 128:56 | SFX | `kit.sfx` |

Bank-128 contents vary between fonts far more than bank 0 does - expect a typical GM font to
provide most of the rows above (`kit.standard` in particular, since program 0 is GM Level 1
and effectively guaranteed) but not all of them, and a minimal/reduced font to provide only a
handful. `kit.sfx` in particular is real GS/GM2 numbering that isn't reliably implemented -
verified missing from every General MIDI font checked so far. None of this is a taxonomy
problem: a path with nothing registered behind it in the loaded font is the normal case this
whole registry is designed around, resolved by walking up to whatever narrower sibling *is*
registered, or failing cleanly if none is - the same handling as any other under-provided
path, not special-cased per kit.

## Defaults for general requests

Walk-up resolves a request more specific than anything registered. The opposite case — a
request for a bare root like `piano` — needs a designated preferred child per node.
Proposed defaults:

| Request | Resolves to |
|---|---|
| `piano` | `piano.acoustic.grand` |
| `piano.acoustic` | `piano.acoustic.grand` |
| `piano.electric` | `piano.electric.tine` |
| `keyboard` | `keyboard.plucked.harpsichord` |
| `organ` | `organ.tonewheel` |
| `guitar` | `guitar.acoustic.steel` |
| `guitar.electric` | `guitar.electric.clean` |
| `bass` | `bass.electric.finger` |
| `string` | `string.bowed.ensemble` |
| `string.bowed` | `string.bowed.violin` |
| `string.plucked` | `string.plucked.harp` |
| `brass` | `brass.trumpet` |
| `reed` | `reed.single.clarinet` |
| `reed.single.sax` | `reed.single.sax.alto` |
| `flute` | `flute.concert` |
| `voice` | `voice.choir.ah` |
| `lead` | `lead.saw` |
| `pad` | `pad.warm` |
| `texture` | `texture.atmosphere` |
| `percussion` | `percussion.pitched.metal.vibraphone` |
| `kit` | `kit.standard` |

`sfx` deliberately has no default — there is no sensible generic sound effect, and an
unresolvable request there should fail loudly.

## Known-arbitrary placements

Recorded because they were close calls, so anyone revisiting them can see what was weighed
rather than re-deriving it:

- **Orchestra Hit (55)** at `sfx.orchestraHit`. It is used musically, as a rhythmic stab,
  but it is not an instrument and does not belong in any instrument family. `percussion.`
  is the other defensible home.
- **Clavinet (7)** at `keyboard.electric.clavinet`. The Hohner mechanism is a rubber tip
  striking the string against an anvil, which is arguably plucked and arguably struck; it
  is grouped with harpsichord under `keyboard` because that is how it is reached for, not
  because the actions match.
- **Dulcimer (15)** at `string.struck.dulcimer` rather than under `piano`, despite sharing
  the struck-chordophone action, per principle 1.
- **Reed Organ (20)** stays under `organ` while accordion and harmonica go to `reed.free`,
  splitting three free-reed instruments across two roots. Justified by how they are asked
  for; noted because it is inconsistent on the physics.

## One caveat specific to microtonal use

**Lead 7 (fifths), program 86**, is realized in SoundFonts as two layered samples a fifth
apart. That interval is baked into the sample layering as a 12-EDO fifth of 700 cents and
transposes rigidly with the note. In 31-EDO the fifth is 18 steps at roughly 696.8 cents,
so this patch will sit about 3 cents wide of the tuning at every pitch and beat against
anything else sounding. The same hazard applies to any preset whose character comes from a
fixed interval between layers — worth a check on `pad.choir`, `brass.section` and the
detuned-pair leads in whichever font is loaded. Nothing to fix in the taxonomy; it belongs
in documentation for the instrument, and is an argument for the eventual software
instruments over samples for this repertoire.
