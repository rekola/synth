# Lineage

synth borrows from many instruments, trackers, DAWs and standards. It is not a
copy of any one of them. Where a convention already exists we follow it and say
so here; where none does, the choice is our own and is marked as such. This
file is the one place to check before inventing something new, and the one to
update when a choice is made.

Each row has a status:

- **Same**: it follows the convention exactly.
- **Adapted**: it follows the convention with a stated difference.
- **Own**: no convention was followed; the choice is ours.

Per-command sources for pattern effect commands are in
[commands.md](commands.md) and are not repeated here.

## Pad surfaces and the Launchpad

| What | Lineage | Status |
|---|---|---|
| 4x4 block of 16 drum pads | Akai's MPC samplers (from 1988) made it the standard drum surface; drum racks in Ableton Live show 16 of their 128 notes at a time in the same shape | Same (the shape) |
| Pad order: General MIDI notes 36-51, left to right then bottom to top | The usual default window of a sampler or drum rack | Same |
| Only one 4x4 bank, no paging | | Own: other banks and their order are left out until a layout is known exactly |
| In-key scale keyboard: a degree per column, a fourth per row | The in-key layout of Ableton's Push | Adapted: the scale is the song's, in any of 12/19/31/53-EDO, and plays as major when none is set |
| Playing surface plus step rows on one grid | The split layout of Ableton's Push | Adapted: 4 + 4 rows, 32 steps, the last pad pressed stays selected (it is not held), a pitched note lasts one step |
| Session view with a clip grid, scenes and per-slot stop buttons | Ableton Live's Session view, and the Launchpad's scene-launch column | Adapted |
| Clips launch on the next bar | Live's global launch quantisation, whose default is one bar | Adapted: the quantisation is fixed to a bar |
| Scenes carry a name, tempo and time signature | Live's scenes (name, tempo and time signature) | Adapted: see [scenes.md](scenes.md) |
| Right-hand button column: scene launch, or Volume / Pan / Send A / Send B / Stop Clip / Mute / Solo / Record Arm | The Launchpad X's own button labels | Same |
| Shift layer: Undo, Redo, click, Duplicate, Delete, Quantise, Tempo and Swing views | Novation's Launchpad Pro MK3 | Adapted: Undo and Redo only report that they are not implemented; Draw is ours |
| Tempo and Swing shown as a number on the pads | Novation's Launchpad Pro MK3 views | Adapted: see [launchpad.md](launchpad.md) |
| Hold a mixer button to preview, tap to switch | The usual momentary-preview convention on pad controllers | Same |
| Session Record and Capture MIDI | Ableton Live's Session Record button and Capture MIDI | Adapted: Capture MIDI is not implemented |
| Pad colouring by consonance | Adriaan Fokker's 31-EDO organ and the Archiphone coloured pads by distance from the diatonic scale; we colour by consonance instead | Own (see [launchpad.md](launchpad.md)) |
| Draw mode | | Own |

## Mixer and transport

| What | Lineage | Status |
|---|---|---|
| Send A and Send B feeding two shared effect returns | Auxiliary sends and returns on any mixing desk; Live's default two return tracks | Adapted: two fixed slots, each a reverb, delay or granular effect |
| Send Main | | Own: the track's dry level, named to sit beside Send A and B |
| Track Monitor: In / Off / Auto | Live's track monitoring (In, Auto, Off) | Adapted: Auto also monitors a note track while nothing is armed |
| Record arm, mute and solo on every track | Universal | Same |
| Back to Arrangement, with an orange marker on a track Session view has taken over | Live's Back to Arrangement | Adapted |
| Tab toggles Session and Arrangement view | Live's Tab shortcut | Same |
| Locators | Named markers in Live and other DAWs | Adapted: shown in the arrangement view |
| Record quantise as a song setting, metronome accented on the bar | Common in DAWs | Same |
| Swing 50-75 %, 50 % = straight, about 67 % = triplet swing | Akai's MPC swing range and meaning | Adapted: it moves the second note of each eighth-note pair (4 rows) |
| Song-wide groove as swing only | Renoise's Groove panel and Live's Groove Pool are richer | Own (see [glossary.md](glossary.md)) |
| Level meters with peak hold | Universal | Same |

## Tracker pattern editor

| What | Lineage | Status |
|---|---|---|
| Rows by tracks, with note, velocity, delay and effect columns | Trackers in general; the per-note delay column and several note columns per track come from Renoise | Adapted |
| A row is a sixteenth note | The usual tracker default | Same |
| Four-character effect commands | Renoise and Impulse Tracker | Adapted: the first character is a device index, `Z` or `Y`, see [commands.md](commands.md) |
| `Y` command namespace, azimuth and extent commands | | Own |
| Pattern break to a row of the next bar | Renoise's `ZBxx` | Adapted |

## Terminal keys

| What | Lineage | Status |
|---|---|---|
| Mark and point, kill ring, yank | Emacs | Same |
| M-x command prompt, ESC as the Meta prefix | Emacs | Same |
| C-x C-c quits, C-k kills a row | Emacs | Same |
| C-b as a second way to set the mark | | Own: C-SPC does not reach every terminal |
| Clips are copied and moved with the kill ring, not with their own commands | Emacs | Adapted: see [terminal.md](terminal.md) |

## Sound and standards

| What | Lineage | Status |
|---|---|---|
| General MIDI percussion map and GM SoundFont instruments | General MIDI and the SoundFont 2 format | Same |
| Ambisonic bus: ACN channel order, SN3D normalisation | The AmbiX convention | Same |
| Binaural decoding from SOFA files | The SOFA format and measured HRIR sets | Same |
| Magnitude-least-squares binaural decoder | The magnitude least squares method for ambisonic binaural rendering | Adapted: see CLAUDE.md |
| Resonant low-pass filter | The Moog ladder filter | Adapted |
| 12/19/31/53-EDO tunings, scales and note names | Equal divisions of the octave and their standard note spelling | Same |

## Deciding something new

1. Look in this file and in [commands.md](commands.md).
2. If a convention exists in the trackers, DAWs, controllers or Emacs lineage
   this project draws on, follow it, or record why not.
3. If it does not, make the choice and add an **Own** row here.
4. Only add a layout or number to this file once it is known exactly. It is
   better to leave something out than to add a subtly wrong version.
