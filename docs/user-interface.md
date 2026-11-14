# User interface

Everything the program does is a named command (`kill-region`, `launch-scene`,
`undo`, ...). Every command can be run by name from the
[command palette](#command-palette), most are in the menus, and a keymap binds
keys to them.

There are two keymaps, and a display uses one of them: the terminal the
terminal keymap, the window the desktop one. `--keymap gui` or `--keymap
terminal` picks one regardless of the display. The command names are the same in
both. The terminal keymap is based on Emacs's keybindings (mark and point, kill
and yank, M-x, the C-x prefix), but it is this program's own and not Emacs's:
it adds shortcuts Emacs has no counterpart for. It is used in the terminal because a terminal can't send many of the
key combinations the usual desktop shortcuts rely on (most Ctrl+Shift shortcuts,
and Ctrl-C, Ctrl-Z and Ctrl-S belong to the terminal itself), while Emacs's
prefix keys work in any terminal. The desktop keymap follows the usual desktop,
DAW and tracker conventions. A binding that has not been given a desktop shortcut
works in both. The commands that have one so far:

| Command | Desktop shortcut | Terminal shortcut |
| --- | --- | --- |
| command palette | Ctrl+Shift+P | M-x |
| `undo` | Ctrl+Z | C-x u, C-/ |
| `undo-redo` | Ctrl+Shift+Z, Ctrl+Y | C-M-_ |
| `kill-region` (cut) | Ctrl+X | C-w |
| `kill-ring-save` (copy) | Ctrl+C | M-w |
| `yank` (paste) | Ctrl+V | C-y |
| `rename` (scene, clip or track, in the clip grid) | Ctrl+R | C-c r |
| `add-sample-track` | Ctrl+T | C-c t |
| `add-instrument-track` | Ctrl+Shift+T | C-c T |
| `add-group-track` | Ctrl+G | C-c g |
| `add-percussion-track` | Ctrl+Shift+D | C-c d |
| `set-mark` (start a selection; on the desktop Shift+arrows set it and extend it) | Shift+Up/Down/Left/Right | C-SPC |
| `keyboard-quit` (cancel the selection) | Escape | C-g |
| `mark-whole-buffer` (select all) | Ctrl+A | C-x h |
| `widen-cursor`, `narrow-cursor` (the pattern editor's cursor, in note columns; the size of the chords in the open chord palette) | Ctrl+Shift+Right, Ctrl+Shift+Left | C-S-Right, C-S-Left |
| `chord-palette` (open the chord palette; Up and Down choose in it) | Ctrl+Space | M-/ |
| `toggle-stop-button` (clip grid) | Ctrl+E | |
| `stop-track-clips` (the cursor track's clips, not the others) | Ctrl+Enter | |
| `back-to-arrangement` | F10 | F10 |
| `toggle-record-arm` (record) | F9 | F9 |
| `quantize-clip` (the clip under the cursor in the clip grid; in the pattern editor the same shortcut is `quantize-region`, below) | Ctrl+U | |
| `quantize-region` (the pattern editor's selection) — **not built yet**, shortcut reserved | Ctrl+U | |
| `insert-scene` (an empty scene below the cursor's, shifting the later ones down) — **not built yet**, shortcut reserved | Ctrl+I | |
| `duplicate` — **not built yet**, shortcut reserved | Ctrl+D | |
| `double-clip-length` — **not built yet**, shortcut reserved | Ctrl+Up | |
| `halve-clip-length` — **not built yet**, shortcut reserved | Ctrl+Down | |
| `capture-midi` — **not built yet**, shortcut reserved | Ctrl+Shift+C | |
| `find-file` | Ctrl+O | C-x C-f |
| `save-buffer` | Ctrl+S | C-x C-s |
| `save-buffer-as` | Ctrl+Shift+S | C-x C-w |
| `save-buffers-kill-terminal` (quit) | Ctrl+Q | C-x C-c |

The desktop shortcuts follow Ableton Live's shortcuts where this program has the
same thing; an empty Terminal cell means the Emacs keybindings it is based on have no obvious counterpart, so the
shortcut is left free. `toggle-track-collapse` therefore has its shortcut (Ctrl+Shift+C) on
the terminal keymap only, as Capture MIDI has it on the desktop one.

Ctrl+X is cut there, so the `C-x` prefix sequences are terminal-only; the buffer
commands (`next-buffer`, `kill-buffer`, ...) have no desktop shortcut yet and are
in the menus and the command palette. What the terminal can't do is listed
under [Terminal](#terminal) at the end.

## Command palette

M-x (Meta-x: the Alt key with x, or Escape and then x) on the terminal keymap and
Ctrl+Shift+P on the desktop one open the command palette, which runs any
command by name. It sits on the last rows of the
screen, where the status line is: the query on the last row (`M-x ` or `> `),
the selected command's description above it, and the matching commands above
that, best match first. The list is a raised panel; the description (quiet and
italic) and the query are on the display's own background. The status line is
still used for messages and for prompts that ask for a name.

- Typing narrows the list by fuzzy matching: the letters in order, not
  adjacent (`tgv` finds `toggle-view`); an exact name ranks first, then a
  prefix, then matches at the start of words, then scattered letters.
- Tab completes what is typed as far as every command starting with it agrees,
  the way Emacs completes a command name; `[No match]` shows when none does. It never runs
  anything.
- Each row shows the command's keybinding in the display's keymap.
- Up/Down or C-n/C-p move, Enter or a click runs the selected command, Escape,
  C-g or a click outside cancels.

The palette takes M-x and Tab completion of the name from Emacs, and from the
command palette of Sublime Text the shortcut Ctrl+Shift+P, matching the typed
letters in order, and showing each command's keybinding and description. There is no minibuffer: the status line only shows
messages and asks for names (a file, a buffer, a new name).

## Terminal keymap

The rest of this page gives the terminal keymap, unless it says otherwise. Mark and point selection, kill and yank, M-x for any named command, ESC as the
Meta prefix, C-x C-c to quit and C-k to kill a row. Each open song is a buffer,
as in Emacs, and `next-buffer` and `previous-buffer` switch between them. An
operation that is a copy or a move is done with the kill ring rather than with
a command of its own, so there is no "duplicate" or "delete" command for clips.

## Kill, copy and yank

| Key | Command | What it does |
|---|---|---|
| M-w | `kill-ring-save` | Copy |
| C-w | `kill-region` | Cut |
| C-y | `yank` | Paste at the cursor |
| C-g | `keyboard-quit` | Cancel the selection |

Which thing they act on depends on the focused widget.

### Pattern editor

They act on the region: the note under the cursor, or the marked block of rows
and tracks (C-SPC sets the mark). C-x h (`mark-whole-buffer`) selects the whole song
instead, by view: in Live View every clip of every track, placed or not; in
Arrangement view every track's background pattern and the clips the
arrangement places (a clip placed twice is one pattern, acted on once). Any
cursor movement or C-g ends it. Transpose, humanize and the tuning commands act
on it; cut, copy and yank say they do not. To duplicate a track's notes, select
the track, copy it, move to another track and yank.

### Clip grid (Live View)

They act on the clip under the cursor.

- **Duplicate a clip:** copy it, move to another slot (the one below, say) and
  yank. The paste is an independent copy under a fresh id and replaces what is
  in the slot, together with that clip's arrangement placements.
- **Move a clip:** cut it, move, yank.
- **Between tracks:** yank only onto a track of the same kind, because a drum
  note has no pitch and audio only fits audio. A clip yanked into a song in
  another tuning moves to it by pitch, as notes do (below).
- **Delete a clip:** cut it. Del, Backspace and C-k do the same, so a deleted
  clip can always be yanked back. On an empty slot they remove the slot's stop
  button instead, which is not clip content and never reaches the clipboard.
- A clip that is playing is not pulled out from under the playhead: its track
  stops at the next bar and the clip is removed then.
- **Drag a clip:** press on a clip and move: the clip comes
  off its slot and follows the pointer, and the slot it would land on shows
  it. Release to drop it there (a cut and a yank in one undo step, with
  the same rules: only onto a track of the same kind, and what is in the slot
  is replaced). Ctrl copies instead of moving (the slot stays filled while
  it is held). Escape, or a release where it cannot land, puts it back. On a
  terminal the clip moves by whole cells, and some terminals keep Ctrl with
  the mouse for themselves; copy and yank (above) copy a clip there.
- **Drag a placed clip (Arrangement view):** press on a
  block and move: the block lifts off its bars and follows the pointer as one
  pass of the clip (a looping clip will run on once placed, as it did), and
  the bars that pass would cover show it. Release to put it there; the bar under the
  point you grabbed is where that point lands. The clip itself and its other
  placements stay. Dropped on another track of the same kind, the clip goes
  there as its own copy. Ctrl places another and keeps the first placement.
  Escape puts it back.
- **Reorder tracks:** press on a track's header in the clip grid, or its name
  in the arrangement grid, and move: a copy of the header follows the pointer
  and the one it would take the place of is marked with an arrow to the side
  the track lands on. Release to move it there (one undo step; the tracks
  between shift one place, and the track numbers, which follow position, with
  them). A track moves only among those in its own group, or at the top
  level. It stays the current track. Escape puts it back.

## Moving a paused playhead

Once anything has played, a clip the arrangement has at the transport's row has
a playhead in Live View, paused or not. With the transport paused, the pattern
editor's cursor in such a clip is that playhead: Up and Down move it, and so
move the arrangement's position (reaching the clip's first row does not detach
it), and playing resumes from there. A launched clip's playhead moves the same
way, taking the other launched clips along. A song that has never played has no
clip playing, so there the cursor moves alone.

## Undo

`undo` (C-x u, C-_ or C-/) takes back the last edit to the song; repeating it
walks further back. `undo-redo` (C-M-_) puts back what the last undo took away,
and works only while the newest thing done is a run of undos. Any other edit
ends that run, and the next `undo` then undoes that edit (Emacs's own rule,
where an undo is itself an edit that can be undone). Moving the cursor or
scrolling is not an edit and does not end the run. Both are in the Edit menu
and M-x, and are the Launchpad's shift + Record Arm and shift + Mute.
A terminal sends C-_ and C-/ as the same single control byte (0x1f), and
Ctrl+Shift+- is how C-_ is typed in the Kitty protocol; all of them are bound.

Only the song is undone: tempo, notes, clips and placements, tracks,
instruments and bus effects. The view and the clipboard are not. The current
track moves to the track the change was on, the cursor returns to the velocity,
delay, command or note cell that changed, and the transport row moves to the row
of an arrangement note or placement (never while the song plays). A change to a clip's notes shows that clip: the
clip grid's cursor goes to its scene and, in Live View, the pattern editor to
the changed row (unless that track is playing).
Changes that follow the playing song (a scene launch setting the tempo) are
never undone; so are mute and solo, which are performance state rather than edits. A velocity or delay is one integer, not a string of characters as
in Renoise, so it is atomic: its hex digits are held in the cell (shown, but not
in the song, so playback never hears half a value) and committed together when
the cursor leaves the cell, the pattern editor loses focus or any other key is
pressed; Enter commits it and moves to the next column. A delay needs a note (or
aftertouch) to belong to, so typing one on an empty column is refused; a velocity
there makes an aftertouch. An OFF row shows its release velocity (its *lift*) in the velocity
column and edits it the same way; velocity 00 on a note-on is a real note that starts from silence, never an OFF. `?` in the note column enters a lost note, `???`: a note whose pitch is unknown; it makes no sound but ends the previous note in its column. A lost note and a lacuna show `--` in the velocity and delay columns, which refuse a typed value. `~` enters a lacuna, `~~~`: a span of missing notes, which makes no sound and ends the previous note in the same way.
A newly entered note-on gets velocity 200 (shown `c8`), a newly entered OFF the neutral 128 (shown `80`),
Launchpads included. A command is text and is written as it is typed. Typed digits are amalgamated as Emacs does with typed characters: the hex
digits of a velocity or delay, the characters of a command, and typed notes
(a chord counts as one, an off or a delete as one), run on from one cell to the
next, undo as one step. The run ends when you move the cursor or press any
other command, edit something else, pause for two seconds, or reach 20 edits.

The keys of a chord are one step, however long they are held (on a terminal
that reports key releases; elsewhere, keys typed within about 0.1 s of each
other). Held keys behave alike in both views: the cursor
stays on the row and steps once when the last key lifts; nothing starts the
transport. A live recording take is one step: its notes appear as they land, and
`undo` takes the whole take back once it has ended. Undo and redo do nothing
while a take is still recording, and they leave a clip that is playing (or
queued, or being recorded) alone: an undo that would change its notes says so
and waits until the clip is stopped.

## The cursor's height and the edit step

The pattern editor's cursor is a rectangle. Its height is the **edit step**:
the number of rows the cursor moves down after an entry. They are one value,
so changing the edit step changes the height at once.

| Shortcut (both keymaps) | Command | Action |
|---|---|---|
| Ctrl-+ | `edit-step-increase` | One row higher |
| Ctrl-- | `edit-step-decrease` | One row lower |
| Ctrl-1 ... Ctrl-9 | `set-edit-step-1` ... `set-edit-step-9` | That many rows |

Each of them shows the new edit step on the status line.

- The cursor's position is its first row. Notes, chords and every command
  that acts at the cursor act there. The cursor's own mark, the lighter
  colour over its columns on a terminal and the border round them in a
  window, reaches down over all its rows; the cell being edited is marked in
  the first row only. Nothing is entered or played in the rows below the
  first: the next entry goes in the row just below the last of them.
- The height is at least 1 and at most one bar at the cursor (16 rows in 4/4).
  There is no edit step of 0 any more: `set-edit-step-0` and Ctrl-0 are gone, and
  the cursor always moves on after an entry. Where the bar is shorter than the
  cursor (after a change of time signature, or in a scene or a part of the
  arrangement with shorter bars), the height is reduced to the bar and stays so.
- At the end of a clip the cursor is drawn only over the rows that exist.
  The view scrolls to keep the first row in sight, and the whole cursor where
  it fits.
- **Delete and Backspace** empty everything under the cursor: every row and
  every column it covers. In a note column those are the notes (with their
  velocity, delay and fx), in a note's fx column the fx of those notes, in the
  effect column the commands of those rows. Delete leaves the cursor where
  it is; Backspace then moves it up by its height.
- While the cursor is a playhead (the transport running in Arrangement view,
  or a playing clip in Live View) only its first row is shown.

## Chord entry

The pattern editor's cursor is 1 to 4 note columns wide. `widen-cursor`
extends it to the right and `narrow-cursor` takes columns off its right end;
the left edge stays. It lies within one track, and keeps its width
as it moves and after an entry, until you change it. The width has nothing to
do with the mark: neither changes the other. With no mark, the region is the
cursor, so cut, copy and transpose take all the columns it covers.

**Note columns follow the notes.** Widening the cursor past a track's last
note column adds one, and so does a chord, a paste or a recording that needs
more; a track has at most 8. Narrowing removes none, and a track never loses
a column while the cursor is in it. The empty columns at the right end of a
track go when the cursor leaves the track and when the song is saved (an
empty column with a used one to its right stays). `add-note-column` and
`remove-note-column` are in the command palette and have no shortcut.

**Completion.** In a song whose scale is otonal or utonal, a cursor of N
columns (N above 1) completes the notes you play to a chord of N notes of the
scale. The lowest note played is the degree. Its chords
([extended-diamond.md](extended-diamond.md)) are tried in order: the seventh
chords, a stand-alone triad, then the major sevenths and the 7♯9 chords. The
first that contains every other note played, octaves aside, is written: the
notes played keep their octaves, and the chord's other notes are added from
its lowest upward, within the octave above the lowest note played, until
there are N. A chord of fewer notes than N is written whole and not padded.
Nothing is ever added below the lowest note played, so E with A♯↑ at four
columns gives E G A♯↑ C.

The chord is written in rising order from the cursor's first column, on one
row, the added notes with the velocity and delay of the played ones; every
column under the cursor is rewritten, and one the chord does not use is
emptied. The status line shows its name: "C harmonic seventh", or "F harmonic
seventh / D♯" for a chord read upward from another of its notes. Three notes
that make a major, minor, narrow major, narrow minor, septimal diminished or
mirror diminished triad on the lowest take the triad's name ("C major");
other chords written short say how many notes they have. One entry is one
undo step.

A played note is written and sounds as its key goes down. The completion
comes when no further key has been pressed for about 0.04 s, or when the keys
are all released, whichever is first. The added notes are written together
and sound one after another from the lowest, the same 0.04 s apart: with C
held, E follows 0.04 s after the key press and G 0.04 s after E. After a
quick tap, released before the 0.04 s have passed, the time the key was down
is the time between the notes, and the chord rings on for a moment. Keys
released while notes are still to come end the chord there. Another key
pressed while keys are held works the chord out again from the held notes.
The cursor steps on when the keys are released. On a terminal that reports
no key releases the completion comes when the 0.04 s have passed, and the
cursor steps on then.

Nothing is added when the cursor is one column wide, when you play as many
notes as the cursor is wide or more (all are written, the track gaining
columns if it needs them), when the lowest note is not a note of the scale,
and when no chord of the degree contains all the notes, which the status line
says. Other scales have no chords: the cursor widens all the same, and notes
played together are written side by side as they are played.

## Chord palette

`chord-palette` (M-/, or Ctrl+Space on the desktop keymap) opens the chord
palette, a list of a degree's chords to choose from, across the bottom rows
of the pattern editor like the command palette. It works like a candidate
window in Japanese text input: each move writes the chord it lands on.

- **What it acts on.** If the last thing you did was enter notes, and nothing
  has been edited or moved since, it acts on that entry, although the cursor
  has stepped on: the given notes are the notes you played, not the ones a
  completion added. Otherwise it acts at the cursor, and the note in the
  cursor's first column is the one given note. To change a chord entered
  earlier, put the cursor on its lowest note and open the palette. With no
  note there, a note that is not a note of the scale, or a song whose scale
  is not otonal or utonal, the palette does not open and the status line says
  why.
- **Size.** The chords of the palette have 2 to 4 notes. The size starts at
  the cursor's width; with a cursor of one column, at the size last used
  there, 4 the first time. So with a narrow cursor you can play one or more
  notes and open the palette to complete them. While it is open,
  `widen-cursor` and `narrow-cursor` change the size and leave the cursor's
  width alone.
- **Lines.** One line per completion of the given notes at that size, found
  as for [chord entry](#chord-entry) and in the same order, no chord twice:
  its name, then its notes.
- **Opening** selects the chord now in the pattern, if it is in the list, and
  plays it. If it is not (two notes played with a narrow cursor, say), the
  first chord is selected and written.
- **Up and Down** move the selection, round the list. Each move writes the
  selected chord in place of the last and plays it on the track's instrument,
  tuned as it sounds in the song, for a moment or until the selection moves.
  A chord of fewer notes empties the columns it leaves.
- **Enter** accepts; so do the palette's own key, any other key (which then
  does what it always does) and a click. `keyboard-quit` (C-g, or Escape on
  the desktop keymap) cancels and puts back the notes that were there when
  the palette opened.
- One session is one undo step, however many chords were tried.

## Just intonation

`apply-just-intonation-region` (M-x or the Edit menu, no key binding) gives
every pitched note in the region a tuning correction: the cents that move it
from its equal-tempered pitch to a just ratio (`model/JustIntonation.h`). The
correction goes in the note's fx as `+hh` or `-hh` (commands.md), and `+00`
marks a note as tuned that needs none.

The ratios come from the song's scale. Every scale names a gamut (the glossary
defines the word) and selecting the scale selects the gamut; the song stores
nothing else:

| Scale | Gamut |
| --- | --- |
| major, minor | duodene (Ellis's 12 notes: 1/1 16/15 9/8 6/5 5/4 4/3 45/32 3/2 8/5 5/3 9/5 15/8) |
| otonal, utonal | the extended diamond (33 ratios; [extended-diamond.md](extended-diamond.md)) |
| the seven Babylonian modes (nīš tuḫrim, išartum, embūbum, nīd qablim, qablītum, kitmum, pītum) and Phrygian tonos | Pythagorean (53-EDO only) |
| none, the maqamat | no gamut: tempered, no corrections |

Each key, a step above the song's key, gets the gamut note whose nearest step
in the song's tuning is that key. Where several notes have the same key (53-EDO's keys 2, 5, 48 and 51, which carry one ratio of each scale; 12, 19 and 31-EDO's several), the one in the song's scale has it,
else the one closer to the key in cents. A key no gamut note is nearest to
stays tempered (`+00`). The rule looks at the key and the note alone, never at
the chord, so a note sounds the same whatever else is playing, and it sounds
that way as it is played too - pads, MIDI and keyboard notes - and a recorded
or typed note is written with its correction.

The price is that some triads are a comma off. The duodene has one note for
each of its twelve keys, so in C major the D is 9/8, the pure fifth G-D and
fourth, but D-A is 40/27, 81/80 flat of 3/2, and F-B♭ in minor is 27/20. Any
fixed twelve-note just scale leaves a comma somewhere; B♭ is 9/5 rather than
16/9 so that in minor the v and ♭III triads come out pure. (Adaptive just
intonation, planned, would move the whole scale by a comma at a switch; that
is a different thing from tuning a chord against its own bass, which is not
done at all.)

Percussion, offs and aftertouch are skipped; any other fx a note had is
replaced, and the message says how many. In a song with no scale, or a scale
with no gamut (the maqamat), the command writes nothing and says so. A run
replaces earlier corrections, so it can be repeated after editing; the mark
stays.

Select all first (C-x h) to tune every clip (Live View) or the arrangement
(Arrangement view); a clip's notes are tuned against the key, so one tuning
fits every place it is put. `clear-tuning-correction-region`
removes the corrections again, whatever put them there. Transposing the region
gives each tuned note the correction for its new pitch, and transposing the
whole song moves the key with the notes when no pitched note is left outside
the selection, so the corrections stay as they were; otherwise the key stays
and the notes are retuned against it. Changing the song's scale
(`set-song-scale`) retunes every tuned note of the song for the new scale, in
the same undo step, and removes the corrections of tuned notes when the new
scale has no gamut; untuned notes stay untuned. Corrections already in a song
are not migrated: it sounds as it did until retuned.
Changing the tuning (`set-song-tuning`, Song menu) converts the song by
pitch: the key moves to the new tuning's nearest step, every other pitched
note to the step nearest its interval to the key (a tuning correction
counted; a tie takes the lower step), and a tuned note keeps what is left
over as its correction, so a tuned chord stays pure and converting there
and back gives the same notes. Percussion is untouched; one undo step.
Notes yanked into a song in another tuning, in the pattern editor or as a
clip, are converted the same way, counted from C rather than the key (C stays
C), and the status line says so. A drum note yanked onto a pitched track, or
the other way round, is refused.
`undo` takes any of these back. Not on the Launchpad.

## Humanize

`humanize-region` (M-x or the Edit menu, no key binding) loosens the region's
notes: each sounding note's velocity moves by up to ±12 and its delay later by
up to 32/255 of a row. Offs and aftertouch are left alone, percussion is
included, and the mark stays, so repeated runs keep adding variation. Every
run draws new values; `undo` takes a run back. Not on the Launchpad.

## Other clip commands

Available from M-x and the Clip menu: `launch-clip`, `launch-scene`,
`stop-all-clips`, `back-to-arrangement`, `track-back-to-arrangement`,
`quantize-clip`, `toggle-stop-button`, `copy-to-clip` (new clip from the
selection) and `merge-clip-to-background`.

## Commands that only a controller uses

A command that only a pad controller dispatches, and that nothing else
registers, is named with a `pad-` prefix (`pad-next-track`, `pad-prev-track`).
A pad gesture whose keyboard equivalent is copy, kill or yank gets no command
of its own: the device calls the underlying function directly. That is why the
Launchpad's delete never touches the clipboard.
See `launchpad.md` for the gestures.

### Note keyboard

The laptop/PC keyboard plays notes on the lower row `z x c v b n m , . -` and
the rows above it. In an otonal or utonal song all eighteen notes of the scale
are on keys, with the same layout shape in both (spellings as in
[extended-diamond.md](extended-diamond.md)). The keys are staggered, so a key
on the home row sits between two keys of the lower row, and the note on it
lies between theirs:

| Key | `z` | `q` | `w` | `s` | `x` | `d` | `c` | `f` | `v` | `t` | `g` | `y` | `b` | `h` | `n` | `j` | `m` | `k` | `o` | `p` | `,` | `.` | `-` |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| otonal | C | D𝄫 | D♭↓ | D | D♯ | D𝄪 | E | F♭ | F | | F♯↑ | F𝄪 | G | G𝄪 | A | | A♯↑ | B♭ | B | B♯ | C′ | D♯′ | E′ |
| utonal | C | D𝄫 | D♭↓ | D↓ | E𝄫 | | E♭ | F𝄫 | F | G𝄫 | G♭↓ | | G | G♯ | A♭ | A♭↑ | B𝄫 | B♭↓ | B | B♯ | C′ | E𝄫′ | E♭′ |

The seven core notes (the 7-note step of the scale) are on the lower row, the
notes between them on the home row (the tritone on `g`, between F and G), and
the notes that cluster round the tonic and the tritone on the upper row. The
`[` and `]` keys are octave down and up, so B♯ is on `p`. A song with any
other scale (or none) keeps its own layouts, which do not follow the scale;
the Babylonian modes and the maqām Rāst have theirs. The Launchpad's step view
steps through all eighteen notes of the scale.

## Terminal

### Limitations

The terminal interface does everything the program does, with these limits:

- **Key releases.** Only terminals with the Kitty keyboard protocol (kitty,
  foot, WezTerm, Ghostty) report a key going up. Elsewhere keyboard note entry
  can't tell how long a note is held. Keys typed within about 0.1 s of the first
  key of a chord still go on one row, in successive note columns (a note
  already in the chord starts a new one, so a held key's repeats each get a
  row); the Launchpad and MIDI input are unaffected.
- **Key combinations.** Without that protocol many combinations arrive as the
  same byte (C-_ and C-/, for example) or not at all (most Ctrl+Shift shortcuts),
  so some commands have fewer bindings there, and M-x is always the way in.
- **Pixel graphics.** Terminals with Kitty graphics or Sixels draw the spectrum
  analyzer and the DirAC heatmap as real pixels; the rest get braille cells.
- **Color.** A terminal without true color (xterm) is limited to the
  256-color palette.
- **Motion.** Everything moves in whole cells: scrolling, playheads and fades
  step rather than glide.

### Supported terminals

Known to work are kitty, xterm and GNOME Terminal; the rest are untested here.
The Graphics and Mouse columns come from testing another program (nanoclj) in
these terminals; xterm's lack of true color applies to synth as well.

| Terminal | Status | Graphics | Mouse | Notes |
| - | - | - | - | - |
| foot | Untested | OK | OK | Wayland only |
| kitty | Works | OK | ? | True color images, but window resizing has bugs (as of 0.26.5) |
| wezterm | Untested | OK | OK | Buggy (as of 20230712) |
| mlterm | Untested | OK | ? | |
| Konsole | Untested | OK | ? | True color images, but on HiDPI system images are upscaled |
| contour | Untested | Inline image layout doesn't work | ? | |
| xterm | Works | True color where built with it and `directColor` is on (found by asking the terminal); else the 256-color palette | OK | Sixel support must be enabled in `.Xresources` (see [xterm](#xterm) below), and images have maximum size 1000x1000 |
| Black Box | Untested | Inline image layout doesn't work | ? | On HiDPI system the images are upscaled, and the terminal and the flatpak system use too much CPU time when idling. |
| Alacritty | Untested | None | ? | |
| GNOME Terminal | Works | None | ? | Sixel support is not enabled by default |
| mintty | Untested | ? | ? | Not tested yet. |

### Setup

#### xterm

xterm's defaults are poor for this UI: 16 colors, no Sixels, a small image
limit, a tiny bitmap font. Put this in `~/.Xresources`:

```
XTerm*termName: xterm-256color
XTerm*decTerminalID: vt340
XTerm*numColorRegisters: 256
XTerm*maxGraphicSize: 2048x2048
XTerm*renderFont: true
XTerm*faceName: monospace
XTerm*faceSize: 12
```

- `termName` sets `TERM`; xterm's default `xterm` has only 8/16 colors, which
  is why the UI looked 16-color everywhere but the Sixel images.
- `decTerminalID: vt340` makes xterm identify as Sixel-capable, and
  `numColorRegisters` sets the image palette size.
- `maxGraphicSize` raises the Sixel image size limit (default 1000x1000).
- `renderFont` with `faceName` selects an antialiased Xft font through
  fontconfig. `monospace` is fontconfig's alias for the system's default
  monospace font, so it is the same on every machine; name a font such as
  `DejaVu Sans Mono` to pick one. `faceSize` is in points.

xterm shows 24-bit color only when it is built with direct-color support and
`XTerm*directColor: true` (the default in some builds). The UI asks the
terminal at start-up by setting a color and reading it back; where xterm
reports it kept, the UI uses true color, otherwise it is limited to the
256-color palette. In that case xterm snaps the colors of text to its palette
while Sixel pictures keep their own, so a picture's flat background can differ
slightly in shade from the cells beside it.

Reload with `xrdb -merge ~/.Xresources` and start a new xterm; running ones
keep their old settings. If `TERM` is still `xterm` inside it, check with
`echo $TERM`; a shell profile that exports `TERM` overrides the resource.

## Tempo and time signature changes

In Arrangement view the Song menu (and M-x) has `set-signature-change` and
`set-tempo-change`, which ask for a signature or a tempo from the bar under the
arrangement grid's cursor, and `clear-arrangement-changes`, which removes both
there. The changes show in the grid's header at their bar (`3/4`, `♩90`) and are
undone like any edit. `set-time-signature` still sets the song's starting
signature. See time_signatures.md.
