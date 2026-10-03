# Glossary

Terms with a specific meaning in this project. Entries marked *(planned)*
describe the target naming and behavior from `plans/swing.md`, not what the
code does today.

**Groove**
The general word for rhythmic feel: how a pattern's timing (and often
accents) deviates from a rigid grid. Swing is the simplest groove. Other
DAWs go further. Ableton's *Groove Pool* holds templates of per-position
timing and velocity that you apply to clips. Renoise's *Groove* panel is
closer to this project: four per-line amounts that repeat every four lines
and apply song-wide at playback, without changing the pattern data. Here,
"groove" means timing
feel only, and for now that is just swing. *(planned: today the Library
also calls its drum patterns "Grooves"; see Rhythm.)*

**Rhythm** *(planned name)*
A pre-written drum pattern in the Library, such as Waltz, Funk or Bossa
Nova. It is content (which drum hits fall on which rows), where swing is
feel (when those rows sound). The same rhythm can be straight or swung.
Currently named "Groove" in the code and the Library; renamed to avoid
confusion with the DAW sense of groove above.

**Row**
This engine's grid step: one sixteenth note, so 4 rows per beat and
`rowsPerBar` rows per bar (default 16). Notes sit on rows; a note's delay
column shifts it by a fraction of a row.

**Straight**
Timing with no swing: the second note of every pair falls exactly halfway
through it. A swing of 50%.

**Swing** *(planned)*
A timing feel that delays the second note of each eighth-note pair (4 rows),
giving a long-short, lilting feel. Stored as a percentage of the pair taken
by its first note:
50% is straight, about 67% is triplet swing (jazz, shuffle, boogie), and
75% is very heavy. Only the second note of a pair moves; on-beat notes
never do. A song-level setting applied at playback time to everything
scheduled against it, not baked into note data.
