#include "aaw.h"
#include <algorithm>

namespace NextFish {

AAWEngine GlobalAAW;

AspirationWindow AAWEngine::compute_initial_window(Value prev_score, int depth, double tau, double rho) const {
    AspirationWindow w;
    if (depth < 5 || is_mate_score(prev_score)) {
        w.alpha = -VALUE_INFINITE;
        w.beta = VALUE_INFINITE;
        w.delta = 15;
        return w;
    }

    Value delta = static_cast<Value>(20.0 + 15.0 * tau + 10.0 * rho);
    delta = std::clamp(delta, 25, 60);

    w.delta = delta;
    w.alpha = std::max(-VALUE_INFINITE, prev_score - delta);
    w.beta = std::min(VALUE_INFINITE, prev_score + delta);
    w.fails = 0;
    return w;
}

void AAWEngine::handle_failure(AspirationWindow& w, Value score, bool is_fail_low) const {
    w.fails++;
    if (w.fails >= 2) {
        w.alpha = -VALUE_INFINITE;
        w.beta = VALUE_INFINITE;
        return;
    }

    if (is_fail_low) {
        w.alpha = std::max(-VALUE_INFINITE, score - w.delta * 3);
    } else {
        w.beta = std::min(VALUE_INFINITE, score + w.delta * 3);
    }
    w.delta *= 2;
}

} // namespace NextFish
