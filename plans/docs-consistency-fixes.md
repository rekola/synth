# Documentation fixes (not music theory)

Errors and inconsistencies found in a review of README.md, docs/ and a few code
comments. The music-theory and historical findings from the same review
are being discussed separately and are not in this plan.

## README.md

1. **Launchpad models.** "Launchpad support" says Mini MK3 / X; the code
   also drives the Pro MK3 (`LaunchpadProtocol::buildProgrammerModeEnter()`,
   CLAUDE.md, launchpad.md's Pro MK3 button numbers). List all three, or say
   why the Pro MK3 is left out. docs/launchpad.md's first line says the same
   thing.
2. **Roadmap lists "Just tuning" as missing.** Chord-based just intonation is
   built (`apply-just-intonation-region`, terminal.md "Just intonation"); what
   is missing is adaptive just intonation (glossary). Rename the item.
3. **The scale cannot be chosen anywhere but the song file.** "Scales" says a
   song has a key and an optional scale but not how to set it; no command or
   menu sets it (only `<song scale="...">` does). Say so, or add the command.
4. **Stretch figure.** "Pianos and Just Intervals" says the additive piano's
   stretch is "bounded at the same ε" as the FM pianos (0.001); the `piano`
   preset uses `stretch` 0.003 (additive.md, Presets and Tuning the presets).
   Give the actual figure, or say "a small ε" without tying it to 0.001.
5. **Heading levels.** `## Features`, then `# Background`, `# Scales and
   Tunings`, `## Pianos and Just Intervals`, `# Terminal support`, ... mix H1
   and H2 for sections of the same rank. Make every top-level section H2 under
   the one H1 title.
6. **Wording.**
   - "combines live sequencer with a traditional tracker" -> "combines a live
     sequencer with a traditional tracker".
   - "The Application" (Background) -> "synth" or "this program"; the capital
     reads as a defined term that is never defined.
   - "Works out of the box (Basic instruments are included and no low-latency
     requirements)" -> "Works out of the box: basic instruments are built in
     and no low-latency audio setup is needed".
   - "Ambisonic Bus" means nothing to a reader who has not read CLAUDE.md;
     "3D (ambisonic) spatial audio, heard binaurally on headphones".
   - "SoundFont2" -> "SoundFont 2 (General MIDI) instruments".
   - "Microtonal (12/19/31/53-EDO)": spell out EDO (equal divisions of the
     octave) at first use; it is only expanded further down.

## docs/drums-and-sequencer.md

7. **Pad colours described two ways.** "Playing" says pitched pads are
   "colored by their distance from the tonic"; "Pad colors" says the
   distance-based scheme was replaced by consonance. README and CLAUDE.md say
   consonance. Fix "Playing".

## docs/additive.md and glossary

8. **"(the brief)"** in Presets cites a document the reader does not have.
   State the source or drop the parenthesis.
9. **Unison detune.** additive.md says 1 cent between unison strings is a
   sourced value; the `piano` preset uses 2.5 (Tuning the presets explains
   why), and the glossary's Unison says "a cent or so". Make the three agree
   on what is sourced and what the preset does.

## Note-number tables (docs/*note_numbers.txt)

10. **Stale and duplicate files.** `19tet_note_numbers.txt` duplicates
    `19edo_note_numbers.txt` with a different numbering base and notation;
    `41edo_note_numbers.txt` is for a tuning the program does not support and
    ends in an "Old:" draft section. Keep one 19-EDO table and drop or move
    the 41-EDO draft.
11. **Inconsistent format.** `19edo_note_numbers.txt` uses ASCII `#`/`b` and
    no octave numbers; the 31 and 53 tables use Unicode accidentals and MIDI-
    style octave numbers. Ratios are written `a:b` in some rows and `a/b` in
    others (31-EDO table), and `1+10/11`-style entries appear. One format for
    all tables.

## Code comments

12. **`src/model/Scale.h`**: "UTONAL's own E𝄫/A♭ spelling" - A♭ is an ordinary
    note in every tuning; the degree meant is B𝄫 ("E𝄫/B𝄫").
