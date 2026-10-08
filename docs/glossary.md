# Glossary

Terms with a specific meaning in this project.

**Aftersound**
The slow tail after a piano note's fast first decay, from the strings'
out-of-phase motion draining the bridge more slowly.

**Clip launcher**
The grid of clips inside Live View: one column per track, one row per scene,
each slot triggered on the next bar. On a Launchpad it is the pad grid in the
Live mode (the device's Session button). See launchpad.md and scenes.md.

**Groove**
The general word for rhythmic feel: how a pattern's timing (and often
accents) deviates from a rigid grid. Swing is the simplest groove. Other
DAWs go further. Ableton's *Groove Pool* holds templates of per-position
timing and velocity that you apply to clips. Renoise's *Groove* panel is
closer to this project: four per-line amounts that repeat every four lines
and apply song-wide at playback, without changing the pattern data. Here,
"groove" means timing feel only, and for now that is just swing. The
Library's drum patterns are not grooves in this sense; see Rhythm.

**Live sequencer**
A tool used to program, trigger and manipulate musical patterns, notes and
rhythms in real time during a performance. Here that is Live View: clips are
launched, stopped and recorded into while the transport runs, changes land on
the next bar, and scenes launch a whole row of clips together. What is played
can also be placed into the arrangement.

**Live View**
The view with the clip launcher, the outline panel and the pattern editor
showing each track's own clip; Tab switches to and from the arrangement
view. The Launchpad's Live mode shows the same clip grid on the pads.

**Rhythm**
A pre-written drum pattern in the Library, such as Waltz, Funk or Bossa
Nova. It is content (which drum hits fall on which rows), where swing is
feel (when those rows sound), so the same rhythm can be played with or
without swing. A few (Swing, Boogie, Jazz Waltz) carry a swing of their own,
heard when previewed and adopted by the song on Add to Song. Named Rhythm, not Groove, to keep clear of the DAW sense of
groove above.

**Row**
This engine's grid step: one sixteenth note, so a 4/4 bar is 16 rows and a
beat 4 (other time signatures: see time_signatures.md). Notes sit on rows; a note's delay
column shifts it by a fraction of a row.

**Scene**
A row of the clip grid: the clips at one position across every track's clip
list, launched together. It has no object of its own beyond an optional
name, tempo and time signature stored by position. See scenes.md.

**Strike point**
Where a hammer or pluck meets the string, as a fraction of its length. Modes
with a node there are not excited: at 1/8, the 8th, 16th and 24th partials.

**Swing**
A timing feel that delays the second note of each eighth-note pair (4 rows),
giving a long-short, lilting feel. Stored as a percentage of the pair taken
by its first note: 50% is no swing (the second note falls exactly halfway
through the pair), about 67% is triplet swing (jazz, shuffle, boogie), and
75% is very heavy. The 50-75% range and what the percentage means follow the
Akai MPC's swing. Only the second note of a pair moves; on-beat notes
never do. A song-level setting (`Song::getSwing()`, `<song swing="">`)
applied at playback time to everything scheduled against it, not baked into
note data.

**Unison**
The strings struck together for one piano key, tuned a cent or so apart.
Their slow beating, and their slightly different decay rates, give a piano
note its shimmer and its double decay. See additive.md.
