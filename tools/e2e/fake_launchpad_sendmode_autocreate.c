// Simulated Launchpad X that toggles into Send A grid mode (CC69) and
// presses column 5, row 4 - run against songs/songtest1.xml (2 tracks), so
// column 5 doesn't exist yet. Exercises PatternEditor::handleLaunchpadPadEvent's
// auto-create-missing-tracks behavior: the first 8 grid columns must work
// even before that many tracks exist. Drains and logs the LED SysEx both
// before and after the press so the verify script can confirm column 5
// goes from "no track there" (forced dark regardless of value) to a real,
// lit bargraph. Deliberately doesn't use Ctrl-N/a fresh song to get down to
// fewer tracks - Ctrl-N has a known post-new-song staleness quirk in this
// harness (see verify_keybindings.py's docstring); a fixture with few
// tracks sidesteps it entirely.
#include <alsa/asoundlib.h>
#include <stdio.h>
#include <unistd.h>

void drain(snd_seq_t * seq, const char * label) {
  fprintf(stderr, "--- draining (%s) ---\n", label);
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
}

void send_cc(snd_seq_t * seq, int port, int cc, int value) {
  snd_seq_event_t ev;
  snd_seq_ev_clear(&ev);
  snd_seq_ev_set_source(&ev, port);
  snd_seq_ev_set_subs(&ev);
  snd_seq_ev_set_direct(&ev);
  snd_seq_ev_set_controller(&ev, 0, cc, value);
  snd_seq_event_output_direct(seq, &ev);
}

void send_note(snd_seq_t * seq, int port, int status, int note, int velocity) {
  snd_seq_event_t ev;
  snd_seq_ev_clear(&ev);
  snd_seq_ev_set_source(&ev, port);
  snd_seq_ev_set_subs(&ev);
  snd_seq_ev_set_direct(&ev);
  if (status == 0x90) {
    snd_seq_ev_set_noteon(&ev, 0, note, velocity);
  } else if (status == 0x80) {
    snd_seq_ev_set_noteoff(&ev, 0, note, velocity);
  }
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
  fprintf(stderr, "fake Launchpad X (send mode autocreate) ready as client %d port %d\n", snd_seq_client_id(seq), port);

  sleep(6); // let synth auto-connect, enter Programmer mode, and settle
  drain(seq, "idle - NOTES mode, 2-track song");

  fprintf(stderr, "sending CC69 press+release (Send A mode toggle)\n");
  send_cc(seq, port, 69, 127);
  usleep(200000);
  send_cc(seq, port, 69, 0);
  sleep(1);
  drain(seq, "Send A mode entered - before press - column 5 has no track yet");

  fprintf(stderr, "sending press+release on pad (5,4) [note 56]\n");
  send_note(seq, port, 0x90, 56, 100);
  usleep(200000);
  send_note(seq, port, 0x80, 56, 0);
  sleep(1);
  drain(seq, "after pad press - column 5 track auto-created");

  snd_seq_close(seq);
  return 0;
}
