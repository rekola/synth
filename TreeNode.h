#ifndef _TREENODE_H_
#define _TREENODE_H_

#include "ChannelConfiguration.h"

#include <cmath>
#include <memory>
#include <unordered_map>

// Shared tree-of-children infrastructure for both TrackState (the
// persistent, per-block-rendered track tree) and VoiceState (the
// ephemeral, per-note voice chain) - see
// plans/trackstate-voicestate-split.md for why those are two distinct
// class hierarchies rather than one. Templated on `Derived` (always the
// immediate subclass - TrackState or VoiceState, never any
// further-derived leaf/wrapper type below it) purely so `children_` can
// hold the pointer type appropriate to whichever tree this instance
// belongs to, without a manual downcast at every call site - every method
// here is otherwise a plain, non-hierarchy-specific helper.
template <typename Derived>
class TreeNode {
 public:
  explicit TreeNode(const ChannelConfiguration & channel_config) : channel_config_(channel_config) { }
  virtual ~TreeNode() { }

  const ChannelConfiguration & getChannelConfiguration() const { return channel_config_; }

  // Mutable access - only SongState::initialize() uses this, to push the
  // just-loaded Song's floor-reflection parameters (ChannelConfiguration.h)
  // into this already-constructed instance's stored copy, the same way
  // main.cpp's setAudioOutSampleRate()/setAmbisonicOrder() calls already
  // finalize one after construction. Every other TrackState is built
  // fresh per song load and never needs this.
  ChannelConfiguration & getMutableChannelConfiguration() { return channel_config_; }

  void addChild(int internal_id, std::unique_ptr<Derived> child) { children_[internal_id] = std::move(child); }

  Derived * getChildByInternalId(int id) {
    for (auto & [ child_id, child ] : getChildren()) {
      if (id == child_id) return child.get();
      auto r = child->getChildByInternalId(id);
      if (r) return r;
    }
    return nullptr;
  }

  const Derived * getChildByInternalId(int id) const {
    for (auto & [ child_id, child ] : getChildren()) {
      if (id == child_id) return child.get();
      auto r = child->getChildByInternalId(id);
      if (r) return r;
    }
    return nullptr;
  }

  void removeChild(int id) {
    auto it = children_.find(id);
    if (it != children_.end()) children_.erase(it);
    else {
      for (auto & [ child_id, child ] : getChildren()) child->removeChild(id);
    }
  }

  const std::unordered_map<int, std::unique_ptr<Derived> > & getChildren() const { return children_; }
  std::unordered_map<int, std::unique_ptr<Derived> > & getChildren() { return children_; }

  static inline float gainToDecibels(float gain) {
    return (gain <= .00001f ? -100.f : (float)(20.0 * log10(gain)));
  }

  static inline float decibelsToGain(float db) {
    return (db > -100.f ? powf(10.0f, db * 0.05f) : 0);
  }

 private:
  ChannelConfiguration channel_config_;
  std::unordered_map<int, std::unique_ptr<Derived> > children_;
};

#endif
