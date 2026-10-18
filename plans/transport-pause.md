# Transport pause: what survives it

The transport toggle only pauses and resumes: launched clips keep their
clip and row, queued changes and taken-over tracks stay, and play resumes
them where they were (`Player`'s `STOP` no longer silences the session).
That's an interim choice - the live-sequencer convention stops every
session clip with the transport instead - and one thing around it is
unresolved.

## Open questions

- **Sustained voices.** A pause releases sample-track voices but leaves an
  instrument's voices as they are: a held note rings on through the pause,
  and whatever starts on resume layers over it. Releasing every voice on a
  pause breaks what relies on them ringing - a demoted buffer's held notes, a song end's tail in an offline render -
  so the answer likely distinguishes a pause from those.

It is not started; it needs a decision on the design first.
