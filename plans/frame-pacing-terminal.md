# Frame pacing: terminal backend plan

Status: implemented (see "As built" at the end for what differs from the plan
and the harness numbers).

## Shared contract (implemented)

```cpp
// cellui::Backend
using FrameHandler = std::function<void(int64_t frame_time_us)>;   // on_frame
virtual void requestFrame() = 0;                  // idempotent, UI thread only
virtual void setFrameHandler(FrameHandler) = 0;
```

The app asks (`requestFrame()`); the backend decides when and calls the handler
with the *current* state's time (GTK: frame clock presentation time; terminal:
monotonic clock). Frames are coalesced, never queued. `InteractiveUI` no longer
has its own frame limiter or render timeouts.

## 1. Files and signatures

| File | Change |
|---|---|
| `term/FramePacer.{h,cpp}` (new) | Pacing state machine. No I/O; takes `now` as an argument so tests use a fake clock. |
| `term/OutputQueue.{h,cpp}` (new) | Non-blocking write queue. |
| `term/TerminalSession.{h,cpp}` | Owns queue and pacer, builds fences, routes replies, replaces the blocking `write()`. |
| `term/InputParser.{h,cpp}` | Longer grace for split replies; timestamp per event. |
| `term/TermEvent.h` | `TimePoint at` on events. |
| `term/ImageEncoder.{h,cpp}` | `q=1`, byte accounting, Kitty error replies. |
| `PollEventLoop.{h,cpp}` | `addWriteSource` (approved). |
| `FrameGate.h` | Stays for Win32 and `TestBackend`; the terminal stops using it. |
| `tools/ptylink.py` (new) | Bandwidth / latency / decode-speed pty harness. |
| `tests/FramePacerTests.cpp`, `tests/TerminalPacingTests.cpp` (new) | Unit tests and pty-harness tests. |

```cpp
// FramePacer.h (namespace cellui::term)
struct FrameBytes { size_t upload = 0, placement = 0, other = 0; size_t total() const; };
struct PacerStats {
  double bandwidth_bps, rtt_min_us, fps;        // B, RTT_min, frames started in the last second
  size_t bytes_in_flight, frames_in_flight;
  FrameBytes last_frame, totals;
  size_t acks, backoffs, stalls, kitty_errors;
  std::string last_kitty_error;
  bool dsr_supported;                            // false: fixed 4 fps cap
};
class FramePacer {
 public:
  using Clock = std::chrono::steady_clock;
  explicit FramePacer(bool dsr_supported);
  void requestFrame();                                   // dirty = true
  bool mayStart(Clock::time_point now) const;            // conditions (a)-(d) below
  std::optional<Clock::time_point> deadline() const;     // when mayStart() next becomes true by time alone
  void onSent(FrameBytes size, Clock::time_point t_sent);// last byte of frame+fence accepted by write()
  void onAck(Clock::time_point t_ack);                   // FIFO match
  void tick(Clock::time_point now);                      // 1.5 s no-ack stall rule
  PacerStats stats(Clock::time_point now) const;
  double estimatedBytesPerSec() const;
};

// TerminalSession additions
double estimatedBytesPerSec() const;
PacerStats pacerStats() const;
```

## 2. Frame lifecycle

1. The app calls `requestFrame()`, which sets `dirty`.
2. At the start of each loop turn the session's `loop_.onFrame` hook asks
   `pacer.mayStart(now)`.
3. If yes: clear `dirty`, call the handler with the monotonic µs.
4. The handler calls `present()`: encode against `FrameEncoder`'s baseline,
   append the fence, record sizes in `OutputQueue`.
5. The queue flushes on `POLLOUT`. `onSent` fires when the last byte of the
   frame, fence included, is accepted by `write()`.
6. Each `6n` reply calls `onAck`, which pops the oldest in-flight record.

**Invariant:** nothing is encoded while the queue is non-empty. The
`FrameEncoder` baseline therefore always means "everything before this has
been sent". A frame the app wanted while the link was busy is rendered later
from the then-current state.

`nextDeadline()` returns the earliest of the parser's deadline and the
pacer's; `InteractiveUI` already folds it into its poll timeout, so there is no
new timer API. `addWriteSource` makes `POLLOUT` part of the wait only while
the queue is non-empty, so an idle terminal wakes nothing.

Other writers (OSC 22 pointer shape, `setNativeSelection`, the cell-size query
on resize) append to the same queue, so ordering holds. They are not frames and
carry no fence; their bytes are counted in the next frame's in-flight total.

