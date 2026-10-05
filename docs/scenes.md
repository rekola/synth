# Scenes: name, tempo and time signature

This describes what a scene carries, how it is typed in, and exactly what
happens when one launches. 
## 1. What a scene is

A scene is a row of the clip grid: clip number *k* of every track's clip
list. There is no scene object holding clips; the row exists because the
tracks' lists line up by position. What a scene does own is a small record
stored **by position** (scene 0, 1, 2, ...), like a track's clip list:

| Field | Meaning | Empty value |
|---|---|---|
| name | Free text shown in the Master column. Has no effect on playback. | `""` |
| tempo | bpm, 20-300 | 0 (none) |
| time signature | numerator / denominator | numerator 0 (none) |

Saved as `<scenes><scene name="Waltz" tempo="90" timeSignature="3/4"/>...</scenes>`,
one `<scene>` per position in order, no index attribute. Trailing scenes with
nothing set are not written. Clip rows are never inserted or removed:
deleting a clip leaves an empty slot, so scene positions stay aligned with
their records. A scene keeps its name, tempo and signature even when every
slot in it is empty.

## 2. Typing it in

In the clip grid, put the cursor on a scene slot in the **Master** column
and press **F2**. The slot shows `▸ name ♩tempo n/d` (tempo and signature at
the right; a long name is cut to fit).

The text you type is split by `scenename::extract()` (`src/model/SceneName.h`):

| Typed | Effect |
|---|---|
| `Waltz 90 BPM` | name `Waltz`, tempo 90 |
| `90bpm`, `90 bpm` | tempo 90 (any case, space optional) |
| `3/4`, `6 / 8` | time signature (spaces around `/` allowed) |
| `Waltz 3/4 90 BPM` | all three |
| `0 BPM` or `- BPM` | clears the tempo |
| `0/4` | clears the time signature |
| `xyz` (no tempo or signature in it) | renames only; tempo and signature are kept |

The tempo and signature are removed from the name, so they cannot go out of
step with the text. Text that does not read as one stays in the name: a
tempo outside 20-300, more than 3 digits, a signature whose denominator is
not 1, 2, 4, 8 or 16, or a numerator above 32 (`2/3`, `100/4`).

Time signatures are limited that way because a row is a sixteenth note: a
signature needs a whole number of rows per beat. Rows per bar =
numerator x 16 / denominator, rows per beat = 16 / denominator.

| Signature | Rows per bar | Rows per beat |
|---|---|---|
| 4/4 | 16 | 4 |
| 3/4 | 12 | 4 |
| 6/8 | 12 | 2 |
| 7/8 | 14 | 2 |
| 5/4 | 20 | 4 |

6/8 and 3/4 are both 12 rows; they differ only in the beat (2 rows against
4), so in the metronome and the beat accents. Grouping 6/8 as two dotted
beats is not represented.

## 3. When a scene launches

A scene launches from the Master column slot (Enter or click), the
`launch-scene` command, or a Launchpad scene button. The tracks' clips are
queued for the next bar as always. Then:

- **Tempo.** If the scene has one, it becomes the **song tempo**, the same as
  using the tempo commands: it is stored in the song and saved with it.
- **Time signature.** If the scene has one, it becomes the **transport's
  bars** (section 4). This is not saved.
- **Timing.** With the transport playing, both take effect on the bar the
  clips launch on (the first bar row `SessionPlayer::tick()` sees after the
  press, so up to one UI frame late). With the transport stopped, they apply
  immediately, and the transport's bars are counted from where it stands.
- A scene with no tempo or no signature leaves the running one **unchanged**.
- Launching a scene replaces any tempo or signature still waiting from an
  earlier launch.
- Launching a single clip never touches the tempo or signature.

## 4. The transport's bars

The song stores `rowsPerBar` (default 16, i.e. 4/4). That is the **song's**
bar length and drives everything laid out on the arrangement timeline. A
launched scene does not change it. It sets a separate, **runtime-only** set
of three values on the song (`Song::setTransportBars()`):

- bar rows,
- beat rows,
- an **origin row**: the transport row of the launch bar, from which bars are
  counted.

The origin is what keeps bars aligned. If a 3/4 scene launches at row 112,
bars run 112, 124, 136, ... rather than staying on multiples of 12. Rows
before the origin keep counting backward in the same bars. With nothing
launched (or after loading a song) bar rows are the song's `rowsPerBar`, a
beat is 4 rows, and the origin is 0.

`Song::isBarStart()`, `rowInBar()` and `barStartAtOrBefore()` answer "where is
this row in the bars the transport is counting".

### Read from the transport's bars

- The audio thread's test for the first row of a bar, which is when queued
  Session launches, stops and returns to the arrangement take effect.
- The ZBxx pattern break (jump to a row of the *next bar*).
- The metronome: a click each beat, accented on the bar.
- Session launch and record quantization (`SessionPlayer::quantizedStep()`,
  `rawStep()`, the bar a take starts on, and a take's length growing in whole
  bars).
- The length of a new Session take and of a sample take.

### Read from the scene's own signature

A scene's own signature, or the transport's running bars when it has none
(that is what it will play in):

