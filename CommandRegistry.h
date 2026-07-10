#ifndef _COMMANDREGISTRY_H_
#define _COMMANDREGISTRY_H_

#include <functional>
#include <string>
#include <unordered_map>

// A name -> callable table for Emacs-style named UI actions (e.g.
// "set-mark", "kill-region"), invoked either via a Keymap (see Keymap.h) or
// directly by name (e.g. from an M-x-style minibuffer). Not to be confused
// with Command.h, the unrelated 4-character tracker effect-column
// mini-language.
class CommandRegistry {
 public:
  void define(std::string name, std::function<void()> fn) {
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

 private:
  std::unordered_map<std::string, std::function<void()>> commands_;
};

#endif
