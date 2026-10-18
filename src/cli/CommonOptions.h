#ifndef _COMMONOPTIONS_H_
#define _COMMONOPTIONS_H_

#include "../ambisonic/ChannelConfiguration.h"

#include <string>

class Controller;

// The command-line options every executable shares: the ones --render
// honours (the interactive binary and the render-only one parse them here).
struct CommonOptions {
  bool force_cardioid = false;        // --stereo
  bool force_legacy_binaural = false; // --legacy-binaural
  bool show_licenses = false;         // --licenses
  std::string render_path;            // --render
};

// Tries argv[i] as one of the shared options, consuming its argument too
// (i then names the last argv used). False if it is not one; exits with a
// message on a malformed one.
bool parseCommonOption(int argc, char * argv[], int & i, CommonOptions & options, ChannelConfiguration & channel_config);

void printLicenses();

// Applies --stereo/--legacy-binaural to a controller.
void applyCommonOptions(const CommonOptions & options, Controller & controller);

// Render the loaded song offline and write it as a WAV file.
bool renderSongToWav(Controller & controller, const ChannelConfiguration & channel_config, const std::string & path);

#endif