- The bar and beat accents the pattern editor draws in **Session view**.
- The length of a clip created by editing an empty slot: one bar.

### Read from the song's `rowsPerBar` (unchanged)

- The arrangement grid, its bar numbers and the transport position display.
- Arrangement-view bar accents.
- Placing clips into the arrangement, and recording into it.
- Rounding a copied selection up to whole bars (`copy-to-clip`).

## 5. The arrangement and the transport

There is one transport and it is always running (or paused). Session view
does not start a second playback: it takes individual tracks over from the
arrangement, while the arrangement keeps advancing underneath and the
tracks not taken over keep following it. Tempo is global to the transport.
So nothing about "playing the arrangement" versus "playing a scene" changes
how tempo works; there is only the one tempo, and a scene launch is one of
the ways it gets set.

Consequences of what is built:

1. **A scene's values do not end when the scene stops.** Stopping the
   scene's clips, or sending every track back to the arrangement, leaves the
   tempo and the transport's bars as the scene set them. Only another scene
   with its own value, or a manual tempo edit, changes them.
2. **The arrangement has no tempo or signature of its own.** The song has one
   tempo, and an arrangement plays at whatever tempo is running. A tempo
   set by a scene is therefore also the tempo the arrangement plays at
   afterwards, and the one that is saved.
3. **The transport's bars can differ from the arrangement's grid.** After a
   3/4 scene, the metronome and the Session launch bars are 12 rows while
   the arrangement grid still shows 16-row bars. Arrangement content is
   positioned by absolute row, so its playback is unaffected; only the
   counting differs.
4. The transport's bars are not saved and are forgotten on load, so launching
   a waltz scene and saving cannot skew the arrangement grid. The tempo is
   saved (it is the song tempo).

## 6. How this compares with Ableton Live

From recollection, not verified here:

- Live scenes carry a tempo and a time signature as separate properties; the
  name field is a shortcut for entering them ("90 BPM", "3/4"), and renaming
  does not clear them. This matches section 2.
- Launching a scene sets the global tempo and time signature; launching a
  single clip does not.
- Live's arrangement has its own tempo automation and time-signature
  markers. Those are not built here (section 8).
- A scene without a tempo leaves the running one, as here.

## 7. The rhythm library

Each rhythm in the Library carries its time signature (Waltz and Jazz Waltz
3/4, Six-Eight 6/8, Five-Four 5/4, Seven-Eight 7/8, Slow Rock and Shuffle
Blues 12/8, March, Polka and Merengue 2/4, the rest 4/4), and its length is a
whole number of bars of it.

Add to Song puts the rhythm into a new clip at the end of the chosen track's
clip list, so it lands in the next free scene row. If that scene has no time
signature of its own, it gets the rhythm's. A scene that already has one
keeps it, so a signature you typed, or one the scene's other clips rely on,
is never overwritten. Nothing is set for the tempo, which a rhythm does not
carry. A swung rhythm sets the song's swing, as before.

Launching that scene then switches the transport to the rhythm's bars, so a
waltz scene followed by a rock scene goes 3/4, then 4/4, with no typing.

## 8. Design decisions

Where Ableton Live has a behaviour, this follows it. Live's behaviour here is
from recollection, not verified.

1. **Scene values stay after the scene stops.** Live's tempo and time
   signature are global controls; a scene launch sets them and nothing puts
   them back when the scene or its clips stop. The same here, including after
   sending every track back to the arrangement.
2. **The tempo is saved, the transport's bars are not.** Live saves the
   global tempo with the Set, and a scene-set tempo is saved here as the song
   tempo. Live also saves the time signature; here the transport's bars are
   runtime-only, because the arrangement's bar grid reads the song's
   `rowsPerBar` and there are no arrangement time-signature markers yet.
   Saving a scene-set signature would re-bar the arrangement. This is the one
   deliberate difference.
3. **A scene without a signature plays in, and is drawn in, the running
   bars.** Live scenes without a value leave the global one alone, and the
   grid follows the global one.
4. **A scene is its position.** Clip rows are never inserted or removed, so
   a record cannot drift from its row. An insert-scene or delete-scene
   command would have to move the records with the clips.
5. **Arrangement tempo and signature changes are not built.** Live puts them
   on the arrangement timeline (tempo automation, signature markers). Until
   that exists the arrangement plays at whatever tempo is running and counts
   bars in the song's `rowsPerBar`.
6. **Timing.** The tempo and signature apply on the first bar row the UI
   notices after the clips launch, so up to one UI frame (about 16 ms) after
   the audio thread passed the bar line. For the signature that is a frame of
   bars counted the old way. Live applies them with the launch itself;
   moving this onto the audio thread, as queued clip launches are, would do
   the same here.

## 9. Limitations

- Per-scene swing (swing is song-wide; Live's groove is per clip).
- Anything per scene beyond name, tempo and signature.
- Tempo or time-signature changes inside the arrangement.
- Scene tempo and signature on the Launchpad; they are set from the
  terminal.
- Beat grouping: 6/8 clicks on every eighth rather than as two beats of
  three, and 5/4 and 7/8 have plain beats rather than 3+2 or 2+2+3.
