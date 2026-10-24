#pragma once

#include "LaunchpadButtonEvent.h"
#include "LaunchpadChannelPressureEvent.h"
#include "LaunchpadPadEvent.h"

#include <variant>

// What LaunchpadIO::pollEvents() decodes: one of the three event structs.
using LaunchpadInput = std::variant<LaunchpadPadEvent, LaunchpadButtonEvent, LaunchpadChannelPressureEvent>;