## 3. Non-blocking writes (item 1)

- `OutputQueue` holds a buffer and an offset. `flush()` calls `::write` until
  `EAGAIN`, then returns.
- It never blocks the main loop. On a serial line with XON/XOFF a long
  `POLLOUT` starvation is "not writable yet": no error, no timeout, no log.
- A write error other than `EAGAIN`/`EINTR` (EIO, EPIPE) means the terminal is
  gone: the session ends and emits `END`, as the read path does.
- Termination signals stay masked during each `write()` call, as today.
- **Shutdown:** `close()` drains the queue for a bounded time (about 500 ms),
  drops anything left, then writes the restore sequence. This is the one
  remaining blocking write, and it is bounded.
- **Known weakness:** `restoreFromSignal()` can land inside a half-written
  sequence. That is true today; partial writes make it more likely.

## 4. Fences and ack matching (items 2 and 3)

- **Fence bytes:** `ESC[<rows>;1H ESC[6n`, after the frame's
  synchronized-update end. The reply is then always `CSI r;c R` with `r >= 2`,
  which cannot be F3 with modifiers (`CSI 1;m R`). `forgetCursor()` is called
  afterwards. On a one-row terminal the plain form is used and only FIFO count
  matches.
- **Startup:** a bare `ESC[6n`, then the first frame, then its fence, all back
  to back. `RTT_min = t_ack0 - t_send0`. Initial
  `B = S1 / (t_ack1 - t_ack0)`, a lower bound. Until `ack1` arrives, at most one
  further frame is in flight.
- **Matching:** `deque<Fence>{bytes, t_sent, FrameBytes}`. The parser's
  existing `expectCursorReport()` counter is incremented once per fence, so the
  reply parses as `CURSOR_POSITION`. `pollTerm()` currently drops it; it will
  pop the oldest fence and call `pacer.onAck(event.at)`.
- **Timestamps:** `event.at` is the time of the `read()` that completed the
  sequence, not the time it is processed. Several acks in one read share a
  timestamp; zero-length gaps are skipped as bandwidth samples.
- **Parser, split CSI replies:** today a CSI with parameter bytes that idles
  25 ms is silently dropped by `settle()`, and the rest arrives as typed text.
  Fix: a CSI that already has parameter bytes uses the long grace (the existing
  1 s `string_grace_`); it can never be Alt+`[`, so nothing is lost. The same
  for `ESC _ G` while a Kitty error reply could arrive.
- **Decision / risk:** a bare `ESC` and a bare `ESC [` keep the 25 ms grace so
  the Escape key stays responsive. If an XOFF stall splits `ESC` from `[` of a
  reply, the reply becomes an Escape key plus stray text. The alternative is
  delaying every Escape while a frame is in flight.
- **Kitty:** every command becomes `q=1` (including the `a=d,q=2` in
  `enterModes()` and the restore sequence). Error replies arrive as
  `KITTY_GRAPHICS` with an `i=` id; they are mapped to the image key, counted in
  `kitty_errors`, and the last is kept as a string. Success produces no reply.
  Replies are not fences and don't affect pacing. `Discovery`'s startup `a=q`
  stays, since it needs the OK reply.
- **No DSR support:** `Discovery` already sends `ESC[6n` at startup. If no
  `CURSOR_POSITION` came back, `dsr_supported = false`: no fences, fixed 4 fps
  cap (250 ms gap). If a terminal answered at startup and later stops, the
  1.5 s rule below applies.

## 5. Pacing algorithm (item 4)

Send the next frame when all hold:

- (a) `dirty`
- (b) `now >= t_sent_prev + S_prev / B`
- (c) `bytes_in_flight <= max(1.5 * B * RTT_min, 2 frames)`
- (d) the output queue is empty

Plus a 16 ms floor between frames (about 60 fps). Without it `S/B` on a fast
link would allow hundreds of frames a second.

- **Bandwidth samples:** `B_sample = S_i / (t_ack_i - t_ack_{i-1})`, only when
  `t_sent_i <= t_ack_{i-1}` (the pipe was never idle between the two). Pairs
  with equal timestamps are skipped.
  - A sample above `B` pulls it up with weight 0.5.
  - A sample below pulls it down with weight 0.125. Ack spacing under-reads
    whenever we are app-limited, so one slow sample must not drag the estimate
    down. This is the stated EWMA made asymmetric, and the main knob to tune.
