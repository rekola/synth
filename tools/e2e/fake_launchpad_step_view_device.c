// Two-device regression test for opening a clip's step view: only the
// device the performer used switches to it, and only that device pages -
// see verify_launchpad_step_view_device.py's own docstring. argv[1] is a
// suffix appended to the ALSA client name (so two instances can run
// simultaneously and be told apart); argv[2] is this instance's role:
// "opener" opens the clip (CC91-held shift + a press+release on pad (0,0))
// and pages forward once (CC94); "follower" only connects and logs. Both
// print a plain-text MARKER line at matching offsets from their own start
// so the driver can split each log into before/after phases.
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

static void drain(snd_seq_t * seq, int ms, const char * name, const char * label) {
  for (int waited = 0; waited < ms; waited += 100) {
    usleep(100000);
    int pending;
    while ((pending = snd_seq_event_input_pending(seq, 1)) > 0) {
      snd_seq_event_t * in_ev;
      snd_seq_event_input(seq, &in_ev);
      if (in_ev->type == SND_SEQ_EVENT_SYSEX) {
        fprintf(stderr, "%s: received sysex %s (%d bytes):", name, label, in_ev->data.ext.len);
        unsigned char * data = (unsigned char *)in_ev->data.ext.ptr;
        for (unsigned int i = 0; i < in_ev->data.ext.len; i++) fprintf(stderr, " %02x", data[i]);
        fprintf(stderr, "\n");
      }
      snd_seq_free_event(in_ev);
    }
  }
}

int main(int argc, char ** argv) {
  fake_ready_init();
  const char * suffix = argc > 1 ? argv[1] : "";
  int is_opener = argc > 2 && argv[2][0] == 'o';

  snd_seq_t * seq;
  if (snd_seq_open(&seq, "default", SND_SEQ_OPEN_DUPLEX, 0) < 0) return 1;
  char name[64];
  snprintf(name, sizeof(name), "Launchpad X %s (e2e)", suffix); // see LaunchpadIO::acceptsClient()
  snd_seq_set_client_name(seq, name);
  int port = snd_seq_create_simple_port(seq, "Launchpad X MIDI 2",
    SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_READ | SND_SEQ_PORT_CAP_SUBS_WRITE,
    SND_SEQ_PORT_TYPE_APPLICATION);
  if (port < 0) return 1;
  fprintf(stderr, "fake %s ready as client %d port %d\n", name, snd_seq_client_id(seq), port);

  fake_wait_ready(seq, "at startup");
  drain(seq, 500, name, "at startup"); // the first LED frames

  if (is_opener) {
    fprintf(stderr, "%s: opening the clip (shift-held pad (0,0))\n", name);
    send_cc(seq, port, 91, 127);
    usleep(200000);
    send_note(seq, port, 0x90, 11, 100);
    usleep(100000);
    send_note(seq, port, 0x80, 11, 0);
    send_cc(seq, port, 96, 127); // shift + Note opens the selected clip
    send_cc(seq, port, 96, 0);
    usleep(200000);
    send_cc(seq, port, 91, 0);
  } else {
    usleep(500000); // roughly matches the opener's own ~0.5s open sequence above
  }
  fprintf(stderr, "%s: MARKER post-open\n", name);
  drain(seq, 1500, name, "post-open");

  if (is_opener) {
    fprintf(stderr, "%s: paging forward once (CC94)\n", name);
    send_cc(seq, port, 94, 127);
    usleep(100000);
    send_cc(seq, port, 94, 0);
  } else {
    usleep(100000); // matches the opener's own brief CC94 press+release above
  }
  fprintf(stderr, "%s: MARKER post-page\n", name);
  drain(seq, 1500, name, "post-page");

  snd_seq_close(seq);
  return 0;
}
