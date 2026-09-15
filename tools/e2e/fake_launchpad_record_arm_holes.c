// Simulated Launchpad X exercising the per-track Record Arm mechanism's
// actual *recording* gesture end to end - not just the arm/disarm picker
// (already covered by fake_launchpad_record_arm_picker.c), but a real
// NOTE-mode note landing in the exact Session-view clip index that was
// pressed while armed, holes and all: arms the fixture's only track, picks
// clip index 2 (note 61, pad x=0/y=5 - LaunchpadManager's own y-flip
// convention, see handleSessionPadEvent()) on a track with no clips at all
// yet, switches to NOTE mode and plays one note, then disarms - all
// through real MIDI CC/note events, the same as a person would press them.
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
  snd_seq_t * seq;
  if (snd_seq_open(&seq, "default", SND_SEQ_OPEN_DUPLEX, 0) < 0) return 1;
  snd_seq_set_client_name(seq, "Launchpad X");
  int port = snd_seq_create_simple_port(seq, "Launchpad X MIDI 2",
    SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_READ | SND_SEQ_PORT_CAP_SUBS_WRITE,
    SND_SEQ_PORT_TYPE_APPLICATION);
  if (port < 0) return 1;
  fprintf(stderr, "fake Launchpad X (record arm holes) ready as client %d port %d\n", snd_seq_client_id(seq), port);

  sleep(6); // let synth auto-connect, enter Programmer mode, and settle
  drain(seq, 500, "at startup");

  fprintf(stderr, "sending CC95 press+release (enters Session's own mixer submode)\n");
  send_cc(seq, port, 95, 127);
  send_cc(seq, port, 95, 0);
  drain(seq, 500, "mixer submode entered");

  fprintf(stderr, "sending CC19 press+release - opens the track-picker overlay (Record Arm)\n");
  send_cc(seq, port, 19, 127);
  send_cc(seq, port, 19, 0);
  drain(seq, 500, "picker opened");

  fprintf(stderr, "sending press on pad (0,0) [note 11] - picker row, column 0 - arms the track\n");
  send_note(seq, port, 0x90, 11, 100);
  send_note(seq, port, 0x80, 11, 0);
  drain(seq, 500, "track armed");

  fprintf(stderr, "sending CC19 press+release - closes the overlay\n");
  send_cc(seq, port, 19, 127);
  send_cc(seq, port, 19, 0);
  drain(seq, 500, "overlay closed");

  fprintf(stderr, "sending press on pad (0,5) [note 61] - Session grid, clip index 2 (7-5) on a track with no clips yet\n");
  send_note(seq, port, 0x90, 61, 100);
  send_note(seq, port, 0x80, 61, 0);
  drain(seq, 500, "recording armed at clip index 2");

  fprintf(stderr, "sending CC96 press+release (Note mode)\n");
  send_cc(seq, port, 96, 127);
  send_cc(seq, port, 96, 0);
  drain(seq, 500, "note mode entered");

  fprintf(stderr, "sending press+release on pad (0,0) [note 11] in Note mode - writes a note into the armed take\n");
  send_note(seq, port, 0x90, 11, 100);
  send_note(seq, port, 0x80, 11, 0);
  drain(seq, 500, "note recorded");

  fprintf(stderr, "sending CC95 press+release (back to the plain Session grid, mixer submode still on)\n");
  send_cc(seq, port, 95, 127);
  send_cc(seq, port, 95, 0);
  drain(seq, 500, "back on session grid");

  fprintf(stderr, "sending CC19 press+release - reopens the track-picker overlay\n");
  send_cc(seq, port, 19, 127);
  send_cc(seq, port, 19, 0);
  drain(seq, 500, "picker reopened");

  fprintf(stderr, "sending press on pad (0,0) [note 11] again - disarms the track, finalizing the take\n");
  send_note(seq, port, 0x90, 11, 100);
  send_note(seq, port, 0x80, 11, 0);
  drain(seq, 1000, "track disarmed");

  fprintf(stderr, "sending CC19 press+release - closes the overlay\n");
  send_cc(seq, port, 19, 127);
  send_cc(seq, port, 19, 0);
  drain(seq, 500, "overlay closed");

  snd_seq_close(seq);
  return 0;
}
