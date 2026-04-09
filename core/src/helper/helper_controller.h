#pragma once

#include "helper_types.h"

namespace markql::helper {

ControllerStep plan_next_step(const ControllerSnapshot& snapshot);

}  // namespace markql::helper
