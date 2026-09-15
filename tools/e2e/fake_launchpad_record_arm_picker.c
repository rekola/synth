// Simulated Launchpad X exercising the track-picker overlay's RECORD_ARM
// purpose (CC19): one of the eight members of the same right-column
// mixer-submode radio group as Stop Clip/Mute/Solo (89-29 - see
// LaunchpadManager.h's own GridMode comment), gated on that submode
// being on the same way they are - a plain CC19 press launches scene row
// 0 instead while mixer submode is off, so this presses CC95 a second
// time first (same precondition fake_launchpad_mute_picker.c needs for
// CC39). Once in mixer submode, presses CC19 to open the overlay (the
// picker row shows dim red for a track that isn't armed, bright red for
// one that is - reusing Stop
// Clip's own hue since the two purposes never show at once), picks the
// fixture's only track (pad (0,0), column 0) to arm it, confirms the
// overlay stays open (a second CC19 press is what finally closes it,
// checked last) rather than auto-closing on a pick. Deliberately avoids
// ever triggering playback, same reasoning as fake_launchpad_mute_picker.c
// - arming itself is pure bookkeeping with nothing to hear.
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
  fprintf(stderr, "fake Launchpad X (record arm picker) ready as client %d port %d\n", snd_seq_client_id(seq), port);

  sleep(6); // let synth auto-connect, enter Programmer mode, and settle
  drain(seq, 500, "at startup");

  fprintf(stderr, "sending CC95 press+release (enters Session's own mixer submode)\n");
  send_cc(seq, port, 95, 127);
  send_cc(seq, port, 95, 0);
  drain(seq, 500, "mixer submode entered");

  fprintf(stderr, "sending CC19 press - opens the track-picker overlay (Record Arm)\n");
  send_cc(seq, port, 19, 127);
  send_cc(seq, port, 19, 0); // release - a quick tap, well under the momentary hold-to-preview threshold, so the switch that already happened on press stands
  drain(seq, 1000, "after CC19 press");

  fprintf(stderr, "sending press on pad (0,0) [note 11] - picker row (bottom), column 0 - arms the track\n");
  send_note(seq, port, 0x90, 11, 100);
  send_note(seq, port, 0x80, 11, 0);
  drain(seq, 1000, "after picking column 0");

  fprintf(stderr, "sending CC19 press again - closes the still-open overlay\n");
  send_cc(seq, port, 19, 127);
  send_cc(seq, port, 19, 0);
  drain(seq, 1000, "after CC19 closed the overlay");

  snd_seq_close(seq);
  return 0;
}
