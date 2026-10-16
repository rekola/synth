#pragma once

#include <string>
#include <string_view>

// A one-line description of a named command, for the command palette; empty
// for a name that has none. A name with an argument after a colon
// ("switch-to-buffer:song.xml") is described by the part before it.
std::string commandDescription(std::string_view name);
