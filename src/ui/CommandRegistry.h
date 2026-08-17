#ifndef _COMMANDREGISTRY_H_
#define _COMMANDREGISTRY_H_

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
