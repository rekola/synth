# Pattern effect commands

A pattern row's effect column holds a 4-character command: a 2-character
mnemonic followed by a 2-digit hex argument (or two separate hex nibbles,
where noted) - e.g. `ZB00`. Character validation in the editor is
permissive (any letter, not just the mnemonics below), but only the
commands listed as **Implemented** actually do anything during playback;
everything else is accepted and stored but currently a no-op (see
`SongState.h`'s own command-handling loop).

The mnemonic's first character is either `Z` (a global command, not
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
elsewhere (`L`/`F`/`M` below, for instance), since the first character
already tells the two apart unambiguously - rather than hunting for a
single scarce free letter inside the crowded device-index namespace
every time a new idea needs one.

**Source** below says where the second character (the action letter)
came from - checked deliberately for every command in this table, not
just guessed.

## Implemented

| Command | Description | Source |
|---|---|---|
| `ZBxx` | Pattern break - jump straight to row `xx` of the next pattern instead of playing out the rest of this one. | Renoise (`ZBxx`) |
| `-Lxx` | Set Volume (Send Main) - an absolute level: `xx` (0-255) maps linearly in dB from -80dB up to 0dB/unity at 255, applied the instant this row starts and reaching every already-sounding voice too. | Renoise (`Lxx`, "Track Level") |
| `-Fxx` | Set Send A - same encoding/behavior as `-Lxx`, for the track's own Send A level. Deprecated in favor of `YAxy`. | Own |
| `-Mxx` | Set Send B - same encoding/behavior as `-Lxx`, for the track's own Send B level. Deprecated in favor of `YBxy`. | Own |
| `-Pxx` | Set azimuth to an absolute position - `xx` maps linearly from -90 degrees at `00` through +90 at `FF`. | Renoise (adapted) - matches Renoise's own `Pxx` "Track Pan" exactly, `xx` meaning included (`00`/`80`/`FF` = left/center/right), but that's a real, inherited limitation: it only reaches half this engine's own 360-degree azimuth range (the front hemisphere), since Renoise's own panning has no "behind" to reach in the first place. |
| `YMxy` | Set Volume (Send Main) with an explicit glide, timed the same way a live Launchpad fader glide already is - wall-clock seconds, unaffected by tempo (a fader press's own velocity-driven speed has nothing to do with it). `M` for **M**ain. `x` (0-15) is the target (same linear-in-dB mapping as `-Lxx`, nibble instead of byte resolution); `y` (0-15) is the glide's own duration, linear in seconds: `duration_seconds = kMinFaderRampSeconds + (y / 15.0) * (kMaxFaderRampSeconds - kMinFaderRampSeconds)` - `y=0` the fastest (0.03s), `y=15` the slowest (1.0s). Recorded automatically by a Launchpad Volume/Send Main fader press (`LaunchpadManager::recordFaderAutomationIfArmed()`); hand-typing works the same way. | Classic tracker (IT) - Impulse Tracker's own `Mxx` sets channel volume directly, the same concept. |
| `YAxy` | Send A's own equivalent of `YMxy` - same encoding, targeting Send A instead of Volume. `A` for Send **A**. | Own |
| `YBxy` | Send B's own equivalent of `YMxy` - same encoding, targeting Send B instead of Volume. `B` for Send **B**. | Own |
| `YZxy` | Set azimuth with an explicit glide, timed the same way `YMxy`/etc. are - wall-clock seconds. `Z` for a**Z**imuth. Not called `YPxy`/matched to `-Pxx`'s own encoding: `-Pxx`'s `xx` only reaches half the circle, which would throw away exactly the range a live Pan press can actually reach, so `x` here instead spans the *full* circle, -180 degrees at `0` through +180 at `F`; `y` is the same duration encoding as `YMxy`. Recorded automatically by a Launchpad Pan fader press. | Own |
| `YLxx` | Slide azimuth left - decrease the track's azimuth by `xx` degrees per tick (12 ticks/row) for the duration of this row, moving both the track's own position and every currently-sounding voice. `L` for **L**eft. | Own |
| `YRxx` | Slide azimuth right - same as `YLxx` but increasing azimuth (this engine's convention: positive azimuth = right). `R` for **R**ight. | Own |

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
| `-Wxx` | Set ambisonic extent - the track's own physical half-width in meters (`SphericalPosition::extent`/`LeafTrack::setExtent()`, 0 = a point source) - `xx` maps linearly in steps of 1/256, `00` = 0m up through `FF` at some chosen max (not settled yet). | Renoise (adapted) |
| `ZTxx` | Set tempo to `xx` BPM - global like `ZBxx`, not a device-index command, since tempo isn't a per-track parameter. | Renoise (`ZTxx`) |
