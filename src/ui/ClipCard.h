#ifndef _CLIPCARD_H_
#define _CLIPCARD_H_

#include <cellui/Plane.h>
#include "StyleProvider.h"

// How a clip stands on a grid wherever one shows - a slot of the clip grid, a
// block of the arrangement grid, the ghost of either being dragged: a card in
// its track's colour, lifted off the grid, so the display lights its edges and
// casts its shadow. Cells written between lift() and flat() are the card.
namespace clip_card {

// The cursor's card stands as high as the others but casts no shadow, so no
// other clip's shadow falls on it.
inline void lift(cellui::Plane & plane, const StyleProvider & styles, bool cursor = false) {
  if (cursor)
    plane.setLift(styles.theme.elevation.card, false, false);
  else
    plane.setLift(styles.theme.elevation.card);
}

inline void flat(cellui::Plane & plane) { plane.setLift(0); }

} // namespace clip_card

#endif
