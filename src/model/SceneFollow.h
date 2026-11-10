#ifndef _SCENEFOLLOW_H_
#define _SCENEFOLLOW_H_

#include <array>
#include <cstring>
#include <string>

// What a scene does once it has played for its follow time: the scene
// follow action. Ten actions are named so the file format and the code
// have their final shape; only None, Next and Stop do anything yet (the
// others read and write, and act as None).
enum class FollowAction { NONE,
                          STOP,
                          AGAIN,
                          PREVIOUS,
                          NEXT,
                          FIRST,
                          LAST,
                          ANY,
                          OTHER,
                          JUMP };

namespace followaction {

inline constexpr std::array<const char *, 10> kNames = {"none", "stop", "again", "previous", "next", "first", "last", "any", "other", "jump"};

inline const char * name(FollowAction action) { return kNames[static_cast<size_t>(action)]; }

// An unknown name is None.
inline FollowAction parse(const char * text) {
  if (!text) return FollowAction::NONE;
  for (size_t i = 0; i < kNames.size(); i++) {
    if (std::strcmp(text, kNames[i]) == 0) return static_cast<FollowAction>(i);
  }
  return FollowAction::NONE;
}

// The <scene> attributes the full feature will use; kept verbatim by the
// song file but not acted on. followAction itself is the first slot.
inline constexpr std::array<const char *, 6> kReservedAttributes = {
    "followActionB", "followActionChanceA", "followActionChanceB", "followActionTime", "followActionJumpTarget", "followActionEnabled"};

// What the transport does when a scene reaches its follow time.
struct FollowResult {
  enum Kind { NOTHING,
              STOP,
              LAUNCH } kind = NOTHING;
  int scene = -1; // LAUNCH: the scene to start
};

// The one place that turns a scene's follow action into the next thing to
// do. `scene_count` is how many scenes have any clip. Chance, a second
// slot, the other actions and the enabled switch belong here.
inline FollowResult decide(FollowAction action, int scene, int scene_count) {
  switch (action) {
    case FollowAction::STOP:
      return {FollowResult::STOP, -1};
    case FollowAction::NEXT:
      // Ableton wraps to the first scene after the last; here the last
      // scene stops. A wrapping Next would go behind the same name.
      if (scene + 1 < scene_count) return {FollowResult::LAUNCH, scene + 1};
      return {FollowResult::STOP, -1};
    default:
      return {FollowResult::NOTHING, -1};
  }
}

} // namespace followaction

#endif
