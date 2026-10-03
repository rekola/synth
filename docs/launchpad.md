# Launchpad

A connected Novation Launchpad (Mini MK3 / X) is optional; the
terminal UI does everything without one. Each connected device has its own
grid mode, so one can sit in Session view while another does note entry.

## Note entry layout

A connected Novation Launchpad (Mini MK3 / X) becomes an
isomorphic note-entry grid, its layout generalizing the 12edo Wicki-Hayden
keyboard to any EDO via a best-fifth generator (`src/launchpad/LaunchpadLayout.h`).

### Colors

LED coloring originally followed the notational convention of Adriaan
Fokker's 31-EDO organ (built 1950 for Teylers Museum, Haarlem) and the
later Archiphone: each pad was colored by its *distance from the song's
diatonic scale* (tonic / diatonic degree / sharp / flat / diesis /
accidental), generalizing the idea that 31-EDO's chromatic notes split
into two musically distinct kinds (a full chromatic step vs. a
quarter-tone-ish shading) that a simple black/white keyboard can't
distinguish.

That scheme was replaced with a different organizing principle: coloring
by *consonance* rather than by scale-degree distance. Standard microtonal
note names (sharps, flats, double-flats, ...) are built from a fixed,
12-tone-shaped chain of fifths, which stops matching musical intuition
well once an EDO is fine enough that the interesting structure is no
longer "how many fifths from the tonic" but "how consonant is this
interval, period." The new scheme instead recursively factors the octave
the way classical interval theory does - `2/1 = 4/3 * 3/2` (the octave's
simplest factor pair is the fourth and fifth), then `3/2 = 6/5 * 5/4` (the
fifth's own simplest factor pair is the minor and major third), and so on
- reusing that same factoring operation, approximated proportionally, at
every deeper level. This reaches every pitch class (no leftover
"everything else" bucket) within 4-6 levels for all four supported EDOs.

Color encodes the resulting tree two ways: hue drifts away from a shared
starting point by a shrinking amount at each level (so a pitch stays
hue-close to its harmonic neighborhood, however deep the recursion goes),
and saturation fades with depth (so more distant/complex notes read as
more muted) - both channels survive the idle-brightness remap that only
lightness gets overridden by; tonic keeps a fixed, deliberately dissimilar
hue (yellow) so it always pops out.

The classification is purely a function of the EDO and key (see
`LaunchpadLayout::computeConsonanceLevels`), so it applies unchanged to
12/19/31/53-EDO.

## Grid modes

Selected with the top-row buttons. 95, 96, 97, Draw, Tempo and Swing are one
exclusive group; pressing one always selects it, and the only way out of a mode is
selecting a different one (Tempo and Swing also close by repeating their
gesture).

| Button | Mode |
| --- | --- |
| 95 | Session: the clip grid |
| 96 | Note: isomorphic note entry (a step grid while a clip is open for editing) |
| 97 | Custom: the percussion lane picker |
| shift + Solo | Draw: a per-pad coloring toy, independent of the song |
| shift + Send B | Tempo: the song's tempo as a number on the pads |
| shift + Stop Clip | Swing: the song's swing as a number on the pads |

A second press of 95 while on the plain Session grid toggles **mixer submode**
(see below). Its LED is dim green away from Session, bright green in Session,
and orange in mixer submode.

## Right-side buttons

Record Arm (19), Volume (89), Pan (79), Send A (69), Send B (59), Stop Clip
(49), Mute (39) and Solo (29) share one dispatch.

| Button | Mixer submode off (default) | Mixer submode on |
| --- | --- | --- |
| each of the eight | launches the scene at its row | the mixer radio group below |
| Volume / Pan / Send A / Send B | | enter that fader mode |
| Stop Clip / Mute / Solo / Record Arm | | open the track picker |

All eight show a uniform dim white while mixer submode is off. In mixer
submode each shows its own color, bright for the active one. A press that
switches to a different member of the group applies immediately; holding it
for 600 ms or more previews it and reverts on release.

The track picker lights the bottom grid row, one pad per track, in the color
of the action: red for Stop Clip and Record Arm, yellow for Mute, blue for
Solo. Picking a track toggles that action for it and leaves the picker open;
the same button again closes it.

### Shift

Holding the up arrow (91) turns the eight right-side buttons into the
alternate functions below. Their LEDs show only these while shift is held, and
the others do nothing. A plain tap of 91 still means move-row-up, deferred to
release so a combination never has to be undone.

| Button | Action |
| --- | --- |
| Volume (89) | **Duplicate** |
| Pan (79) | **Metronome** |
| Send A (69) | **Quantise** |
| Send B (59) | **Tempo** view |
| Stop Clip (49) | **Swing** view |
| Mute (39, Pro MK3 30) | **Delete** |
| Solo (29) | **Draw** |

Record Arm (19) does nothing under shift. Apart from Swing, which follows the
Pro MK3's own view, the assignment is simply the next free button. It carries
no meaning.

#### Duplicate

Hold shift and Volume, then press a populated clip pad to pick it as the
source (white). Press an empty slot in the same track column to copy it there.
The copy is independent and never overwrites. One hold can fill several slots.
Releasing Volume with a source picked and no destination copies to the next
empty slot.

#### Metronome

Press shift + Pan to toggle a click on every beat while the transport plays,
accented on the first beat of the bar. The same action is the
`toggle-metronome` command. It is not saved with the song. The LED is amber,
bright while on.

#### Quantise

Quantise follows the convention of a dedicated Quantise button: hold it and
press a clip to quantise it, tap it with shift to toggle recording quantised.
Here shift + Send A stands in for that button.

- **Quantise a clip:** hold shift and Send A, then press a populated clip pad.
  Every note in that clip snaps to the closest row (a row is a 16th note) and
  its sub-row delay is cleared. Note-offs move with their notes. A note that
  lands on an occupied slot takes the next free column, and an off the next
  row. A move past the end of the clip wraps in a looping clip and clamps in
  a one-shot. Several pads can be quantised in one hold. Audio clips and empty
  slots are left alone. From the terminal, `quantize-clip` does the same for
  the clip under the cursor in the clip grid.
- **Record Quantise:** tap shift + Send A with no pad pressed to toggle it
  (`toggle-record-quantize`). The toggle happens on release, because the same
  hold also quantises clips. With it off, the default, Session recording keeps
  the exact timing of every press and release. With it on, each press and
  release snaps to the nearest row as it is recorded. The setting is saved
  with the song.
- **LED:** while shift is held, Send A shows red when Record Quantise is off,
  green when it is on, and white while held.

#### Delete

Hold shift and Mute, then press a clip pad to delete what its slot holds, one
layer per press: a populated slot loses its clip (leaving an empty slot in
place, so the scene rows of every other track stay aligned), and an empty slot
loses its stop button. Session view only. The terminal's `delete-clip` does the
same.

With the transport stopped, or when the clip is not sounding, the delete is
instant. A clip that is playing, or queued, on its track while the transport
runs is never pulled out from under the playhead: its track is stopped at the
next bar, and the clip is removed once that has taken effect. There is no undo
or confirmation. The LED is red, bright while held.

#### Tempo and Swing views

Shift + Send B opens the **Tempo** view (blue and white) and shift + Stop Clip
the **Swing** view (orange and white), as on Novation's Launchpad Pro MK3. The
value is drawn as a number on the pads. The tens digit is always the white one,
centered; the hundreds and units digits sit beside it in the view's colour,
clipped by the edge of the grid. There is no padding, so 120 shows a white 2
between a blue 1 and 0, 50 a white 5 and an orange 0, and a one-digit value has
nothing white.

- **Arrows:** the up arrow (91) and down arrow (92) change the value by one. A
  hold repeats, after 400 ms and then every 100 ms. 91 is also shift, so its
  step happens on release and is skipped if anything was combined with it,
  including the gesture that switches views.
- **Leaving:** repeat the gesture that opened the view to go back to the mode
  you came from, switch to the other view with its gesture, or select a mode
  with 95, 96, 97 or Draw. The pads, 93, 94 and the rest of the right column do
  nothing in a view.
- **LEDs:** the arrows are white, and the button that opened the view stays lit
  in its colour. While shift is held, Send B (Tempo) is blue and Stop Clip
  (Swing) is orange.
- **Tempo** is 20 to 300 bpm and takes effect immediately, also while playing
  (commands `tempo-increase` and `tempo-decrease`).
- **Swing** is 50 to 75 per cent (commands `swing-increase` and
  `swing-decrease`). 50 is straight; about 67 is triplet swing. The second
  note of every eighth-note pair plays late, applied at playback to everything,
  never changing the notes themselves. Library rhythms carry a swing of their
  own: it is heard when previewing one, and Add to Song sets the song's swing
  to it.

## Session view

Rows are a track's clip list, columns are the tracks. Every connected device
follows one shared cursor track. Launching is quantized to the next bar and
plays inside the one transport, per track: a launch takes the track over from
the arrangement, a stop takes it over silently, and "back-to-arrangement"
hands it back. Pressing the active pad relaunches its clip from the start at
the next bar.

Pad lighting: green pulses while playing and flashes while queued. On an armed
track the column is red instead: dim for an empty slot, flashing when queued
to record, pulsing while recording.

### Session Record (98)

A tap overdubs the playing clip of each armed track (the followed track's if
none is armed) from the next bar. While any take is running, a tap stops them
at the next bar and leaves the clips playing. A long hold is Capture MIDI,
which is not implemented. Shift + 98 toggles the arrangement's own Record Arm,
which makes a Session pad press write the clip into the arrangement. Its LED
is bright red while anything records, dim red otherwise.

Notes played on the grid during a take are recorded with their exact timing
unless Record Quantise is on (see Quantise above).

## Step grid and drum machine

A percussion track with lanes, or a pitched track's clip, can be edited as a
step grid: rows are lanes or scale degrees, columns are steps. Hold 91 and press
a Session pad to open that pad's clip; the pad resolves on release. Every
device then switches to the step grid, each showing its own page. In the step
grid, 93 and 94 scroll the steps, and on a pitched track 91 and 92 scroll the
rows. A lone press of 95 closes it. 97 opens the lane picker for a percussion
track, which is how a lane-less track gains its first lane.
