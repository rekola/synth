// Simulated Launchpad X sending a 3-note chord (near-simultaneous presses,
// then a non-LIFO release order) to verify chord entry doesn't collide
// notes into the same sub-column and that step-entry advances only once
// per gesture (not per pad).
#include <alsa/asoundlib.h>
#include <stdio.h>
#include <unistd.h>

void send_note(snd_seq_t * seq, int port, int status, int note, int velocity) {
  snd_seq_event_t ev;
  snd_seq_ev_clear(&ev);
  snd_seq_ev_set_source(&ev, port);
  snd_seq_ev_set_subs(&ev);
  snd_seq_ev_set_direct(&ev);
  if (status == 0x90) snd_seq_ev_set_noteon(&ev, 0, note, velocity);
  else if (status == 0x80) snd_seq_ev_set_noteoff(&ev, 0, note, velocity);
  snd_seq_event_output_direct(seq, &ev);
}

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
  fprintf(stderr, "fake Launchpad X (chord) ready as client %d port %d\n", snd_seq_client_id(seq), port);

  sleep(4); // let synth auto-connect and enter Programmer mode

  // GridMode defaults to SESSION - a plain note-on there launches a
  // Session View clip slot instead of entering a note; CC96 selects
  // NOTES mode. A press also only actually writes into the pattern
  // (rather than just auditioning) with Record Arm (CC19) on.
  fprintf(stderr, "sending CC96 press (Note mode)\n");
  send_cc(seq, port, 96, 127);
  sleep(1);
  fprintf(stderr, "sending CC19 press (Record Arm on)\n");
  send_cc(seq, port, 19, 127);
  sleep(1);

  // Pads (0,0)=note 11, (1,0)=note 12, (2,0)=note 13 - a 3-note chord,
  // pressed within a couple ms of each other (as close to simultaneous as
  // separate MIDI messages get), held briefly, then released out of order.
  fprintf(stderr, "pressing 3-note chord (near-simultaneous)\n");
  send_note(seq, port, 0x90, 11, 100);
  send_note(seq, port, 0x90, 12, 95);
  send_note(seq, port, 0x90, 13, 90);
  sleep(2);

  fprintf(stderr, "releasing chord, non-LIFO order (middle pad first)\n");
  send_note(seq, port, 0x80, 12, 0);
  usleep(20000);
  send_note(seq, port, 0x80, 11, 0);
  usleep(20000);
  send_note(seq, port, 0x80, 13, 0);
  sleep(2);

  snd_seq_close(seq);
  return 0;
}
