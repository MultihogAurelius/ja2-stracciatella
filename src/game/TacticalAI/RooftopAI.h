#pragma once

#include "Isometric_Utils.h"

// Returns MOVE_TO_CLIMB (and sets usActionData) only for a worthwhile nearby
// rooftop position. groundCover is an optional alternative already found by AI.
INT8 DecideRooftopPosition(SOLDIERTYPE* soldier, INT16 groundCover = NOWHERE);
