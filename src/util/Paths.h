#ifndef _PATHS_H_
#define _PATHS_H_

#include <filesystem>
#include <string>

// Where the program keeps and finds its files, per platform. Callers add
// their own sub-paths to these roots, so a directory name is spelled once.
namespace paths {

// Settings that belong to the machine's user and never to a song:
// $XDG_CONFIG_HOME/synth, else ~/.config/synth (Linux); %APPDATA%\synth
// (Windows). Empty if the environment names no home.
std::filesystem::path configDir();

// Data the user added, such as SoundFonts and SOFA files, under
// "soundfonts", "sounds/sf2" and "sofa": ~/.local/share (Linux);
// %LOCALAPPDATA%\synth (Windows). Empty if there is no home.
std::filesystem::path userDataDir();

// The same kinds of data installed for every user: /usr/share (Linux); the
// "share" directory next to the executable (Windows).
std::filesystem::path installDataDir();

// The directory the running executable is in; empty if it cannot be found.
std::filesystem::path executableDir();

// The song `synth` opens when given none, as installed: the path baked in at
// configure time (Linux); relative to the executable (Windows).
std::string installedWelcomeSongPath();

} // namespace paths

#endif
