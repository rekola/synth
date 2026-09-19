// Simulated Launchpad X exercising the track-picker overlay Stop Clip
// (CC49) opens: triggers pool index 7 for the fixture's only track (pad
// (0,0)), confirms it's playing, then enters Session's own mixer submode
// (CC95 pressed a second time - see LaunchpadManager.h's own GridMode
// comment; the seven mixer-submode buttons launch a whole scene instead
// while it's off) and presses CC49 to open the overlay and picks that
// same track's column in the picker row (the bottom row - pad (0,0)
// again, this time read as "column 0" rather than "clip index 7") to
// queue a stop, confirms the picker row itself (not CC49's own LED)
// reflects the stop once the quantized stop actually takes effect - the
// overlay deliberately stays open after a pick, so a second CC49 press is
// what finally closes it, checked last.
#include <alsa/asoundlib.h>
#include "fake_ready.h"
#include <stdio.h>
#include <unistd.h>

static void send_cc(snd_seq_t * seq, int port, int cc, int value) {
  snd_seq_event_t ev;
  snd_seq_ev_clear(&ev);
  snd_seq_ev_set_source(&ev, port);
  snd_seq_ev_set_subs(&ev);
  snd_seq_ev_set_direct(&ev);
  snd_seq_ev_set_controller(&ev, 0, cc, value);
  snd_seq_event_output_direct(seq, &ev);
}

static void send_note(snd_seq_t * seq, int port, int status, int note, int velocity) {
  snd_seq_event_t ev;
  snd_seq_ev_clear(&ev);
  snd_seq_ev_set_source(&ev, port);
  snd_seq_ev_set_subs(&ev);
  snd_seq_ev_set_direct(&ev);
  if (status == 0x90) snd_seq_ev_set_noteon(&ev, 0, note, velocity);
  else snd_seq_ev_set_noteoff(&ev, 0, note, velocity);
  snd_seq_event_output_direct(seq, &ev);
}

static void drain(snd_seq_t * seq, int ms, const char * label) {
  for (int waited = 0; waited < ms; waited += 100) {
    usleep(100000);
    int pending;
    while ((pending = snd_seq_event_input_pending(seq, 1)) > 0) {
      snd_seq_event_t * in_ev;
      snd_seq_event_input(seq, &in_ev);
      if (in_ev->type == SND_SEQ_EVENT_SYSEX) {
        fprintf(stderr, "received sysex %s (%d bytes):", label, in_ev->data.ext.len);
        unsigned char * data = (unsigned char *)in_ev->data.ext.ptr;
        for (unsigned int i = 0; i < in_ev->data.ext.len; i++) fprintf(stderr, " %02x", data[i]);
        fprintf(stderr, "\n");
      }
      snd_seq_free_event(in_ev);
    }
  }
}

int main() {
  fake_ready_init();
  snd_seq_t * seq;
  if (snd_seq_open(&seq, "default", SND_SEQ_OPEN_DUPLEX, 0) < 0) return 1;
  snd_seq_set_client_name(seq, "Launchpad X (e2e)"); // see LaunchpadIO::acceptsClient()
  int port = snd_seq_create_simple_port(seq, "Launchpad X MIDI 2",
    SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_READ | SND_SEQ_PORT_CAP_SUBS_WRITE,
    SND_SEQ_PORT_TYPE_APPLICATION);
  if (port < 0) return 1;
  fprintf(stderr, "fake Launchpad X (stopclip) ready as client %d port %d\n", snd_seq_client_id(seq), port);

  fake_wait_ready(seq, "at startup");
  drain(seq, 500, "at startup"); // the first LED frames

  fprintf(stderr, "sending press on pad (0,0) [note 11] - triggers pool index 7\n");
  send_note(seq, port, 0x90, 11, 100);
  send_note(seq, port, 0x80, 11, 0);
  drain(seq, 1000, "after trigger");

  fprintf(stderr, "sending CC95 press+release (enters Session's own mixer submode)\n");
  send_cc(seq, port, 95, 127);
  send_cc(seq, port, 95, 0);
  drain(seq, 300, "mixer submode entered");

  fprintf(stderr, "sending CC49 press - opens the track-picker overlay\n");
  send_cc(seq, port, 49, 127);
  send_cc(seq, port, 49, 0); // release - CC49 is a plain press-only toggle now, the release is a no-op
  drain(seq, 300, "after CC49 press");

  fprintf(stderr, "sending press on pad (0,0) [note 11] again - picker row (bottom), column 0 - queues a stop\n");
  send_note(seq, port, 0x90, 11, 100);
  send_note(seq, port, 0x80, 11, 0);
  drain(seq, 2000, "after stop should have taken effect");

  fprintf(stderr, "sending CC49 press again - closes the still-open overlay\n");
  send_cc(seq, port, 49, 127);
  send_cc(seq, port, 49, 0);
  drain(seq, 300, "after CC49 closed the overlay");

  snd_seq_close(seq);
  return 0;
}