- **Probing:** every 8th frame, or after 20 consecutive clean acks, the gap in
  (b) uses `1.25 * B`. A higher back-to-back sample raises `B` through the rule
  above; otherwise nothing changes.
- **RTT:** `RTT_min` from the first probe. After about 10 acks it becomes the
  running minimum of `max(0, t_ack - t_sent - S/B)`.
- **Backoff:** `B *= 0.8` when `(t_ack - t_sent - S/B) > 2 * RTT_min` for two
  consecutive frames, or when ack spacing lags the send rate (the last four ack
  gaps average more than 1.25x the send gaps).
- **Stall:** no ack for 1.5 s with frames in flight: `B /= 2`, in-flight
  accounting resets, the fence deque is cleared, and the next frame starts with
  a fresh bare `6n`. The expected-report counter is reset too, so a late stray
  `R` is read as an ordinary key.
- **Real serial ttys:** `cfgetospeed()` is read once, as a sanity ceiling on
  `B` only.
- **Hook for later:** `PacerStats` / `estimatedBytesPerSec()` are where an
  adaptive-quality policy would read. Nothing consumes them yet.

## 6. Damage audit (item 5)

Read from the code; each is covered by a test below.

Correct as is:

- Unchanged Kitty images are not re-uploaded. Upload happens only for a fresh
  key or a changed `version`; `a=p` is re-sent only when rectangles or position
  change, or after a clear.
- A vanished widget gets `a=d,d=I`; shrinking placements get `a=d,d=i,p=`.
- An unchanged Sixel emits nothing.
- The frame path writes `ESC[2J` only on the first frame, a resize or
  `invalidate()`.
- `FrameEncoder` skips image cells, so no text is written across Sixel cells.

Gaps (only the first matters for pacing; none will be fixed here):

1. A changed Kitty image re-uploads its full raw RGB (`f=24`, base64, about
   1.33x) and deletes the old id. A chart changing every frame will dominate
   the "upload" bytes. Compression, PNG and shared memory are out of scope; a
   hook is left and the bytes are counted.
2. A changed Sixel resends the whole bounding box of its visible rectangles.
3. A cleared screen re-places every image but re-uploads none, assuming the
   terminal keeps image data across `ESC[2J`. kitty does; other terminals are
   unchecked.

Byte accounting: **upload** = Kitty `a=t` chunks, Sixel and iTerm2 data;
**placement** = `a=p`, `a=d` and their cursor moves; **other** = text, sync
wrappers, cursor, fences.

## 7. Stats (item 6)

`pacerStats()` returns B, `RTT_min`, bytes in flight, fps (frames started over
the last second), `last_frame` and cumulative byte splits, ack/backoff/stall
counts, and Kitty error count and last error. `estimatedBytesPerSec()` is on
`TerminalSession` only (not `Backend`).

## 8. Test plan (deliverable c)

