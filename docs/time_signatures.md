# Time signatures and bars

## 1. Rows, beats and bars

A row is a sixteenth note. A time signature n/d is stored as its numerator
and denominator and gives whole rows:

- rows per bar = n x 16 / d
- rows per beat = 16 / d

The denominator is therefore 1, 2, 4, 8 or 16, and the numerator at most 32.
Anything else (`2/3`, `100/4`) is refused wherever a signature is typed.

| Signature | Rows per bar | Rows per beat |
|---|---|---|
| 2/4 | 8 | 4 |
| 3/4 | 12 | 4 |
| 4/4 | 16 | 4 |
| 5/4 | 20 | 4 |
| 6/8 | 12 | 2 |
| 7/8 | 14 | 2 |
| 12/8 | 24 | 2 |

3/4 and 6/8 are both 12 rows and differ in the beat only: the metronome and
the beat accents fall every 4 rows in 3/4 and every 2 in 6/8. A beat is
always a plain division of the bar; there is no grouping such as 3+2 or
2+2+3.

Songs are 4/4 unless they say otherwise. A song file that only gives a bar
length (`rowsPerBar="12"`, the older way) loads as the signature of that many
rows (12 is 3/4) from the first row; it is written back as a marker.

## 2. The arrangement's bars: time signature markers

The arrangement is counted in bars of 4/4 until a **marker** sets another
signature from its row on. Markers are keyed by row and saved in the song:

```xml
<timeSignatures>
  <timeSignature row="0" value="3/4"/>
  <timeSignature row="96" value="5/4"/>
</timeSignatures>
```

A marker at row 0 sets the first signature. Nothing is written for a song
that is plain 4/4.

**Setting one.** `set-time-signature` (also in the Song menu as *Set Time
Signature Here...*) asks for a signature at the current bar. The bar is the
arrangement grid's cursor bar while that grid has focus, otherwise the bar
the transport is in; the prompt shows its number and is filled with the
signature in force, so Enter alone changes nothing. Typing `0/4` removes the
marker at that bar. A marker always sits on a bar start of the signature
before it, because it is placed at a bar.

**Where it shows.**
- In the arrangement grid, the column at the right edge shows `/` on a bar
  that has a marker (`»` if it also has a locator).
- The bar and beat accents of the pattern editor in Arrangement view follow
  the markers.
- The info line shows the signature at the transport's position next to the
  tempo.
- The transport position (bar.beat.sixteenth) counts bars through the
  markers: with 3/4 from row 32, row 44 is bar 4.

**What follows the markers.** Everything laid out on the arrangement
timeline: the arrangement grid (one row per bar, so bars of different
lengths appear as equal rows), where clips are placed and stops land, how an
arrangement recording is rounded to bars, copy-to-clip rounding in
Arrangement view, and the arrangement's length. A marker that is not on a bar
start of the signature before it cuts the bar before it short.

Markers move no content: arrangement content is positioned by absolute row,
so adding, moving or removing a marker changes how rows are grouped into
bars, never when anything sounds.

## 3. The transport's bars

The transport has its own idea of the bar, used for everything that happens
"on the next bar": queued Session launches, stops and returns to the
arrangement, the ZBxx pattern break (which jumps to a row of the next bar),
the metronome, and the quantization and length of Session takes.

By default these are the arrangement's. Launching a scene that has a time
signature sets the **running signature**: the transport counts bars of that
signature from the launch bar (its *origin* row) onward, ignoring the
arrangement's markers while it is active. Rows before the origin keep the
arrangement's bars, so the launch bar itself is a bar start of both.

The running signature ends when:
- another scene with a time signature launches, which replaces it; or
- Back to Arrangement is used for every track (`back-to-arrangement`), which
  hands the bars back to the arrangement's markers on the next bar, or at
  once from a stopped transport.

It stays through stopping the scene's clips, stopping the transport and
single-clip launches. It is **saved** with the song, with its origin:

```xml
<song ... transportTimeSignature="3/4" transportBarOrigin="112">
```

and is active again when the song is loaded.

A scene without a signature leaves the running one alone. Such a scene is
shown and edited in the running signature, which is also what it plays in.

## 4. Which bars each part uses

| Uses the arrangement's bars (markers) | Uses the transport's bars (running signature, else markers) |
|---|---|
| Arrangement grid and its bar numbers | Applying queued Session changes on the bar |
| Arrangement-view row accents | Pattern break (ZBxx) |
| Placing clips and stops into the arrangement | Metronome click and accent |
| Recording into the arrangement | Session take quantization and length |
| Rounding for copy-to-clip in Arrangement view | Transport position display |
| Arrangement length | Info line signature |

Session view's pattern editor accents come from the scene itself: its own
signature, else the running one.

## 5. Timing

Two things happen when a scene launches, and they are timed differently.

- **The clips** are queued to the audio thread, which starts them on the bar
  line itself, sample-exact. The press only has to arrive before the bar.
  A press from the terminal and from a Launchpad take exactly the same path
  (`SessionPlayer::launchScene()`), so they behave identically, and the delay
  between a press and the UI noticing it makes no difference as long as the
  press is before the bar.
- **The tempo and the running signature** are applied by the UI side, which
  sees each bar line once per frame (about 16 ms), so they take effect up to
  one frame after the bar line the clips started on. The new tempo is picked
  up by the audio thread within a block after that. In practice the first row
  of a scene can play a few milliseconds at the old tempo.
