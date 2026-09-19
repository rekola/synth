// Simulated Launchpad X exercising the shift+pad "open for editing"
// gesture (CC91-held, see fake_launchpad_shift_stepgrid.c's own comment)
// on a clip belonging to a *lane-less* PercussionTrack - Controller::
// toggleDrumClipFocus() doesn't gate on lane count, so this should still
// open the step grid, just completely empty - see
// verify_launchpad_shift_no_lanes.py's own docstring for the regression
// this catches. Presses shift+pad on pad (1,0) [note 12] - track 1's own
// clip index 7 in the fixture.
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
  fprintf(stderr, "fake Launchpad X (shift no lanes) ready as client %d port %d\n", snd_seq_client_id(seq), port);

  fake_wait_ready(seq, "at startup");
  drain(seq, 500, "at startup"); // the first LED frames

  fprintf(stderr, "sending CC91 press (holding shift)\n");
  send_cc(seq, port, 91, 127);
  drain(seq, 300, "shift held");

  fprintf(stderr, "sending press+release on pad (1,0) [note 12] while shift held - track 1's clip index 7, lane-less\n");
  send_note(seq, port, 0x90, 12, 100);
  send_note(seq, port, 0x80, 12, 0);
  drain(seq, 300, "pad released");

  fprintf(stderr, "sending CC91 release - move-row-up must NOT fire, it was combined with a pad\n");
  send_cc(seq, port, 91, 0);
  drain(seq, 1500, "shift released, settled");

  snd_seq_close(seq);
  return 0;
}
