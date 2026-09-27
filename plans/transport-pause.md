# Transport pause: what survives it

The transport toggle only pauses and resumes: launched clips keep their
clip and row, queued changes and taken-over tracks stay, and play resumes
them where they were (`Player`'s `STOP` no longer silences the session).
That's an interim choice - the live-sequencer convention stops every
session clip with the transport instead - and two things around it are
unresolved.

## Open questions

- **Where an arpeggiator resumes.** `ArpeggiatorState` keeps its own step
  position on a clock of its own, which keeps running while the transport
  is stopped, so after a pause it resumes wherever that clock has got to
  rather than where the song is. One direction: drive it from clips, so it
  is positioned by their rows like everything else. See also
  `plans/arpeggiator-timing-fixes.md`, which covers the same clock's other
  timing problems.
- **Sustained voices.** A pause releases sample-track voices but leaves an
  instrument's voices as they are: a held note rings on through the pause,
  and whatever starts on resume layers over it. Releasing every voice on a
  pause breaks what relies on them ringing - an arpeggiator's held chord,
  a demoted buffer's held notes, a song end's tail in an offline render -
  so the answer likely distinguishes a pause from those.

Neither is started; both need a decision on the design first.
