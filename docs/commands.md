# Pattern effect commands

A pattern row's effect column holds a 4-character command: a 2-character
mnemonic followed by a 2-digit hex argument (or two separate hex nibbles,
where noted) - e.g. `ZB00`. Character validation in the editor is
permissive (any letter, not just the mnemonics below), but only the
commands in the per-note and "Other implemented" tables below actually do anything during playback;
everything else is accepted and stored but currently a no-op (see
`SongState.h`'s own command-handling loop).

The effect column follows the tracker tradition described in the README. The
mnemonic's first character is either `Z` (a global command, not
scoped to any one track - `ZBxx`/`ZTxx` below), `Y` (this engine's own
reserved namespace - see below), or a device index: how far up *this*
track's own ancestor chain the command actually targets (Group/Effect
tracks, not a flat per-track device list - the equivalent traversal this
engine's own track hierarchy actually offers). `-` (stored internally as
`0`, an accepted synonym for it - `Command::updateData()`) means the
track itself, `1` its parent, `2` its grandparent, and so on further up
the tree. Nothing beyond `-`/`0` is implemented yet - typing `1`, `2`, …
is accepted and stored (nothing rejects it) but currently behaves
exactly like `-` at playback, since nothing reads it yet. Every command
below is written with `-`, the form to actually type for a command that
doesn't (yet) target anything upstream.

`Y` is reserved as this engine's own namespace for a genuinely new
concept, not yet meaningful as a first character anywhere else in this
scheme. It works the same way `Z` already does for global commands: as
the first character, it replaces the device-index digit entirely,
opening a whole separate letter space where the *second* character is
free to mean anything - including reusing a letter already spoken for
elsewhere (`L`/`M` below, for instance), since the first character
already tells the two apart unambiguously - rather than hunting for a
single scarce free letter inside the crowded device-index namespace
every time a new idea needs one.

**Source** below says where the second character (the action letter)
came from - checked deliberately for every command in this table, not
just guessed.

## Where commands are read from

Playback reads a row's commands from two places, in this order
(`SongState::applyRowCommands()`): the track's own background pattern
in the arrangement, then a placed clip's own pattern at the row the clip is
supplying. So automation recorded into the background keeps playing
under any clip, and a clip's own command wins where both set the same
thing on the same row. `ZBxx` (pattern break) works from either, and
does the same wherever the clip is played from.

## Per-note commands

These work in a note column's local fx column as well as in the effect column.
The fx column sits after the delay column, is always shown, and holds three
characters: the command without its device-index digit, which is always an
implicit `-`/`0` there (so a note's `R24` is the row command `-R24`). A
character that is not set is `.` in the file and `·` on screen. It is
stored on the note (`fx` attribute, `---` when unset) and edited like the
effect column; Delete/Backspace clear all three characters. Typed in the effect
column a command acts on the whole track, in a note's fx column on that note
alone. Any other command typed in the fx column is stored but does nothing.

A cell can carry fx with no note: it starts and stops nothing and the fx acts on
its column, which a note-less cell has to be able to do for a command that
carries on from an earlier row. `Gxx`/`G00` there sets or repeats that column's
glide rate, and `Rxy` retriggers the column's last note; the others (`P`, `W`,
a tuning correction) need a note to belong to and do nothing. Such a cell is
saved as a `<note>` with an `fx` attribute and no text.

| Command | Description | Source |
|---|---|---|
| `-Pxx` | Set azimuth to an absolute position (as a note's fx: that note's azimuth alone) - `xx` maps linearly from -90 degrees at `00` through +90 at `FF`. | Renoise (adapted) - matches Renoise's own `Pxx` "Track Pan" exactly, `xx` meaning included (`00`/`80`/`FF` = left/center/right), but that's a real, inherited limitation: it only reaches half this engine's own 360-degree azimuth range (the front hemisphere), since Renoise's own panning has no "behind" to reach in the first place. |
| `-Rxy` | Retrigger - re-fire every note still playing on the track (as a note's fx: only that note) every `y` ticks (12 ticks/row; `y=0`, or an interval reaching the next row, adds nothing) with volume factor `x` applied to each retrigger: `0`/`8` no change; `1`-`5` lower the original volume by 3/6/12/25/50%; `6`/`7` cumulatively lower by 33/50%; `9`-`D` raise it by 3/6/12/25/50%; `E`/`F` cumulatively raise by 50/100%. | Renoise (`Rxy`) |
| `-Wxx` | Set ambisonic extent - the source's physical half-width in meters (`SphericalPosition::extent`, 0 = a point source). `xx` maps linearly from 0 m at `00` up to 8 m (`Command::kMaxExtentMeters`) at `FF`. As a row command it sets the track's extent for the notes that start after it (voices already sounding keep theirs); as a note's fx, that note's extent alone. Extent is also how far a track's notes are spread around its position (`docs/spatial.md`). | Renoise (adapted) |
| `+hh` / `-hh` | Tuning correction - add (`+`) or take away (`-`) `hh` cents (two hex digits, 1 cent each, up to 255) to that note's pitch. Only a note's fx can carry it: it is a property of one note, and the sign takes the place of the mnemonic. Written by `apply-just-intonation-region` (below), which gives every pitched note the correction that brings it to its gamut note's just ratio (the song's scale names the gamut; live and recorded notes get it too; nothing in a scale without one); `+00` marks a note as tuned that needs none. Transposing a tuned note gives it the correction for its new pitch; `clear-tuning-correction-region` removes them. | Tracker finetune (ProTracker `E5x`, Impulse Tracker `S2x`), adapted: a byte in cents instead of a nibble in 1/8 semitone, which is too coarse for microtonal tuning |

