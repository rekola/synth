#ifndef _DEVICELABELS_H_
#define _DEVICELABELS_H_

#include <cstddef>
#include <map>
#include <string>
#include <vector>

// Text helpers for the device pickers - plain string logic, kept apart from
// the audio server so it can be tested without one.

// What tells one device from another of the same make: where it is plugged in.
// `bus_path` is the server's own location string (e.g.
// "pci-0000:00:14.0-usb-0:2:1.0"), which for USB is cut down to the port
// ("usb-0:2:1.0"); without one, the node's own name minus its "alsa_input."/
// "alsa_output." prefix and trailing profile ("usb-Maker_Model-00").
inline std::string deviceDetail(const std::string & node_name, const std::string & bus_path) {
  if (!bus_path.empty()) {
    auto usb = bus_path.rfind("usb-");
    return usb == std::string::npos ? bus_path : bus_path.substr(usb);
  }
  std::string name = node_name;
  for (const char * prefix : {"alsa_input.", "alsa_output."}) {
    std::string p = prefix;
    if (name.compare(0, p.size(), p) == 0) {
      name = name.substr(p.size());
      // A profile suffix like ".analog-stereo" - but only when what is left
      // is still more than the suffix (names contain dots of their own).
      auto dot = name.rfind('.');
      if (dot != std::string::npos && dot > 0) name = name.substr(0, dot);
      break;
    }
  }
  return name;
}

// Makes every label in `labels` unique. Entries that share a label are told
// apart by their detail ("Mic [usb-0:2:1.0]") when theirs all differ, and
// numbered ("Mic (2)") for whatever is still the same - a lone label is never
// touched. `details` runs parallel to `labels`; a missing or empty one just
// means numbering.
inline void disambiguateLabels(std::vector<std::string> & labels, const std::vector<std::string> & details) {
  std::map<std::string, std::vector<size_t>> groups;
  for (size_t i = 0; i < labels.size(); i++) groups[labels[i]].push_back(i);

  for (auto & [label, members] : groups) {
    if (members.size() < 2) continue;
    std::map<std::string, int> detail_counts;
    for (auto i : members) detail_counts[i < details.size() ? details[i] : ""]++;
    for (auto i : members) {
      auto detail = i < details.size() ? details[i] : std::string();
      if (!detail.empty() && detail_counts[detail] == 1) labels[i] = label + " [" + detail + "]";
    }
  }

  // Numbering for anything the details could not separate.
  std::map<std::string, int> seen;
  for (auto & label : labels) {
    int n = ++seen[label];
    if (n > 1) label += " (" + std::to_string(n) + ")";
  }
}

#endif
