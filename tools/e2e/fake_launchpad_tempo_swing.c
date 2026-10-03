// Simulated Launchpad X exercising the Tempo and Swing views: shift (CC91
// held) + Send B (CC59) opens Tempo, Stop Clip (CC49) switches to
// Swing, CC91 / CC92 are the up / down arrows, and CC95 leaves. The python
// script reads the LED frames synth sends back (the number drawn on the
// pads, the arrows' and entry buttons' colours).
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
  fprintf(stderr, "fake Launchpad X (tempo/swing) ready as client %d port %d\n", snd_seq_client_id(seq), port);

  fake_wait_ready(seq, "at startup");
  drain(seq, 500, "at startup");

  fprintf(stderr, "step: shift + Send B (opens Tempo)\n");
  send_cc(seq, port, 91, 127);
  send_cc(seq, port, 59, 127);
  send_cc(seq, port, 59, 0);
  send_cc(seq, port, 91, 0);
  drain(seq, 600, "tempo view");

  fprintf(stderr, "step: CC92 down arrow\n");
  send_cc(seq, port, 92, 127);
  send_cc(seq, port, 92, 0);
  drain(seq, 600, "tempo down");

  fprintf(stderr, "step: CC91 up arrow tap\n");
  send_cc(seq, port, 91, 127);
  send_cc(seq, port, 91, 0);
  drain(seq, 600, "tempo up");

  fprintf(stderr, "step: shift + Stop Clip (switches to Swing)\n");
  send_cc(seq, port, 49, 127);
  send_cc(seq, port, 49, 0);
  drain(seq, 600, "swing view");

  fprintf(stderr, "step: CC95 leaves\n");
  send_cc(seq, port, 95, 127);
  send_cc(seq, port, 95, 0);
  drain(seq, 600, "left");

  snd_seq_close(seq);
  return 0;
}
