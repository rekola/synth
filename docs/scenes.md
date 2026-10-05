# Scenes: name, tempo and time signature

This describes what a scene carries, how it is typed in, and exactly what
happens when one launches. Section 8 lists the decisions still open.

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
nothing set are not written. Inserting or deleting a clip row would shift the
records, exactly as it shifts the clips.

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

- The bar and beat accents the pattern editor draws in **Session view**:
  each scene shows its own, whatever is running.
- The length of a clip created by editing an empty slot: one bar of that
  scene's signature (the song's if the scene has none).

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

## 7. Not done

- Per-scene swing (swing is song-wide; Live's groove is per clip, so it is
  left alone for now).
- Anything per scene beyond name, tempo and signature.
- Tempo or time-signature changes inside the arrangement.
- Scene tempo and signature on the Launchpad; they are set from the
  terminal.
- Compound or irregular beat grouping (6/8 as two dotted beats, 7/8 as 2+2+3).

## 8. Open questions

1. **Should a scene's tempo and signature revert when its scene stops?**
   Built: no, they stay until changed. The alternative is to restore the
   previous tempo and bars when no track is playing a scene any more (all
   stopped, or all returned to the arrangement). That needs a "tempo before
   the first scene" to be remembered, and a rule for two scenes with
   different tempos playing on different tracks.
2. **Should the tempo be saved?** Built: yes, as the song tempo, because
   launching a scene is an ordinary tempo edit. An alternative is to keep a
   scene-set tempo runtime-only like the signature, so the saved song tempo
   is only ever one you set deliberately.
3. **Should the arrangement get its own time signature?** Tempo and
   signature changes in the arrangement itself (markers on the timeline)
   would let a section be written in 3/4 and played back that way without a
   scene. That is a larger change to bar numbering across the arrangement
   grid.
4. **Scenes without a signature:** the Session view draws them with the
   song's bars even while a waltz is running, because the pattern editor shows
   the scene's own signature, not the running one. Showing the running one
   would make the accents match what the metronome plays.
5. **Timing precision.** The change applies on the bar row the UI thread
   notices, so it can land up to a frame after the audio thread passed the
   bar. For tempo that is inaudible; for the signature it means one frame
   of bars counted the old way. Moving it to the audio thread (as queued
   clip launches are) would make it sample-exact.
6. **Clip rows inserted or deleted** shift scene records along with the
   clips, since both are by position. Check this matches what you expect
   when a row is removed.
