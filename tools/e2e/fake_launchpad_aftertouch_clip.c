// Simulates a Launchpad X performing a live, Record-Arm-driven take:
// arms Record Arm (CC19, redirecting the take into a real Clip instance -
// see Controller::ensureNoteRecordingClip()), holds a note across several
// rows of playback, and sends aftertouch partway through the hold - the
// exact shape verify_launchpad_aftertouch_clip.py checks for visibility
// in the PatternEditor's own velocity column.
#include <alsa/asoundlib.h>
#include <stdio.h>
#include <unistd.h>

static void send_note(snd_seq_t * seq, int port, int status, int note, int velocity) {
  snd_seq_event_t ev;
  snd_seq_ev_clear(&ev);
  snd_seq_ev_set_source(&ev, port);
  snd_seq_ev_set_subs(&ev);
  snd_seq_ev_set_direct(&ev);
  if (status == 0x90) {
    snd_seq_ev_set_noteon(&ev, 0, note, velocity);
  } else if (status == 0x80) {
    snd_seq_ev_set_noteoff(&ev, 0, note, velocity);
  } else if (status == 0xA0) {
    ev.type = SND_SEQ_EVENT_KEYPRESS;
    ev.data.note.channel = 0;
    ev.data.note.note = note;
    ev.data.note.velocity = velocity;
  }
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
  fprintf(stderr, "fake Launchpad X (aftertouch-clip) ready as client %d port %d\n", snd_seq_client_id(seq), port);

  sleep(6); // let synth auto-connect, enter Programmer mode, and settle

  // GridMode defaults to SESSION on every connected device - a plain
  // note-on there launches a Session View clip slot instead of entering a
  // note (LaunchpadManager routes by grid mode before any note-entry code
  // ever runs). CC96 selects NOTES mode, the same as pressing the
  // Launchpad's own dedicated Note button.
  fprintf(stderr, "sending CC96 press (Note mode)\n");
  send_cc(seq, port, 96, 127);
  sleep(1);

  fprintf(stderr, "sending CC19 press (Record Arm on)\n");
  send_cc(seq, port, 19, 127);
  sleep(1);

  fprintf(stderr, "sending press on pad (0,0)\n");
  send_note(seq, port, 0x90, 11, 100);
  sleep(2); // let several rows of playback pass under the held note

  fprintf(stderr, "sending aftertouch on pad (0,0), pressure 90\n");
  send_note(seq, port, 0xA0, 11, 90);
  sleep(2); // let several more rows pass

  fprintf(stderr, "sending aftertouch on pad (0,0), pressure 40\n");
  send_note(seq, port, 0xA0, 11, 40);
  sleep(2);

  fprintf(stderr, "sending release on pad (0,0)\n");
  send_note(seq, port, 0x80, 11, 0);
  sleep(1);

  snd_seq_close(seq);
  return 0;
}
