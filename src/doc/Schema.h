#ifndef _SCHEMA_H_
#define _SCHEMA_H_

#include "Document.h"

#include <cmath>
#include <string>
#include <type_traits>

// Typed access to node properties. A property is declared once - key and
// default - and read and written only through these, so application code
// never spells a key or a variant type. A property at its default is stored
// as absent, so two documents that mean the same thing are equal.
namespace doc {

template <typename T>
struct Prop {
  const char * key;
  T def;
};

inline Value toValue(bool v) { return v; }
inline Value toValue(const std::string & v) { return v; }
inline Value toValue(double v) { return v; }
inline Value toValue(float v) { return static_cast<double>(v); }
template <typename T, typename = std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool> > >
Value toValue(T v) { return static_cast<int64_t>(v); }

template <typename T>
T fromValue(const Value * v, T def) {
  if (!v) return def;
  if constexpr (std::is_same_v<T, bool>) {
    if (auto b = std::get_if<bool>(v)) return *b;
  } else if constexpr (std::is_same_v<T, std::string>) {
    if (auto s = std::get_if<std::string>(v)) return *s;
  } else if constexpr (std::is_floating_point_v<T>) {
    if (auto d = std::get_if<double>(v)) return static_cast<T>(*d);
    if (auto i = std::get_if<int64_t>(v)) return static_cast<T>(*i);
  } else {
    if (auto i = std::get_if<int64_t>(v)) return static_cast<T>(*i);
  }
  return def;
}

template <typename T>
T get(const Document & d, NodeId node, const Prop<T> & p) {
  auto n = d.get(node);
  return fromValue<T>(n ? n->find(p.key) : nullptr, p.def);
}

// For a string property, without copying it. Valid until the node changes.
inline const std::string & getRef(const Document & d, NodeId node, const Prop<std::string> & p) {
  static const std::string kEmpty;
  auto n = d.get(node);
  auto v = n ? n->find(p.key) : nullptr;
  auto s = v ? std::get_if<std::string>(v) : nullptr;
  return s ? *s : (p.def.empty() ? kEmpty : p.def);
}

template <typename T>
bool isDefault(const T & value, const T & def) {
  if constexpr (std::is_floating_point_v<T>) return std::fabs(value - def) < static_cast<T>(1e-9);
  else return value == def;
}

template <typename T>
void set(Document & d, NodeId node, const Prop<T> & p, T value) {
  d.setProperty(node, p.key, isDefault(value, p.def) ? Value() : toValue(value));
}

}  // namespace doc

#endif