## Other implemented commands

| Command | Description | Source |
|---|---|---|
| `ZBxx` | Pattern break - when this row ends, jump to row `xx` (hex, from 0) of the next bar instead of playing on; past the bar's end it lands on the bar's last row. `ZB00` starts the next bar from its first row. Ends a scene early: on a clip's last row it keeps the clip bar-aligned, and a queued launch or stop fires on that bar. A launched clip keeps its own place (the live clock doesn't follow the jump), so a clip shorter than a whole number of bars still loops in step with the others. On a bar's last row `ZB00` changes nothing. | Renoise (`ZBxx`), adapted: a bar stands in for its pattern |
| `-Lxx` | Set Volume (Send Main) - an absolute level: `xx` (0-255) maps linearly in dB from -80dB up to 0dB/unity at 255, applied the instant this row starts and reaching every already-sounding voice too. | Renoise (`Lxx`, "Track Level") |
| `ZTxx` | **Use a tempo change in the arrangement instead** (`set-tempo-change` in Arrangement view, `<tempo row bpm/>` in the file): it is one object at a row, shown in the arrangement grid and the info line, and recording a live show writes it; `ZTxx` is for a tempo that has to travel inside a pattern or a clip. Set the tempo to `xx` BPM (hex: `78` = 120), from this row on - the row itself is already the new length, and every other command on it (a slide's ramp, say) times itself by it. A tempo change in the arrangement (the same effect) comes first on its row and a `ZTxx` on that row wins over it. Global like `ZBxx`: from any track, read from the background and from a placed clip like any command, and from a launched clip on a track Live View has taken over (whose other automation doesn't apply). `20` (32 BPM) to `FF` (255 BPM); below `20` it does nothing, except `ZT00` stops the song instead: the row plays, then the transport stops on the row after it, so play carries on from there - a pause like the transport toggle's (launched clips keep their place, a sample take ends), and where an offline render ends, its release tail after it. Of several on one row, the last track in the song wins. It changes the tempo playback runs at, not the song's own tempo, which stays where playing from the start begins (the info line shows the playing tempo while the transport runs): a seek or restart recomputes it from the arrangement up to that row - the last `ZTxx` or arrangement tempo change before it, else the song's tempo - and a pattern break keeps it. Editing the song's tempo still applies at once. | Renoise (`ZTxx`), adapted: the song's tempo is a starting tempo the command never changes, as in classic trackers' tempo command, so a song plays the same each time from the top |
| `YMxy` | Set Volume (Send Main) with an explicit glide, timed the same way a live Launchpad fader glide already is - wall-clock seconds, unaffected by tempo (a fader press's own velocity-driven speed has nothing to do with it). `M` for **M**ain. `x` (0-15) is the target, a send position: `0` is off, `1` to `F` go from -42 dB in 3 dB steps up to 0 dB (unity) at `F` - the same 16 levels the Launchpad send faders and the clip grid's dials rest on; `y` (0-15) is the glide's own duration, exponential in seconds: `duration_seconds = kMinFaderRampSeconds * (kMaxFaderRampSeconds / kMinFaderRampSeconds) ^ (y / 15.0)` - `y=0` the fastest (0.03s), `y=15` the slowest (8s), so both a near-instant move and a slow fade keep useful resolution. Recorded automatically by a Launchpad Volume/Send Main fader press (`LaunchpadManager::recordFaderAutomationIfArmed()`) - into the take's clip during a clip take, else into the arrangement's background; hand-typing works the same way. | Classic tracker (IT) - Impulse Tracker's own `Mxx` sets channel volume directly, the same concept. |
| `YAxy` | Send A's own equivalent of `YMxy` - same encoding, targeting Send A instead of Volume. `A` for Send **A**. | Own |
| `YBxy` | Send B's own equivalent of `YMxy` - same encoding, targeting Send B instead of Volume. `B` for Send **B**. | Own |
| `YZxy` | Set azimuth with an explicit glide, timed the same way `YMxy`/etc. are - wall-clock seconds. `Z` for a**Z**imuth. Not called `YPxy`/matched to `-Pxx`'s own encoding: `-Pxx`'s `xx` only reaches half the circle, which would throw away exactly the range a live Pan press can actually reach, so `x` here instead spans the *full* circle in 16 steps of 22.5 degrees, -180 degrees at `0`, dead front at `8`, up to +157.5 at `F` (+180 is the same direction as -180) - every Launchpad Pan pad target is exactly one step; `y` is the same duration encoding as `YMxy`. Recorded automatically by a Launchpad Pan fader press. | Own |
| `-Uxx` | Slide pitch up - `xx` (hex) is in 1/16ths of one scale step of the song's tuning (`10` = one step: 100 cents in 12-EDO, about 38.7 in 31-EDO). The whole amount ramps linearly across the row, the phase increment interpolated per sample within each audio chunk (the slide's position is computed absolutely at each chunk end, so chunk length doesn't matter), independent of ticks per row, on every voice sounding in the track, including one started on the same row. A note that starts late in the row (a delay or swing) glides at the same rate as the others from its own pitch, so it gets only the rest of the amount. On a percussion track a hit slides too, a step being the song's tuning's. The offset is cumulative per voice, adds to the note's fine-tune and resets with the next note. `U00` repeats the track's last nonzero amount, so a column of `U00` under a `U10` keeps sliding at the same rate; `U` and `D` share the memory (`D00` after `U10` slides down by `10`), and a stop or seek clears it. | Renoise (`Uxx`), generalised to the tuning |
| `-Dxx` | Slide pitch down - as `-Uxx`, lowering the pitch. | Renoise (`Dxx`), generalised to the tuning |
| `YUxx` | Slide the song's pitch up - as `-Uxx` (same unit, same per-row ramp, `00` repeating the last amount, `U` and `D` sharing it), but for every track at once: it moves the song's pitch offset, which is applied after every per-track pitch effect, so notes started later sound at it too and nothing on a track resets it. The memory is the song's, separate from every track's `-U`/`-D`. Rows of several tracks on the same row add up. The offset is counted in sixteenths of a step and is not limited (only a voice's own frequency is, 20 octaves either way); when the tuning changes, the count stays and its size follows the new tuning. It is 0 at the start of the song; a seek recomputes it from the arrangement up to that row (a pattern break or a loop keeps it). The info line shows it, in steps, with the standard pitch it makes. A sample track's audio follows it too, pitch-shifted (its length kept) from the standard pitch it was recorded at to the song's at each trigger: a lap already sounding keeps its pitch. | Own, after `-Uxx` |
| `YDxx` | Slide the song's pitch down - as `YUxx`, lowering it. | Own, after `-Dxx` |
| `-Gxx` | Glide to note - on a row that has a note, the note already sounding in that column glides to it instead of the note being retriggered (with nothing sounding, it plays as usual). The glide lands on the target note's pitch, its tuning correction included. `xx` (hex) is the rate in 1/16ths of one scale step of the song's tuning per row (`10` = a step a row, so a semitone a row in 12-EDO); `FF` is instant (and `G00` after it repeats that). The pitch moves linearly, interpolated per sample, and stops at the target. Each note column glides on its own: as a note's fx (`G04`) it is that column's rate, as a row command it sets the rate of every column. It moves only on rows that carry the command, so a long glide takes the rate on the first row (with the note) and `G00` in the effect column on the rows after, which carries each column on at the rate it last had (the row command's, for a column with none of its own) toward the same target; `G00` with no rate issued yet, or after a stop or seek, does nothing. The glide offset adds to a slide's. | Renoise (`Gxx`), generalised to the tuning |
| `YLxx` | Slide azimuth left - decrease the track's azimuth by `xx` degrees per tick (12 ticks/row) for the duration of this row, moving both the track's own position and every currently-sounding voice. `L` for **L**eft. | Own |
| `YRxx` | Slide azimuth right - same as `YLxx` but increasing azimuth (this engine's convention: positive azimuth = right). `R` for **R**ight. | Own |

## Planned

| Command | Description | Source |
|---|---|---|
| `-Axy` | Arpeggio | Renoise (`-Axy`) |
| `-Vxx` | Vibrato | Renoise (`-Vxy`) |
| `-Ixx` | Fade in | Renoise (`-Ixx`) |
| `-Oxx` | Fade out | Renoise (`-Oxx`) |
| `-Txy` | Tremolo (depth `x`, speed `y`) | Renoise (`-Txy`) |
