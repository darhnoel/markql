#include "color.h"

namespace markql::cli {

/// Holds the singleton color palette used by CLI rendering.
/// MUST match the declaration in the header and MUST not be redefined elsewhere.
/// Inputs are none; side effects occur when consumers print ANSI codes.
Color kColor;

}  // namespace markql::cli
