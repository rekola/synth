#ifndef _TRACKDRAG_H_
#define _TRACKDRAG_H_

// What is dragged when a track is picked up by its header (the clip grid) or
// its name (the arrangement grid): payload kind "track", dropped on another
// track to take its place (Song::moveTrack()).
struct TrackDrag {
  int track_id = 0;
};

#endif
