#pragma once

#include "types.h"
#include "position.h"
#include "harenn.h"
#include "dee.h"

namespace NextFish {

class DQRSEngine {
public:
    DQRSEngine() = default;

    Value search(Position& pos, Value alpha, Value beta, int depth_budget, int ply, uint64_t& nodes);

private:
    StateInfo q_states[MAX_PLY + 4];
};

extern DQRSEngine GlobalDQRS;

} // namespace NextFish
