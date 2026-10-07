# Launchpad

A connected Novation Launchpad (Mini MK3 / X) is optional; the
terminal UI does everything without one. Each connected device has its own
grid mode, so one can sit in Session view while another does note entry.

## Where it comes from

- **Buttons.** The right-hand column carries the Launchpad X's own labels
  (Record Arm, Volume, Pan, Send A, Send B, Stop Clip, Mute, Solo) and
  launches a scene while mixer submode is off. In the shift layer, Undo, Redo
  and the click sit where Novation's Launchpad Pro MK3 puts them, and the Tempo
  and Swing views follow that device's views; the other shift functions are
  ours. Undo and Redo only report that they are not implemented.

## Note mode

Note mode (96) is the playing surface: a 4x4 drum rack on a percussion track,
an in-key scale keyboard on a pitched one, and, with a clip open, 32 steps
above it. It is described in [drums-and-sequencer.md](drums-and-sequencer.md).

## Grid modes

Selected with the top-row buttons. 95, 96, 97, Draw, Tempo and Swing are one
exclusive group; pressing one always selects it, and the only way out of a mode is
selecting a different one (Tempo and Swing also close by repeating their
gesture).

| Button | Mode |
| --- | --- |
| 95 | Session: the clip grid |
| 96 | Note: the drum rack or scale keyboard (plus the step rows while a clip is open for editing) |
| 97 | Custom: nothing yet |
| shift + 97 | Draw: a per-pad coloring toy, independent of the song |
| shift + Send B | Tempo: the song's tempo as a number on the pads |
| shift + Stop Clip | Swing: the song's swing as a number on the pads |

A second press of 95 while on the plain Session grid toggles **mixer submode**
(see below). Its LED is dim green away from Session, bright green in Session,
and orange in mixer submode.

## Right-side buttons

The right column differs per model. Pads, the top row and the shift
functions below are the same on both. The Pro MK3 has no layout of its own and
is untested.

### Launchpad X

Record Arm (19), Volume (89), Pan (79), Send A (69), Send B (59), Stop Clip
(49), Mute (39) and Solo (29) share one dispatch.

In Note mode, Record Arm (19) instead starts and stops capturing what is
played into the arrangement; its LED is bright red while capturing, dim red
otherwise.

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

### Launchpad Mini MK3

The Mini's buttons are labelled differently: seven scene launch buttons, and
the lowest one is Stop/Solo/Mute. The Mini has no mixer mode at all (no
Volume/Pan/Send faders, and a second press of 95 does nothing special), but it
keeps the shift actions below.

| Button | Function |
| --- | --- |
| 89, 79, 69, 59, 49, 39, 29 (top to bottom) | launch the scene at that row (rows 1-7 from the top) |
| 19 (Stop/Solo/Mute) | cycles the bottom pad row between clips, Stop, Solo and Mute |

The bottom pad row starts out showing clips like the rest of the grid. Each
press of the Stop/Solo/Mute button moves to the next function, and the press
after Mute returns to clips:

| Press | LED | Bottom row |
| --- | --- | --- |
| default / fourth | white | clips |
| first | red | Stop: bright pad = a clip is playing on that track; pressing it stops the track |
| second | blue | Solo: bright pad = the track is soloed; pressing it toggles solo |
| third | yellow | Mute: bright pad = the track is audible; dim = muted; pressing it toggles mute |

In Note mode the Stop/Solo/Mute button still starts and stops capture, as
Record Arm does on the other models. While shift is held the right column
shows the shift functions below by position (Volume, Pan, ... are the Mini's
scene buttons 89, 79, ...).

### Shift

Holding the up arrow (91) turns the eight right-side buttons into the
alternate functions below. Their LEDs show only these while shift is held, and
the others do nothing. A plain tap of 91 still means move-row-up, deferred to
release so a combination never has to be undone.

| Button | Action |
| --- | --- |
| Record Arm (19) | **Undo** (reserved, not implemented yet) |
| Mute (39, Pro MK3 30) | **Redo** (reserved, not implemented yet) |
| Solo (29, Pro MK3 20) | **Metronome** click |
| Volume (89) | **Duplicate** |
| Pan (79) | **Delete** |
| Send A (69) | **Quantise** |
| Send B (59) | **Tempo** view |
| Stop Clip (49) | **Swing** view |
| 97 | **Draw** mode |
| a clip pad | **Select** the clip without launching it |
| 96 (Note) | **Step edit**: open or close the selected clip's step grid |

Following the Pro MK3's own shift layer, Undo, Redo and the click sit where
Novation puts them; the rest are ours. Undo and Redo only report that they are
not implemented. LEDs: Undo and Redo dim white, Delete magenta (red is
Quantise's off state).

#### Select a clip

Hold shift and press a clip pad to select it without launching it, empty slots
included. The track and clip become the cursor, so the next recording or paste
lands there. Nothing opens by itself: shift + Note (96) then opens the
selected clip for step editing, as under the drum machine, and closes it again.

#### Duplicate

Hold shift and Volume, then press a populated clip pad to copy that clip into
the slot below it, overwriting whatever is there (an overwritten clip's
arrangement placements are removed). One hold can copy several clips.

#### Metronome

Press shift + Solo to toggle a click on every beat while the transport plays,
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

Hold shift and Pan, then press a clip pad to delete what its slot holds, one
layer per press: a populated slot loses its clip (leaving an empty slot in
place, so the scene rows of every other track stay aligned), and an empty slot
loses its stop button. Session view only. The terminal's `kill-region` in the clip grid
does the same, but also puts the clip on the clipboard (see `terminal.md`).

With the transport stopped, or when the clip is not sounding, the delete is
instant. A clip that is playing, or queued, on its track while the transport
runs is never pulled out from under the playhead: its track is stopped at the
next bar, and the clip is removed once that has taken effect. There is no undo
or confirmation. The LED is red, bright while held.

#### Tempo and Swing views

Shift + Send B opens the **Tempo** view (blue and white) and shift + Stop Clip
the **Swing** view (orange and white), as on Novation's Launchpad Pro MK3. The
value is drawn as a number on the pads, the digits side by side with no
margin: the colour change between neighbours keeps them apart. The tens digit
is the white one, always in the same place (columns 3-5); the units digit
beside it and a narrow two-column hundreds digit before it are in the view's
colour. 120 shows a blue 1, a white 2 and a blue 0, 50 a white 5 and an orange
0, and a one-digit value has nothing white.

- **Arrows:** the up arrow (91) and down arrow (92) change the value by one,
  on press. A hold repeats, after 400 ms and then every 100 ms. 91 is not
  shift in a view.
- **Leaving:** repeat the gesture that opened the view to go back to the mode
  you came from, or select a mode with 95, 96, 97 or Draw. Plain Send B and
  Stop Clip (no shift) switch between the two views. The pads, 93, 94 and the
  rest of the right column do nothing in a view.
- **LEDs:** the arrows are white. Send B (Tempo) and Stop Clip (Swing)
  show their colours, bright for the view showing and dim for the other, which
  a press switches to.
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

## Step grid

Hold 91 and press a Session pad to open that pad's clip for step editing; the
steps and the playing surface are described in
[drums-and-sequencer.md](drums-and-sequencer.md).
