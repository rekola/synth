// Simulated Launchpad X exercising CC96 (Note)'s active-state LED and
// CC97 (Custom)'s instant-on-press mode switch: presses/releases
// CC96, then presses CC97 and holds it for a while *before* releasing,
// so the verify script can confirm Custom mode's own LED already lit up
// while CC97 is still held down, not only after release.
#include <alsa/asoundlib.h>
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

int main() {
  snd_seq_t * seq;
  if (snd_seq_open(&seq, "default", SND_SEQ_OPEN_DUPLEX, 0) < 0) return 1;
  snd_seq_set_client_name(seq, "Launchpad X");
  int port = snd_seq_create_simple_port(seq, "Launchpad X MIDI 2",
    SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_READ | SND_SEQ_PORT_CAP_SUBS_WRITE,
    SND_SEQ_PORT_TYPE_APPLICATION);
  if (port < 0) return 1;
  fprintf(stderr, "fake Launchpad X (notecustom) ready as client %d port %d\n", snd_seq_client_id(seq), port);

  sleep(6); // let synth auto-connect, enter Programmer mode, and settle

  int pending;
  while ((pending = snd_seq_event_input_pending(seq, 1)) > 0) {
    snd_seq_event_t * in_ev;
    snd_seq_event_input(seq, &in_ev);
    if (in_ev->type == SND_SEQ_EVENT_SYSEX) {
      fprintf(stderr, "received sysex (%d bytes):", in_ev->data.ext.len);
      unsigned char * data = (unsigned char *)in_ev->data.ext.ptr;
      for (unsigned int i = 0; i < in_ev->data.ext.len; i++) fprintf(stderr, " %02x", data[i]);
      fprintf(stderr, "\n");
    }
    snd_seq_free_event(in_ev);
  }

  fprintf(stderr, "sending CC96 press (Note)\n");
  send_cc(seq, port, 96, 127);
  sleep(1);
  fprintf(stderr, "sending CC96 release\n");
  send_cc(seq, port, 96, 0);
  sleep(1);

  // Drain the LED update(s) that followed CC96 before starting CC97, so
  // the verify script's log-ordering check below only sees SysEx traffic
  // that's actually about CC97.
  while ((pending = snd_seq_event_input_pending(seq, 1)) > 0) {
    snd_seq_event_t * in_ev;
    snd_seq_event_input(seq, &in_ev);
    if (in_ev->type == SND_SEQ_EVENT_SYSEX) {
      fprintf(stderr, "received sysex (%d bytes):", in_ev->data.ext.len);
      unsigned char * data = (unsigned char *)in_ev->data.ext.ptr;
      for (unsigned int i = 0; i < in_ev->data.ext.len; i++) fprintf(stderr, " %02x", data[i]);
      fprintf(stderr, "\n");
    }
    snd_seq_free_event(in_ev);
  }

  fprintf(stderr, "sending CC97 press (Custom)\n");
  send_cc(seq, port, 97, 127);

  // Actively drain *before* sending the release, in 100ms steps for up to
  // 1s - anything received here is unambiguous proof synth reacted to the
  // press itself, not the (not yet sent) release.
  for (int waited_ms = 0; waited_ms < 1000; waited_ms += 100) {
    usleep(100000);
    while ((pending = snd_seq_event_input_pending(seq, 1)) > 0) {
      snd_seq_event_t * in_ev;
      snd_seq_event_input(seq, &in_ev);
      if (in_ev->type == SND_SEQ_EVENT_SYSEX) {
        fprintf(stderr, "received sysex while CC97 still held (%d bytes):", in_ev->data.ext.len);
        unsigned char * data = (unsigned char *)in_ev->data.ext.ptr;
        for (unsigned int i = 0; i < in_ev->data.ext.len; i++) fprintf(stderr, " %02x", data[i]);
        fprintf(stderr, "\n");
      }
      snd_seq_free_event(in_ev);
    }
  }

  fprintf(stderr, "sending CC97 release\n");
  send_cc(seq, port, 97, 0);
  sleep(1);

  snd_seq_close(seq);
  return 0;
}
