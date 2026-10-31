#include <cellui/LinkStatus.h>

#include <algorithm>

namespace cellui {

const char * linkClassName(LinkClass cls) {
  switch (cls) {
    case LinkClass::FULL: return "full";
    case LinkClass::HZ_30: return "30";
    case LinkClass::HZ_15: return "15";
    case LinkClass::HZ_5: return "5";
    case LinkClass::HZ_1: return "1";
  }
  return "full";
}

std::string linkClassLabel(LinkClass cls) {
  switch (cls) {
    case LinkClass::FULL: return "Full speed";
    case LinkClass::HZ_30: return "30 Hz";
    case LinkClass::HZ_15: return "15 Hz";
    case LinkClass::HZ_5: return "5 Hz";
    case LinkClass::HZ_1: return "1 Hz";
  }
  return "";
}

bool parseLinkClass(const std::string & name, std::optional<LinkClass> & pin) {
  if (name == "auto") {
    pin.reset();
    return true;
  }
  for (LinkClass cls : {LinkClass::FULL, LinkClass::HZ_30, LinkClass::HZ_15, LinkClass::HZ_5, LinkClass::HZ_1}) {
    if (name == linkClassName(cls)) {
      pin = cls;
      return true;
    }
  }
  return false;
}

LinkClass linkClassForBandwidth(const LinkThresholds & t, double bytes_per_sec, double margin) {
  double b = bytes_per_sec / std::max(margin, 1e-9);
  if (b >= t.full) return LinkClass::FULL;
  if (b >= t.hz30) return LinkClass::HZ_30;
  if (b >= t.hz15) return LinkClass::HZ_15;
  if (b >= t.hz5) return LinkClass::HZ_5;
  return LinkClass::HZ_1;
}

LinkClass linkClassForFps(double fps, double margin) {
  double f = fps / std::max(margin, 1e-9);
  if (f >= 50) return LinkClass::FULL;
  if (f >= 25) return LinkClass::HZ_30;
  if (f >= 12) return LinkClass::HZ_15;
  if (f >= 4) return LinkClass::HZ_5;
  return LinkClass::HZ_1;
}

LinkClass LinkClassifier::update(TimePoint now, LinkClass indicated, LinkClass indicated_with_margin) {
  if (pin_) return current_ = *pin_;
  // Down: every reading for a while said worse; take the best of what they said.
  if (indicated > current_) {
    if (!below_since_) {
      below_since_ = now;
      below_best_ = indicated;
    } else {
      below_best_ = std::min(below_best_, indicated);
    }
    if (now - *below_since_ >= config_.down_after) {
      current_ = below_best_;
      below_since_.reset();
      above_since_.reset();
    }
  } else {
    below_since_.reset();
  }
  // Up: every reading for a long while said better even with the margin; one class at a time.
  if (indicated_with_margin < current_) {
    if (!above_since_) above_since_ = now;
    if (now - *above_since_ >= config_.up_after) {
      current_ = static_cast<LinkClass>(static_cast<int>(current_) - 1);
      above_since_ = now;
    }
  } else {
    above_since_.reset();
  }
  return current_;
}

void LinkMonitor::notifyIfChanged(const LinkStatus & before) {
  if ((before.cls != status_.cls || before.pinned != status_.pinned) && handler_) handler_(status_);
}

void LinkMonitor::pin(std::optional<LinkClass> pin) {
  LinkStatus before = status_;
  classifier_.pin(pin);
  status_.cls = classifier_.current();
  status_.pinned = pin.has_value();
  notifyIfChanged(before);
}

void LinkMonitor::update(TimePoint now, LinkClass indicated, LinkClass indicated_with_margin) {
  LinkStatus before = status_;
  status_.cls = classifier_.update(now, indicated, indicated_with_margin);
  status_.pinned = classifier_.pinned();
  notifyIfChanged(before);
}

void LinkMonitor::set(const LinkStatus & status) {
  LinkStatus before = status_;
  status_ = status;
  notifyIfChanged(before);
}

}  // namespace cellui
