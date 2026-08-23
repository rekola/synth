#include "InstrumentPool.h"

#include "../instruments/GenericInstrument.h"
#include "../instruments/InstrumentProvider.h"

// Out-of-line (not = default inline in the header) purely so
// default_kit_'s unique_ptr<GenericInstrument> can destroy/move a
// GenericInstrument without the header needing its complete type - see
// InstrumentPool.h's own class comment.
InstrumentPool::InstrumentPool() = default;
InstrumentPool::~InstrumentPool() = default;
InstrumentPool::InstrumentPool(InstrumentPool &&) noexcept = default;
InstrumentPool & InstrumentPool::operator=(InstrumentPool &&) noexcept = default;

void
InstrumentPool::loadParameters(const ParameterSource & input) {
  default_kit_ = std::make_unique<GenericInstrument>();
  default_kit_->setFrom(input.getText("from"));
}

void
InstrumentPool::storeParameters(ParameterSource & output) const {
  if (default_kit_ && !default_kit_->getFrom().empty()) output.set("from", default_kit_->getFrom());
}

void
InstrumentPool::prepare(const InstrumentProvider & provider) {
  if (!default_kit_) default_kit_ = std::make_unique<GenericInstrument>();
  if (default_kit_->getFrom().empty()) default_kit_->setFrom("kit");
  if (default_kit_->getFrom() != "none") default_kit_->prepare(provider);
}

const Track *
InstrumentPool::getDefaultKitInstrument() const {
  if (!default_kit_) return nullptr;
  auto & from = default_kit_->getFrom();
  return (from.empty() || from == "none") ? nullptr : default_kit_.get();
}
