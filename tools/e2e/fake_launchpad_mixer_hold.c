// Simulated Launchpad X exercising the mixer radio group's own momentary
// hold-to-preview gesture (LaunchpadManager::armMixerHoldPreview()/
// handleMixerFunctionRelease()): a quick tap on a different member of the
// group switches to it and stays there (sticky, the ordinary case), but a
// genuine hold (>= 600ms, LaunchpadManager.cpp's own
// kMixerHoldPreviewThreshold) reverts back to whatever was showing right
// before that press once released.
//
// Sequence: enters Session's own mixer submode (CC95 x2), quick-taps Send A
// (CC69) so it becomes the sticky selection, long-holds Mute (CC39, > the
// threshold) and releases - expecting the display to land back on Send A,
// not stay on Mute - then quick-taps Mute again as a control, expecting it
// to stay this time (sticky, no hold involved).
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

static void drain(snd_seq_t * seq, const char * label) {
  fprintf(stderr, "--- draining (%s) ---\n", label);
  int pending;
  while ((pending = snd_seq_event_input_pending(seq, 1)) > 0) {
    snd_seq_event_t * ev;
    snd_seq_event_input(seq, &ev);
    if (ev->type == SND_SEQ_EVENT_SYSEX) {
      fprintf(stderr, "received sysex (%d bytes):", ev->data.ext.len);
      unsigned char * data = (unsigned char *)ev->data.ext.ptr;
      for (unsigned int i = 0; i < ev->data.ext.len; i++) fprintf(stderr, " %02x", data[i]);
      fprintf(stderr, "\n");
    }
    snd_seq_free_event(ev);
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
  fprintf(stderr, "fake Launchpad X (mixer hold) ready as client %d port %d\n", snd_seq_client_id(seq), port);

  sleep(6); // let synth auto-connect, enter Programmer mode, and settle
  drain(seq, "idle - SESSION mode (the connect-time default)");

  fprintf(stderr, "sending CC95 press+release (enters Session's own mixer submode)\n");
  send_cc(seq, port, 95, 127);
  usleep(200000);
  send_cc(seq, port, 95, 0);
  sleep(1);
  drain(seq, "mixer submode entered");

  fprintf(stderr, "sending CC69 quick tap (Send A becomes the sticky selection)\n");
  send_cc(seq, port, 69, 127);
  usleep(50000); // well under the 600ms hold threshold
  send_cc(seq, port, 69, 0);
  sleep(1);
  drain(seq, "after CC69 quick tap - showing Send A");

  fprintf(stderr, "sending CC39 press (long hold begins - previews Mute)\n");
  send_cc(seq, port, 39, 127);
  usleep(200000);
  drain(seq, "mid-hold on CC39 - previewing Mute");
  usleep(600000); // total hold now ~800ms, past the 600ms threshold
  fprintf(stderr, "sending CC39 release (long hold ends - should revert to Send A)\n");
  send_cc(seq, port, 39, 0);
  sleep(1);
  drain(seq, "after CC39 long-hold release - should be back on Send A");

  fprintf(stderr, "sending CC39 quick tap (control - should stay on Mute this time)\n");
  send_cc(seq, port, 39, 127);
  usleep(50000);
  send_cc(seq, port, 39, 0);
  sleep(1);
  drain(seq, "after CC39 quick tap - showing Mute");

  snd_seq_close(seq);
  return 0;
}
