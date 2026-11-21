#include "TestFramework.h"

#include "../src/launchpad/LaunchpadLayout.h"
#include "../src/launchpad/LaunchpadManager.h"
#include "../src/model/Song.h"

#include <vector>

// With a clip open for editing the playing rows are the scale in order, tonic first, left to right and then bottom
// to top; the rows above are the steps and the keyboard otherwise.
TEST(launchpad_step_view_shows_the_scale_degrees_in_order) {
  Song song(Tuning::EDO53, 5 * 53); // C
  song.setScale(Scale::OTONAL);
  LaunchpadManager manager;
  auto notes = manager.resolveKeyboardNotes(song, 0, true);
  // Otonal C: C D𝄫 D♭↓ D D♯ D𝄪 E F♭ F F♯↑ F𝄪 G G𝄪 A A♯↑ B♭ B B♯, then the next octave.
  const std::vector<int> otonal = {0, 2, 5, 9, 12, 16, 17, 19, 22, 26, 29, 31, 38, 39, 43, 45, 48, 51};
  for (int n = 0; n < 8 * LaunchpadLayout::kPlayRows; n++) {
    int octave = n / 18;
    CHECK(notes[static_cast<size_t>(n)] - notes[0] == otonal[static_cast<size_t>(n % 18)] + octave * 53);
  }
  // Without the step view the pads are the lattice, whose tonic is at column 4 of the bottom row.
  auto lattice = manager.resolveKeyboardNotes(song, 0, false);
  CHECK(lattice[4] == notes[0]);
  CHECK(lattice[0] != notes[0]);
}
