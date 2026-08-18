#ifndef _COMMANDREGISTRY_H_
#define _COMMANDREGISTRY_H_

#include <algorithm>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

// A name -> callable table for Emacs-style named UI actions (e.g.
// "set-mark", "kill-region"), invoked either via a Keymap (see Keymap.h) or
// directly by name (e.g. from an M-x-style minibuffer). Not to be confused
// with Command.h, the unrelated 4-character tracker effect-column
// mini-language.
class CommandRegistry {
 public:
  void define(std::string name, std::function<void()> fn) {
    // Every prefix of `name` (including the empty one, so matching("")
    // lists everything) indexes straight to every full name sharing it -
    // built once here rather than scanning the whole table on every
    // matching() call, which an interactive autocomplete (M-x) does once
    // per keystroke.
    for (size_t len = 0; len <= name.size(); len++) {
      prefixes_[name.substr(0, len)].push_back(name);
    }
    commands_[std::move(name)] = std::move(fn);
  }

  bool execute(const std::string & name) const {
    auto it = commands_.find(name);
    if (it == commands_.end()) return false;
    it->second();
    return true;
  }

  bool has(const std::string & name) const {
    return commands_.count(name) > 0;
  }

  // The inverse of define() - removes `name` from both commands_ and every
  // prefixes_ entry it was indexed under, so it stops being executable,
  // M-x-completable, or (via has()) reported as defined at all. A no-op if
  // `name` was never defined. Needed for commands whose whole set can
  // shrink at runtime (e.g. TerminalMenu's per-open-buffer
  // "switch-to-buffer:<name>" commands, Controller::refreshBufferCommands())
  // - define() alone can add/overwrite but never retract one.
  void undefine(const std::string & name) {
    if (commands_.erase(name) == 0) return;
    for (size_t len = 0; len <= name.size(); len++) {
      auto it = prefixes_.find(name.substr(0, len));
      if (it == prefixes_.end()) continue;
      auto & matches = it->second;
      matches.erase(std::remove(matches.begin(), matches.end(), name), matches.end());
      if (matches.empty()) prefixes_.erase(it);
    }
  }

  // Every defined name starting with `prefix` - the read-only counterpart
  // to execute(), used by M-x's autocomplete rather than to run anything.
  const std::vector<std::string> & matching(const std::string & prefix) const {
    static const std::vector<std::string> kEmpty;
    auto it = prefixes_.find(prefix);
    return it == prefixes_.end() ? kEmpty : it->second;
  }

 private:
  std::unordered_map<std::string, std::function<void()>> commands_;
  std::unordered_map<std::string, std::vector<std::string>> prefixes_;
};

#endif