`tools/ptylink.py` (stdlib only) opens a pty pair and relays with independent
knobs: `--bandwidth` (token bucket, app to terminal), `--latency-up` /
`--latency-down`, `--decode` (the emulated terminal's parse speed). The
emulated terminal answers `ESC[6n` once everything before it is consumed at the
decode rate, then waits the down latency. It can drop DSR replies or split them
into delayed pieces, and logs timestamped received bytes so tests count uploads
and placements.

Assertions:

1. High latency (20 MB/s, 400 ms each way): fps within about 15% of `B/S`, not
   `1/RTT`.
2. 100 KB/s cap: fps drops in proportion; `estimatedBytesPerSec()` converges to
   within about 25% of the cap in a couple of seconds.
3. Responsiveness: a key sent while frames are in flight is read within one
   loop turn; it never waits for acks or a full frame to flush.
4. Static screen with N Kitty images, then a one-cell text change: zero `a=t`
   and zero `a=p` bytes. Removing a widget: exactly one `a=d`. A static Sixel:
   nothing.
5. A DSR reply split in pieces 200 ms apart is matched; no stray text reaches
   the app.
6. A terminal that never answers settles at about 4 fps.
7. A 3 s freeze halves `B`, then recovers.
8. `FramePacer` unit tests with a fake clock: startup, back-to-back filter,
   backoff, stall.

## 9. Open points

- Pacing constants (probe interval, EWMA weights, 16 ms floor) are guesses;
  they will be tuned against the harness and reported.
- A DSR reply means "parsed", not "painted". Terminals that decode images on
  another thread will make `B` look higher than the screen can show.
- `addWriteSource` is on `PollEventLoop` only; `TestBackend` doesn't need it.
- The lone-Escape risk in section 4 needs confirmation.

## As built

Differences from the plan above:

- **Fence is `ESC[?6n`** (DECXCPR; reply `CSI ? r ; c R`), not the CUP form. The
  CUP + `ESC[6n` form is the fallback when `Discovery` saw `?6n` unanswered
  (`Capabilities::dec_cursor_report`). Discovery sends both queries at start-up.
- **The pacing gap runs from when the previous frame started**, not from when its
  last byte was accepted. On a full link the acceptance time already contains the
  transfer, so measuring from it counted S/B twice (first harness run: 8 fps on a
  link that carries 20).
- **Bandwidth estimate:** the largest of the last 8 back-to-back samples, each
  capped at 2x the current estimate; a lower window pulls the estimate down by
  1/8 of the way. This replaces the symmetric EWMA.
- **Probe frames (1.25x) are left out of the lag rule**, which uses 1.5x.
- **"2 frames" in the in-flight cap is `2 x S_prev` bytes.**
- **`delay_floor` (10 ms):** the queueing-delay rule uses
  `max(2 x RTT_min, 10 ms)`. With RTT_min near zero (a local terminal) every
  scheduling jitter counted as congestion and kept cutting B.
- **Late replies:** after a stall the replies for the fences given up on are
  counted and dropped when they arrive; the parser keeps expecting them.
- **Lone Escape repair:** while replies are expected, a settled `ESC` or `ESC [`
  followed within 1 s by `[ ? ...` / `? ...` is taken as the reply. The Escape key
  that was already emitted stays; the text does not reach the application.
- **Frames presented outside the pacer's turn** (a status message) are held (the
  latest wins) and sent when a frame may start; with no frame handler set,
  `present()` sends at once and nothing is paced, as before.
- **Settings:** all constants are `PacerConfig` (`SessionOptions::pacer`).

Final defaults (`PacerConfig`): `min_frame_interval` 16 ms, `in_flight_bdp` 1.5,
`min_in_flight_frames` 2 (x S_prev), `startup_frames_in_flight` 2,
`sample_window` 8, `sample_cap` 2.0, `down_weight` 0.125, `probe_every` 8,
`probe_gain` 1.25, `backoff_factor` 0.8, `delay_ratio` 2.0, `delay_floor` 10 ms,
`delay_frames` 2, `lag_ratio` 1.5, `lag_frames` 4, `rtt_refine_after` 10,
`stall` 1.5 s, `stalls_before_giving_up` 3, `fallback_fps` 4,
`min_bandwidth` 1000 B/s.

Harness (`lib/cellui/tools/pacing/run_assertions.py`, run by ctest as
`cellui_pacing_harness`), two consecutive runs:

| # | Check | Result |
|---|---|---|
| 1 | 520 kB/s, 400 ms each way, frames of 25.8 kB (B/S 20.2, 1/RTT 1.25) | 20.2 fps both runs; estimated B 589 kB/s and 524 kB/s |
| 2 | 100 kB/s cap, 10 ms | 4.0 fps (B/S 3.9); estimated B 104 kB/s; unshaped link 60 fps |
| 3 | keys while frames are in flight on the cap | 4 of 4 read, latency 10 ms (the link delay) |
| 4 | static kitty screen, 6 pictures | 6 uploads, 6 placements; a text change sent 0 picture commands; removing a picture sent 1 delete, 0 uploads, 0 placements |
| 4 | static Sixel screen, 6 pictures | 6 images drawn, none sent after they settled |
| 5 | reply split after `ESC [ ?`, 200 ms | 0 keys reached the application, 39 acks |
| 5 | reply split after `ESC` | 4 keys, all Escape (one per split), 39 acks |
| 6 | terminal that never answers | 0 fences, 3.99 fps |
| 7 | 3 s freeze at 300 kB/s | 1 stall; 11.0 fps before, 10.8-10.9 after; estimated B 240-297 kB/s |
| 8 | `FramePacer` and parser unit tests (fake clock) | in `cellui_tests` |

Not tuned: the probe interval and the weights were left at their first values
because every check passed with them; the estimate overshoots by up to 13%
for a while after a probe (589 kB/s against 520 kB/s in one run) before the
delay rule brings it back.
