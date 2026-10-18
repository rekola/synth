#include "InstrumentPool.h"

#include "../instruments/GenericInstrument.h"
#include "../instruments/InstrumentProvider.h"

// Out-of-line (not = default inline in the header) purely so
// default_kit_'s unique_ptr<GenericInstrument> can destroy/move a
// GenericInstrument without the header needing its complete type - see
// InstrumentPool.h's own class comment.
InstrumentPool::InstrumentPool() = default;
InstrumentPool::~InstrumentPool() = default;
InstrumentPool::InstrumentPool(const InstrumentPool &) = default;
InstrumentPool & InstrumentPool::operator=(const InstrumentPool &) = default;

const Track *
InstrumentPool::findByInternalId(int id) const {
  for (auto & instrument : instruments_) {
    if (instrument->getInternalId() == id) return instrument.get();
  }
  if (default_kit_ && default_kit_->getInternalId() == id) return getDefaultKitInstrument();
  return nullptr;
}

void
InstrumentPool::setDefaultKit(std::shared_ptr<GenericInstrument> kit) {
  default_kit_ = std::move(kit);
}

std::string
InstrumentPool::getDefaultKitFrom() const {
  return default_kit_ ? default_kit_->getFrom() : std::string();
}

void
InstrumentPool::loadParameters(const ParameterSource & input) {
  default_kit_ = std::make_shared<GenericInstrument>();
  default_kit_->setFrom(input.get<std::string>("from"));
}

void
InstrumentPool::storeParameters(ParameterSource & output) const {
  if (default_kit_ && !default_kit_->getFrom().empty()) output.set("from", default_kit_->getFrom());
}

void
InstrumentPool::prepare(const InstrumentProvider & provider) {
  if (!default_kit_) default_kit_ = std::make_shared<GenericInstrument>();
  if (default_kit_->getFrom().empty()) default_kit_->setFrom("kit");
  if (default_kit_->getFrom() != "none") default_kit_->prepare(provider);
}

const Track *
InstrumentPool::getDefaultKitInstrument() const {
  if (!default_kit_) return nullptr;
  auto & from = default_kit_->getFrom();
  return (from.empty() || from == "none") ? nullptr : default_kit_.get();
}
