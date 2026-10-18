// Simulated Launchpad X that presses nothing at all - just connects and
// logs every SysEx it receives, so verify_launchpad_clear_on_exit.py can
// confirm synth's final message, sent right as it quits
// (LaunchpadIO::clearAllLeds(), its own destructor's last act), actually
// blanks every LED rather than leaving the device showing whatever
// Live View happened to be lit at the moment of quitting.
#include <alsa/asoundlib.h>
#include <stdio.h>
#include <unistd.h>

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
  snd_seq_set_client_name(seq, "Launchpad X (e2e)"); // see LaunchpadIO::acceptsClient()
  int port = snd_seq_create_simple_port(seq, "Launchpad X MIDI 2",
    SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_READ | SND_SEQ_PORT_CAP_SUBS_WRITE,
    SND_SEQ_PORT_TYPE_APPLICATION);
  if (port < 0) return 1;
  fprintf(stderr, "fake Launchpad X (clear on exit) ready as client %d port %d\n", snd_seq_client_id(seq), port);

  // Long enough to comfortably outlast the Python driver's own quit
  // sequence (connect, settle, C-x C-c, wait for exit) - the driver kills
  // this process once synth has actually exited, well before this drain
  // would otherwise elapse on its own.
  drain(seq, 30000, "while synth runs/quits");

  snd_seq_close(seq);
  return 0;
}
