// Simulated Launchpad X used to verify LaunchpadManager's *per-device*
// state: argv[1] is a suffix appended to the ALSA client name (so two
// instances can run simultaneously and be told apart in logs/aconnect
// output; LaunchpadProtocol::modelFromDeviceName only requires the name
// to *contain* "Launchpad X", so a suffix is harmless), argv[2] is how
// many times to press+release CC91 (octave-up) before finally pressing
// pad (0,0) [note 11] and releasing it.
#include <alsa/asoundlib.h>
#include <stdio.h>
#include <stdlib.h>
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

int main(int argc, char ** argv) {
  const char * suffix = argc > 1 ? argv[1] : "";
  int octave_ups = argc > 2 ? atoi(argv[2]) : 0;

  snd_seq_t * seq;
  if (snd_seq_open(&seq, "default", SND_SEQ_OPEN_DUPLEX, 0) < 0) return 1;
  char name[64];
  snprintf(name, sizeof(name), "Launchpad X %s", suffix);
  snd_seq_set_client_name(seq, name);
  int port = snd_seq_create_simple_port(seq, "Launchpad X MIDI 2",
    SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_READ | SND_SEQ_PORT_CAP_SUBS_WRITE,
    SND_SEQ_PORT_TYPE_APPLICATION);
  if (port < 0) return 1;
  fprintf(stderr, "fake %s ready as client %d port %d\n", name, snd_seq_client_id(seq), port);

  sleep(6); // let synth auto-connect, enter Programmer mode, and settle

  int pending;
  while ((pending = snd_seq_event_input_pending(seq, 1)) > 0) {
    snd_seq_event_t * in_ev;
    snd_seq_event_input(seq, &in_ev);
    snd_seq_free_event(in_ev);
  }

  for (int i = 0; i < octave_ups; i++) {
    fprintf(stderr, "%s: sending CC91 (octave-up) press+release #%d\n", name, i + 1);
    send_cc(seq, port, 91, 127);
    usleep(300000);
    send_cc(seq, port, 91, 0);
    usleep(300000);
  }

  fprintf(stderr, "%s: sending press on pad (0,0) [note 11]\n", name);
  send_note(seq, port, 0x90, 11, 100);
  usleep(500000);
  fprintf(stderr, "%s: sending release on pad (0,0)\n", name);
  send_note(seq, port, 0x80, 11, 0);
  sleep(1);

  snd_seq_close(seq);
  return 0;
}
