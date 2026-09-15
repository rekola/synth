// Simulated Launchpad X used to verify LaunchpadManager's *per-device*
// state: argv[1] is a suffix appended to the ALSA client name (so two
// instances can run simultaneously and be told apart in logs/aconnect
// output; LaunchpadProtocol::modelFromDeviceName only requires the name
// to *contain* "Launchpad X", so a suffix is harmless). argv[2] is this
// instance's own role: "arm" switches into NOTES mode (CC96) and presses
// Record Arm (a quick CC98 tap - CC19 itself is a full member of the
// scene-launch/mixer radio group now, not this command) - a single
// song-wide flag, not per-device, so only one connected instance should
// ever do this - before pressing; "plain"
// only switches into its own NOTES mode and presses, relying on
// whichever "arm" instance already armed Record Arm (a plain press never
// writes into the pattern, only auditions, without it). Neither role
// ever disarms - the caller just tears the whole test down once it's
// done, and Record Arm itself outlives any one device's own connection
// (it's Song state, not per-device connection state). argv[3] is the
// note to press+release (defaults to 11, pad (0,0)).
#include <alsa/asoundlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
  int is_arm = argc > 2 && strcmp(argv[2], "arm") == 0;
  int note = argc > 3 ? atoi(argv[3]) : 11;

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

  fprintf(stderr, "%s: sending CC96 press (Note mode)\n", name);
  send_cc(seq, port, 96, 127);
  sleep(1);

  if (is_arm) {
    fprintf(stderr, "%s: sending CC98 quick tap (Record Arm on)\n", name);
    send_cc(seq, port, 98, 127);
    usleep(100 * 1000);
    send_cc(seq, port, 98, 0);
    sleep(1);
  } else {
    // Give the "arm" instance time to have actually armed (and its own
    // rising edge to have started playback) before this one presses -
    // a plain press before that would land with Record Arm still off.
    sleep(2);
  }

  fprintf(stderr, "%s: sending press on pad [note %d]\n", name, note);
  send_note(seq, port, 0x90, note, 100);
  usleep(500000);
  fprintf(stderr, "%s: sending release on pad [note %d]\n", name, note);
  send_note(seq, port, 0x80, note, 0);
  sleep(1);

  snd_seq_close(seq);
  return 0;
}
