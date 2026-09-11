# Pattern effect commands

A pattern row's effect column holds a 4-character command: a 2-character
mnemonic followed by a 2-digit hex argument (or two separate hex nibbles,
where noted) - e.g. `ZB00`. Character validation in the editor is
permissive (any letter, not just the mnemonics below), but only the
commands listed as **Implemented** actually do anything during playback;
everything else is accepted and stored but currently a no-op (see
`SongState.h`'s own command-handling loop).

The mnemonic's first character is either `Z` (a global command, not
scoped to any one track - `ZBxx`/`ZTxx` below) or a device index: how far
up *this* track's own ancestor chain the command actually targets. `-`
(stored internally as `0`, an accepted synonym for it - `Command::
updateData()`) means the track itself, `1` its parent, `2` its
grandparent, and so on further up the tree - this engine's own
generalization of Renoise's numbered per-track DSP-device-chain digit to
ancestor *tracks* (Group/Effect) rather than a flat per-track device
list, since that's the equivalent traversal this engine's own track
hierarchy actually offers. Nothing beyond `-`/`0` is implemented yet -
typing `1`, `2`, … is accepted and stored (nothing rejects it) but
currently behaves exactly like `-` at playback, since nothing reads it
yet. Every command below is written with `-`, the form to actually type
for a command that doesn't (yet) target anything upstream.

**Source** below says where the second character (the action letter)
came from - checked against
[Renoise's own Pattern Effects reference](https://files.renoise.com/manual/PatternEffects_ReferenceCard.pdf)
for every command in this table, not just guessed: **Renoise** means the
letter and its meaning are borrowed directly, unchanged; **Renoise
(adapted)** means the letter is borrowed but this engine's own version
differs in some real way from Renoise's original (noted in the
description); **Own** means Renoise has no equivalent concept at all, so
the letter is just an unclaimed one, picked for that reason alone.

## Implemented

| Command | Description | Source |
|---|---|---|
| `ZBxx` | Pattern break - jump straight to row `xx` of the next pattern instead of playing out the rest of this one. | Renoise (`ZBxx`) |
| `-Hxx` | Slide azimuth left - decrease the track's azimuth by `xx` degrees per tick (12 ticks/row) for the duration of this row, moving both the track's own position and every currently-sounding voice. | Own - Renoise's own panning is 2D, with no slide-in-the-effect-column equivalent to this engine's full 3D azimuth to match letters against. |
| `-Kxx` | Slide azimuth right - same as `-Hxx` but increasing azimuth (this engine's convention: positive azimuth = right). | Renoise (adapted) - matches Renoise's own *panning-column* `Kx` (pan slide right there too), a different, note-row-scoped sub-column from this engine's single Command type, not the main effect-column vocabulary. |
| `-Lxx` | Set Volume (Send Main) - unlike the slide commands above, an absolute level: `xx` (0-255) maps linearly in dB from -80dB up to 0dB/unity at 255, applied the instant this row starts and reaching every already-sounding voice too. | Renoise (`Lxx`, "Track Level") |
| `-Fxx` | Set Send A - same encoding/behavior as `-Lxx`, for the track's own Send A level. | Own - sends are DSP-chain routing in Renoise, never a pattern command. |
| `-Mxx` | Set Send B - same encoding/behavior as `-Lxx`, for the track's own Send B level. | Own - same reasoning as `-Fxx`. |
| `-Pxx` | Set azimuth to an absolute position - `xx` maps linearly from -90 degrees at `00` through +90 at `FF`. | Renoise (adapted) - matches Renoise's own `Pxx` "Track Pan" exactly, `xx` meaning included (`00`/`80`/`FF` = left/center/right), but that's a real, inherited limitation: it only reaches half this engine's own 360-degree azimuth range (the front hemisphere), since Renoise's own panning has no "behind" to reach in the first place. |

## Planned

| Command | Description | Source |
|---|---|---|
| `-Uxx` | Slide pitch up | Renoise (`Uxx`) |
| `-Dxx` | Slide pitch down | Renoise (`Dxx`) |
| `-Gxx` | Glide to note | Renoise (`Gxx`) |
| `-Vxx` | Vibrato | Renoise (`Vxy`) |
| `-Ixx` | Fade in | Renoise (`Ixx`) |
| `-Oxx` | Fade out | Renoise (`Oxx`) |
| `-Txy` | Tremolo (depth `x`, speed `y`) | Renoise (`Txy`) |
| `ZTxx` | Set tempo to `xx` BPM - global like `ZBxx`, not a device-index command, since tempo isn't a per-track parameter. | Renoise (`ZTxx`) |
