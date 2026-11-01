#pragma once

#include "types.h"

namespace NextFish {

struct AspirationWindow {
    Value alpha = -VALUE_INFINITE;
    Value beta = VALUE_INFINITE;
    Value delta = 15;
    int fails = 0;
};

class AAWEngine {
public:
    AAWEngine() = default;

    AspirationWindow compute_initial_window(Value prev_score, int depth, double tau, double rho) const;
    void handle_failure(AspirationWindow& w, Value score, bool is_fail_low) const;
};

extern AAWEngine GlobalAAW;

} // namespace NextFish
